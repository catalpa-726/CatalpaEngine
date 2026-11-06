#pragma once

#include "Core/Jobs/JobHandle.h"
#include "Scene/SceneHandle.h"
#include "Assets/AssetId.h"

namespace Catalpa {
    class SceneManager;
    class AssetManager;
    class JobSystem;

    // Box 默认样例的天空装配；只持有指定场景句柄，不给其他作者场景隐式添加环境。
    class BoxDefaultEnvironment final {
    public:
        BoxDefaultEnvironment() = default;
        ~BoxDefaultEnvironment();
        BoxDefaultEnvironment(const BoxDefaultEnvironment&) = delete;
        BoxDefaultEnvironment& operator=(const BoxDefaultEnvironment&) = delete;

        bool Start(SceneManager&, AssetManager&, JobSystem&, SceneHandle, std::string& error);
        void Update();
        // Layer 卸载先取消并等待自己的 Cook 任务，再释放 Runtime 借用。
        void Stop() noexcept;

    private:
        struct Task;
        void Fail(std::string_view error);

        SceneManager* m_Scenes = nullptr;
        AssetManager* m_Assets = nullptr;
        JobSystem* m_Jobs = nullptr;
        SceneHandle m_Scene;
        std::shared_ptr<Task> m_Task;
        JobHandle m_Job;
        AssetId m_Environment;
        bool m_Loading = false;
    };
}
