#pragma once

#include "catpch.h"

#include "Core/Base.h"
#include "ECS/World.h"
#include "Scene/SceneHandle.h"

namespace Catalpa {

    class Scene;
    class ComponentRegistry;

    // Runtime唯一拥有的同步场景集合；本步只更新active Scene，后续再接调度/异步资产。
    class CATALPA_API SceneManager {
    public:
        SceneManager();
        ~SceneManager();
        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;

        // 只在创建线程且尚无Scene时安装共享只读schema；现有Scene不会被悄悄换规则。
        [[nodiscard]] WorldStatus SetComponentSchemas(std::shared_ptr<const ComponentRegistry> schemas);

        [[nodiscard]] WorldStatus CreateScene(std::string name, SceneHandle& output);
        // 指针仅借用到下一次Unload/Clear；业务应长期保存SceneHandle。
        [[nodiscard]] Scene* GetScene(SceneHandle handle) noexcept;
        [[nodiscard]] const Scene* GetScene(SceneHandle handle) const noexcept;
        [[nodiscard]] WorldStatus Unload(SceneHandle handle);
        [[nodiscard]] WorldStatus SetActive(SceneHandle handle);
        [[nodiscard]] Scene* GetActive() noexcept;
        [[nodiscard]] const Scene* GetActive() const noexcept;
        [[nodiscard]] WorldStatus UpdateActive();
        // 在完整 tick/派生/提取作用域保护 active Scene，回调内禁止卸载/切换场景。
        [[nodiscard]] WorldStatus WithActive(const std::function<WorldStatus(Scene&)>& operation);
        // 全部场景可卸载后才一次清理；失败不释放任何仍被借用的场景。
        [[nodiscard]] WorldStatus Clear() noexcept;

    private:
        struct Slot {
            std::unique_ptr<Scene> scene;
            uint32_t generation = 1;
            bool retired = false;
        };
        [[nodiscard]] WorldStatus CanMutate() const noexcept;
        [[nodiscard]] WorldStatus Validate(SceneHandle handle) const noexcept;
        // 卸载后保留槽位代次；代次耗尽的槽永久退役。
        void ReleaseSlot(uint32_t index) noexcept;
        uint64_t m_Identity = 0;
        std::thread::id m_OwnerThread;
        std::shared_ptr<const ComponentRegistry> m_ComponentSchemas;
        std::vector<Slot> m_Slots;
        SceneHandle m_Active;
        bool m_Updating = false;
    };

}
