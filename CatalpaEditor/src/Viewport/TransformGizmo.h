#pragma once
#include "Application/EditorSelection.h"
#include "Renderer/RenderView.h"
#include "Panels/PanelText.h"
#include "Commands/TransformDrag.h"

namespace Catalpa::Editor {
    // ImGuizmo 处理屏幕操作，TransformDrag 管理实体实时预览和一次性撤销。
    class TransformGizmo {
      public:
        void Toolbar(const PanelText& text);
        bool Draw(SceneDocument&, const EditorSelection&, const RenderView&, float x, float y, float w,
                  float h, std::string& error);

        bool Cancel(std::string& error) {
            if (m_Drag.Active())
                m_WaitRelease = true;
            return m_Drag.Cancel(error);
        }

      private:
        int m_Mode = 0, m_Operation = -1;
        bool m_Snap = false, m_WaitRelease = false;
        float m_Step = .25f;
        TransformDrag m_Drag;
        uint64_t m_TargetGeneration = 0;
        RenderInstanceId m_Camera;
        uint64_t m_CameraRevision = 0;
        Math::Mat4 m_Preview{1}, m_ParentWorld{1};
    };
}
