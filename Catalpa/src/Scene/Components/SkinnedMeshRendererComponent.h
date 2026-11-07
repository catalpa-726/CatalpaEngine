#pragma once
#include "Animation/Skinning.h"
#include "ECS/World.h"
#include "Scene/PersistentEntityId.h"
namespace Catalpa {
    struct SkinnedMeshRendererComponent {
        AssetId binding;
        PersistentEntityId animator;
        bool enabled = true;
        AssetVersionRef<MeshSkinBinding> bindingVersion;
    };
    template <> struct ComponentTraits<SkinnedMeshRendererComponent> {
        static CATALPA_API const ComponentType& Get();
    };
} // namespace Catalpa
