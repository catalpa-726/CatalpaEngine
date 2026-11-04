#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "ECS/System.h"

namespace Catalpa {

    // 创建线程上的确定性 CPU 调度器。Scene 拥有本对象，外部服务保持借用。
    class CATALPA_API SystemScheduler {
    public:
        SystemScheduler();
        ~SystemScheduler() noexcept;
        SystemScheduler(const SystemScheduler&) = delete;
        SystemScheduler& operator=(const SystemScheduler&) = delete;

        [[nodiscard]] WorldStatus Add(SystemDesc system);
        [[nodiscard]] WorldStatus Remove(std::string_view id);
        // 非运行时整体撤销注册与计划，用于恢复含环/失效依赖的配置；不分配或提交场景命令。
        [[nodiscard]] WorldStatus Clear() noexcept;
        [[nodiscard]] WorldStatus Compile();
        [[nodiscard]] WorldStatus RunPhase(SystemPhase phase, SystemContext& context);
        [[nodiscard]] bool HasSystem(std::string_view id) const noexcept;
        [[nodiscard]] bool IsRunning() const noexcept { return m_Running; }
        // 借用到下一次调度操作；包含失败的系统/资源名，不把诊断保存为长期引用。
        [[nodiscard]] std::string_view GetLastDiagnostic() const noexcept { return m_LastDiagnostic; }

    private:
        [[nodiscard]] WorldStatus CanMutate() noexcept;
        [[nodiscard]] WorldStatus Fail(WorldError code, const char* message,
            std::string_view first = {}, std::string_view second = {}) noexcept;

        std::thread::id m_OwnerThread;
        std::vector<SystemDesc> m_Systems;
        std::vector<std::size_t> m_Order;
        std::string m_LastDiagnostic;
        bool m_Compiled = false;
        bool m_Running = false;
    };

}
