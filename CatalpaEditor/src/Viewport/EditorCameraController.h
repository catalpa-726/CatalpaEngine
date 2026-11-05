#pragma once

#include "Camera/CameraLens.h"
#include "Core/Math/TransformMath.h"
#include "Scene/Scene.h"

namespace Catalpa::Editor {
    // 输入适配器已经处理焦点、文字输入和 Gizmo 占用；位移用秒，右键视角用帧内鼠标增量。
    struct EditorCameraInput {
        float right = 0, up = 0, forward = 0;
        float lookX = 0, lookY = 0;
        float seconds = 0;
    };

    // 工作区持有的独立视角。只在显式复位时读取场景相机，移动过程不修改 World。
    class EditorCameraController {
      public:
        bool Reset(const Scene* scene, std::string& error, CameraViewport viewport = {640, 480});
        bool Navigate(const EditorCameraInput&, std::string& error);

        const Math::TransformTRS& Pose() const noexcept {
            return m_Pose;
        }

        const CameraLens& Lens() const noexcept {
            return m_Lens;
        }

        uint64_t HistoryRevision() const noexcept {
            return m_HistoryRevision;
        }

      private:
        Math::TransformTRS m_Pose;
        CameraLens m_Lens;
        uint64_t m_HistoryRevision = 1;
    };
}
