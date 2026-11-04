#pragma once

#include "Scene/SceneHandle.h"
#include "Runtime/GameplayModule.h"
#include <memory>

namespace Catalpa {
    class SceneManager;
    class AssetManager;
    struct FrameTime;
    struct RenderFramePacket;

    // 沙盒只提供示例内容/固定步回调和诊断；输入、时钟、调度与提取由引擎执行。
    class BoxGameplay {
    public:
        // reversedZ只选择本样例的镜头契约；渲染后端根据提取的lens匹配深度状态。
        bool Attach(SceneManager& scenes, AssetManager& assets, bool reversedZ = false, bool errorMaterial = false);
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
