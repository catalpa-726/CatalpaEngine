#pragma once

#include "Scene/SceneHandle.h"
#include "Runtime/GameplayModule.h"
#include <memory>

namespace Catalpa {
    class SceneManager;
    struct FrameTime;
    struct RenderFramePacket;

    // 沙盒只提供示例内容/固定步回调和诊断；输入、时钟、调度与提取由引擎执行。
    class BoxGameplay {
    public:
        bool Attach(SceneManager& scenes);
        void Update(const FrameTime& time, const RenderFramePacket& frame);
        void Detach();

    private:
        struct State;
        SceneManager* m_Scenes = nullptr;
        SceneHandle m_Scene{};
        GameplayModule m_Module;
        std::shared_ptr<State> m_State;
        double m_ReportSeconds = 0.0;
        bool m_ReportedHierarchy = false;
    };
}
