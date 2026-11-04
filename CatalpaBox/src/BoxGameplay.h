#pragma once

#include "Scene/SceneHandle.h"

namespace Catalpa {
    class SceneManager;

    // S03.1 的父子 TRS 样例生产者；只借用 Runtime 服务、持有场景句柄。
    class BoxGameplay {
    public:
        bool Attach(SceneManager& scenes);
        void Update();
        void Detach();

    private:
        SceneManager* m_Scenes = nullptr;
        SceneHandle m_Scene{};
        unsigned m_Stage = 0;
    };
}
