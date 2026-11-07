#pragma once
#include "Documents/SceneDocument.h"
#include "Panels/PanelText.h"

namespace Catalpa {
    class Application;
}
namespace Catalpa::Editor {
    // 作者组件通过既有文档历史发布；编辑器不直接驱动设备 voice 或修改 PlayWorld。
    void DrawAudioUIInspector(Application&, SceneDocument&, EntityId, const PanelText&, std::string& error);
} // namespace Catalpa::Editor
