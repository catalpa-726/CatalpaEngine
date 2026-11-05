#include "Viewport/EditorCameraController.h"
#include "Camera/CameraProjection.h"
#include "Scene/Components/CameraComponent.h"

namespace Catalpa::Editor {
    namespace {
        constexpr float SpeedMetresPerSecond = 3.0f;
        constexpr float MouseRadiansPerPixel = 0.0025f;
        constexpr float WheelMetresPerStep = 0.5f;
        constexpr float WheelStepsPerOrthoDoubling = 4.0f;

        // 与 Runtime 相机选择顺序相同。忽略不具备可用刚性世界姿态的相机，不修整作者数据。
        bool ReadSceneCamera(const Scene& scene, const CameraViewport& viewport, Math::TransformTRS& pose, CameraLens& lens,
                             std::string& error) {
            bool found = false;
            const char* invalidReason = nullptr;
            int32_t priority = std::numeric_limits<int32_t>::min();
            PersistentEntityId selected;
            const auto status = scene.GetWorld().ForEach([&](EntityId id) -> WorldStatus {
                if (!scene.GetWorld().Has<CameraComponent>(id))
                    return {};
                CameraComponent camera;
                IdentityComponent identity;
                TransformComponent transform;
                if (auto s = scene.GetWorld().Get(id, camera); !s.IsSuccess())
                    return s;
                if (!camera.enabled)
                    return {};
                if (auto s = scene.GetIdentity(id, identity); !s.IsSuccess())
                    return s;
                if (found && (camera.priority < priority ||
                              (camera.priority == priority && selected < identity.persistent)))
                    return {};
                if (auto s = scene.GetTransform(id, transform); !s.IsSuccess())
                    return s;
                auto matrix = transform.worldMatrix;
                if (!transform.worldValid) {
                    // 首帧在 Runtime 派生更新之前：无父节点的作者 TRS 即其世界姿态。
                    RelationshipComponent relation;
                    if (auto s = scene.GetRelationship(id, relation); !s.IsSuccess())
                        return s;
                    if (relation.parent.HasValue() || !Math::ComposeTRS(transform.local, matrix).IsSuccess())
                        return {};
                }
                Math::TransformTRS worldPose;
                CameraMatrices candidate;
                if (auto s = BuildCameraMatrices(matrix, camera.lens, viewport, candidate); !s.IsSuccess()) {
                    invalidReason = s.message;
                    return {};
                }
                if (!Math::DecomposeTRS(matrix, worldPose).IsSuccess())
                    return {};
                worldPose.scale = Math::Vec3{1};
                pose = worldPose;
                lens = camera.lens;
                priority = camera.priority;
                selected = identity.persistent;
                found = true;
                return {};
            });
            if (!status.IsSuccess()) {
                error = status.message;
                return false;
            }
            if (!found && invalidReason)
                error = std::string("Starting the editor view at its default pose: ") + invalidReason;
            return true;
        }
    }

    bool EditorCameraController::Reset(const Scene* scene, std::string& error, CameraViewport viewport) {
        if (m_HistoryRevision == UINT64_MAX) {
            error = "Editor camera history exhausted";
            return false;
        }
        Math::TransformTRS pose;
        pose.translation = {0, 2, 10};
        CameraLens lens;
        error.clear();
        if (scene && !ReadSceneCamera(*scene, viewport, pose, lens, error))
            return false;
        m_Pose = pose;
        m_Lens = lens;
        ++m_HistoryRevision;
        return true;
    }

    bool EditorCameraController::Navigate(const EditorCameraInput& input, std::string& error) {
        if (!std::isfinite(input.seconds) || input.seconds < 0 || !std::isfinite(input.right) ||
            !std::isfinite(input.up) || !std::isfinite(input.forward) || !std::isfinite(input.lookX) ||
            !std::isfinite(input.lookY) || !std::isfinite(input.wheel)) {
            error = "Editor camera input contains an invalid time or axis";
            return false;
        }
        auto pose = m_Pose;
        if (input.lookX || input.lookY) {
            const auto oldForward = pose.rotation * Math::Vec3{0, 0, -1};
            const float pitch = std::asin(std::clamp(oldForward.y, -1.f, 1.f));
            constexpr float maxPitch = Math::Pi * 0.5f - 0.01f;
            const float target = std::clamp(pitch - input.lookY * MouseRadiansPerPixel, -maxPitch, maxPitch);
            const auto yaw = glm::angleAxis(-input.lookX * MouseRadiansPerPixel, Math::Vec3{0, 1, 0});
            const auto tilt = glm::angleAxis(target - pitch, Math::Vec3{1, 0, 0});
            pose.rotation = glm::normalize(yaw * pose.rotation * tilt);
        }
        auto direction =
            pose.rotation * Math::Vec3{input.right, 0, -input.forward} + Math::Vec3{0, input.up, 0};
        const float length = glm::length(direction);
        if (length > 1.f)
            direction /= length;
        // 避免窗口失焦、拖动或调试暂停之后的巨步长，不改变模拟时钟。
        pose.translation += direction * (std::min(input.seconds, 0.25f) * SpeedMetresPerSecond);
        auto lens = m_Lens;
        if (input.wheel != 0) {
            if (lens.projection == ProjectionType::Perspective) {
                pose.translation += pose.rotation * Math::Vec3{0, 0, -input.wheel * WheelMetresPerStep};
            } else {
                // 正交画面中沿视线移动不会放大物体，缩放独立视口的可见高度。
                lens.orthographicHeight *= std::exp2(-input.wheel / WheelStepsPerOrthoDoubling);
                if (const auto status = ValidateCameraLens(lens); !status.IsSuccess()) {
                    error = status.message;
                    return false;
                }
            }
        }

        const auto status = Math::ValidateTRS(pose);
        if (!status.IsSuccess()) {
            error = status.message;
            return false;
        }
        m_Pose = pose;
        m_Lens = lens;
        return true;
    }
}
