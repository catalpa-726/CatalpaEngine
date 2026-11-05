#pragma once
#include "Application/EditorSelection.h"
#include "Renderer/Readback/Picking.h"
#include "Renderer/RenderSurface.h"

namespace Catalpa {
    class Application;
}

namespace Catalpa::Editor {
    // 一个工作区只保留最近的点击请求；离屏资源的读回寿命由 Renderer 跟踪至 GPU 完成。
    class PickingController {
      public:
        bool Request(Application&, SceneDocument&, const RenderSurfaceSnapshot&, uint32_t x, uint32_t y,
                     std::string& error);
        void Update(Application&, SceneDocument*, const RenderSurfaceSnapshot*, EditorSelection&,
                    std::string& error);
        void Cancel(Application&);

      private:
        uint64_t m_Request = 0, m_Ticket = 0;
        StableId m_Document;
        SceneHandle m_Scene;
        PickingRequest m_Expected;
    };
}
