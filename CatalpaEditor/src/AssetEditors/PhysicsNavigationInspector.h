#pragma once
#include "ECS/EntityId.h"
#include "Panels/PanelText.h"
#include <string>
namespace Catalpa::Editor {
    class SceneDocument;
    void DrawPhysicsNavigationInspector(SceneDocument&, EntityId, std::string& error,
                                        const PanelText& text = {});
} // namespace Catalpa::Editor
