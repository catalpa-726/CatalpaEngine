#pragma once
#include "Scene/Components/IdentityComponent.h"
#include "Scene/Components/TransformComponent.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"

namespace Catalpa {
    // 撤销只保存作者局部值及固定资产引用。父关系按持久身份恢复，不复用旧 World 槽位。
    class SceneSubtreeSnapshot {
        friend class Scene;

        struct Node {
            IdentityComponent identity;
            PersistentEntityId parent;
            Math::TransformTRS local;
            std::optional<CameraComponent> camera;
            std::optional<MeshRendererComponent> mesh;
            std::optional<LightComponent> light;
            std::optional<EnvironmentComponent> environment;
            bool SameAuthorData(const Node&) const;
        };

        uint64_t m_World = 0;
        EntityId m_ParentGeneration;
        std::vector<Node> m_Nodes;

      public:
        template <class F> void ForEachIdentity(F&& f) const {
            for (const auto& node : m_Nodes)
                f(node.identity.persistent);
        }
    };
}
