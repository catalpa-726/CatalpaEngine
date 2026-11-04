#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "ECS/System.h"

namespace Catalpa {
    class Scene;

    // 一个客户端固定步系统的注册令牌；不拥有 Scene/World，也不保存组件或场景指针。
    class CATALPA_API GameplayModule {
    public:
        GameplayModule() = default;
        GameplayModule(const GameplayModule&) = delete;
        GameplayModule& operator=(const GameplayModule&) = delete;

        // 注册一个 Gameplay 阶段系统并编译验证，失败撤销本次注册。
        [[nodiscard]] WorldStatus Install(Scene& scene, SystemDesc system);
        // 拒绝跨场景或仍被依赖的注销，失败保留注册令牌。
        [[nodiscard]] WorldStatus Uninstall(Scene& scene);
        [[nodiscard]] bool IsInstalled() const noexcept { return m_WorldIdentity != 0; }
        // 场景已经卸载时，只清除本地令牌；场景所有的回调已随调度器销毁。
        void ForgetUnloadedScene() noexcept;

    private:
        uint64_t m_WorldIdentity = 0;
        std::string m_SystemId;
    };
}
