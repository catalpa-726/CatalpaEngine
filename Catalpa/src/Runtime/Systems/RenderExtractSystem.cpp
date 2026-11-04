#include "catpch.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Runtime/Systems/CameraSystem.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"


namespace Catalpa {

    WorldStatus RenderExtractSystem::Extract(const Scene& scene, uint64_t sequence,
        uint32_t width, uint32_t height, RenderFramePacket& output)
    {
        if (sequence == 0) return { WorldError::InvalidArgument, "Render frame sequence must be nonzero" };
        try {
            RenderFramePacket candidate;
            candidate.sequence = sequence;
            candidate.sceneIdentity = scene.GetWorld().GetIdentity();
            candidate.transforms.reserve(scene.GetWorld().GetEntityCount());
            EntityId selected;
            CameraComponent selectedCamera;
            PersistentEntityId selectedPersistent;
            const auto status = scene.GetWorld().ForEach([&](EntityId entity) -> WorldStatus {
                TransformComponent transform;
                if (const auto result = scene.GetTransform(entity, transform); !result.IsSuccess()) return result;
                if (!transform.worldValid) return { WorldError::InvalidArgument, "Transform must be updated before render extraction" };
                candidate.transforms.push_back({ { entity.world, entity.index, entity.generation }, transform.worldMatrix });
                if (!scene.GetWorld().Has<CameraComponent>(entity)) return {};
                CameraComponent camera;
                if (const auto result = scene.GetWorld().Get(entity, camera); !result.IsSuccess()) return result;
                if (!camera.enabled) return {};
                IdentityComponent identity;
                if (const auto result = scene.GetIdentity(entity, identity); !result.IsSuccess()) return result;
                if (!selected.HasValue() || camera.priority > selectedCamera.priority
                    || (camera.priority == selectedCamera.priority && identity.persistent < selectedPersistent)) {
                    selected = entity;
                    selectedCamera = camera;
                    selectedPersistent = identity.persistent;
                }
                return {};
            });
            if (!status.IsSuccess()) return status;

            // 零像素目标不构建投影；仍发布独立变换快照以正确收束最小化窗口状态。
            if (selected.HasValue() && width != 0 && height != 0) {
                RenderView view;
                view.id = selectedCamera.viewId;
                view.camera = { selected.world, selected.index, selected.generation };
                view.historyRevision = selectedCamera.historyRevision;
                view.lens = selectedCamera.lens;
                view.viewport.width = width;
                view.viewport.height = height;
                if (const auto result = CameraSystem::Build(scene, selected, view.viewport, view.matrices); !result.IsSuccess()) return result;
                candidate.views.push_back(std::move(view));
            }
            output = std::move(candidate);
            return {};
        } catch (const std::bad_alloc&) {
            return { WorldError::AllocationFailed, "Cannot allocate render extraction snapshot" };
        }
    }

}
