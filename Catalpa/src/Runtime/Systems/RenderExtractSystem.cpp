#include "catpch.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Runtime/Systems/CameraSystem.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"
#include "Scene/Components/MeshRendererComponent.h"
#include "Scene/Components/LightComponent.h"
#include "Scene/Components/EnvironmentComponent.h"
#include "Lighting/LightValidation.h"
#include "Renderer/RenderScene.h"


namespace Catalpa {
    namespace {
        // 每个请求独立选相机；只复制值，不保存World迭代器或组件指针。
        struct SelectedCamera {
            EntityId entity;
            CameraComponent component;
            PersistentEntityId persistent;
        };

        // 单事务提取共享场景和各请求相机；仅全部成功后替换输出，旧入口复用同一实现。
        WorldStatus ExtractViews(const Scene& scene, uint64_t sequence,
            std::span<const RenderViewRequest> requests, bool useCameraViewId, RenderFramePacket& output)
        {
            if (sequence == 0) return { WorldError::InvalidArgument, "Render frame sequence must be nonzero" };
            try {
                RenderFramePacket candidate;
                candidate.sequence = sequence;
                candidate.sceneIdentity = scene.GetWorld().GetIdentity();
                candidate.transforms.reserve(scene.GetWorld().GetEntityCount());
                candidate.instances.reserve(scene.GetWorld().GetEntityCount());
                std::set<uint64_t> requestedIds;
                for (const auto& request : requests) {
                    if (request.id == 0 || request.targetGeneration == 0 || !requestedIds.insert(request.id).second
                        || !std::isfinite(request.viewport.jitterPixelsX) || !std::isfinite(request.viewport.jitterPixelsY))
                        return { WorldError::InvalidArgument, "View request identity, target generation or jitter is invalid" };
                }
                std::vector<SelectedCamera> selected(requests.size());
                candidate.views.reserve(requests.size());
                const auto status = scene.GetWorld().ForEach([&](EntityId entity) -> WorldStatus {
                    TransformComponent transform;
                    if (const auto result = scene.GetTransform(entity, transform); !result.IsSuccess()) return result;
                    if (!transform.worldValid) return { WorldError::InvalidArgument, "Transform must be updated before render extraction" };
                    candidate.transforms.push_back({ { entity.world, entity.index, entity.generation }, transform.worldMatrix });
                    if (scene.GetWorld().Has<LightComponent>(entity)) {
                        LightComponent light;
                        if (const auto result = scene.GetWorld().Get(entity, light); !result.IsSuccess()) return result;
                        if (light.enabled) {
                            std::string error;
                            if (!ValidateLightParameters(light.parameters, error))
                                return { WorldError::InvalidArgument, "Light parameters failed validation" };
                            Math::Vec3 direction;
                            if (!TryGetWorldLightDirection(transform.worldMatrix, direction))
                                return { WorldError::InvalidArgument, "Light world direction is degenerate" };
                            candidate.lights.push_back({ { entity.world, entity.index, entity.generation }, light.parameters,
                                Math::Vec3(transform.worldMatrix[3]), direction });
                        }
                    }
                    if (scene.GetWorld().Has<EnvironmentComponent>(entity)) {
                        EnvironmentComponent environment;
                        if (const auto result = scene.GetWorld().Get(entity, environment); !result.IsSuccess()) return result;
                        if (environment.enabled) {
                            // 多个环境没有隐式优先级；拒绝歧义，不能由 ECS 遍历顺序决定背景。
                            if (candidate.environment)
                                return { WorldError::InvalidArgument, "Only one enabled environment is allowed per scene" };
                            candidate.environment = RenderEnvironment{ environment.environment, environment.settings };
                        }
                    }
                    if (scene.GetWorld().Has<MeshRendererComponent>(entity)) {
                        MeshRendererComponent renderer;
                        if (const auto result = scene.GetWorld().Get(entity, renderer); !result.IsSuccess()) return result;
                        if (renderer.visible) {
                            RenderMeshInstance instance;
                            if (const auto result = BuildRenderMeshInstance({ entity.world, entity.index, entity.generation },
                                renderer.mesh, transform.worldMatrix, renderer.color, instance); !result.IsSuccess())
                                return { WorldError::InvalidArgument, result.message };
                            instance.meshVersion = renderer.meshVersion;
                            instance.materials = renderer.materials;
                            candidate.instances.push_back(std::move(instance));
                        }
                    }
                    if (requests.empty() || !scene.GetWorld().Has<CameraComponent>(entity)) return {};
                    CameraComponent camera;
                    if (const auto result = scene.GetWorld().Get(entity, camera); !result.IsSuccess()) return result;
                    if (!camera.enabled) return {};
                    IdentityComponent identity;
                    if (const auto result = scene.GetIdentity(entity, identity); !result.IsSuccess()) return result;
                    for (size_t index = 0; index < requests.size(); ++index) {
                        const auto& request = requests[index];
                        if (request.viewport.width == 0 || request.viewport.height == 0
                            || (request.cameraViewId != 0 && request.cameraViewId != camera.viewId)) continue;
                        auto& choice = selected[index];
                        if (!choice.entity.HasValue() || camera.priority > choice.component.priority
                            || (camera.priority == choice.component.priority && identity.persistent < choice.persistent)) {
                            choice = { entity, camera, identity.persistent };
                        }
                    }
                    return {};
                });
                if (!status.IsSuccess()) return status;

                // 零像素目标不构建投影；仍发布独立变换快照以正确收束最小化窗口状态。
                for (size_t index = 0; index < requests.size(); ++index) {
                    const auto& request = requests[index];
                    const auto& choice = selected[index];
                    if (request.viewport.width == 0 || request.viewport.height == 0) continue;
                    if (!choice.entity.HasValue()) {
                        if (request.cameraViewId != 0) return { WorldError::InvalidArgument, "Requested enabled camera is missing" };
                        continue;
                    }
                    RenderView view;
                    view.id = useCameraViewId ? choice.component.viewId : request.id;
                    view.camera = { choice.entity.world, choice.entity.index, choice.entity.generation };
                    view.historyRevision = choice.component.historyRevision;
                    view.targetGeneration = request.targetGeneration;
                    view.lens = choice.component.lens;
                    view.viewport = request.viewport;
                    if (const auto result = CameraSystem::Build(scene, choice.entity, view.viewport, view.matrices); !result.IsSuccess()) {
                        // 仅相机姿态/镜头值不可用于显示时允许重试。World 访问、分配等错误原样传播。
                        if (result.code == WorldError::InvalidArgument)
                            return { WorldError::InvalidPresentationView, result.message };
                        return result;
                    }
                    candidate.views.push_back(std::move(view));
                }
                output = std::move(candidate);
                return {};
            } catch (const std::bad_alloc&) {
                return { WorldError::AllocationFailed, "Cannot allocate render extraction snapshot" };
            }
        }
    }

    WorldStatus RenderExtractSystem::Extract(const Scene& scene, uint64_t sequence,
        uint32_t width, uint32_t height, RenderFramePacket& output)
    {
        // 保留旧入口选择出的相机viewId，不让旧调用方因多view扩充改变身份或选择规则。
        const RenderViewRequest request{ .viewport = { width, height } };
        return ExtractViews(scene, sequence, std::span{ &request, 1 }, true, output);
    }

    WorldStatus RenderExtractSystem::Extract(const Scene& scene, uint64_t sequence,
        std::span<const RenderViewRequest> requests, RenderFramePacket& output)
    {
        return ExtractViews(scene, sequence, requests, false, output);
    }

}
