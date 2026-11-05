#pragma once
#include "Documents/SceneDocument.h"
#include "Core/Math/TransformMath.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/CameraComponent.h"

namespace Catalpa::Editor {
    // UI 只提交意图；变更和历史记录在同一安全点完成。
    bool CreateAuthorEntity(SceneDocument&, std::string name, PersistentEntityId parent,
                            PersistentEntityId& created, std::string& error);
    bool DeleteAuthorEntity(SceneDocument&, PersistentEntityId, std::string& error);
    bool ChangeLocal(SceneDocument&, PersistentEntityId, const Math::TransformTRS& before,
                     const Math::TransformTRS& after, std::string& error);
    bool RenameEntity(SceneDocument&, PersistentEntityId, std::string, const std::string& expected,
                      std::string&);
    bool EditCamera(SceneDocument&, PersistentEntityId, const CameraComponent& next, std::string&);
    bool ReparentEntity(SceneDocument&, PersistentEntityId child, PersistentEntityId parent,
                        std::string& error);
    bool EditLight(SceneDocument&, PersistentEntityId, const LightComponent&, std::string&);
    bool EditEnvironment(SceneDocument&, PersistentEntityId, const EnvironmentComponent&, std::string&);
    bool EditMeshDisplay(SceneDocument&, PersistentEntityId, Math::Vec4 color, bool visible, std::string&);
}
