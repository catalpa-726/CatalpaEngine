#include "catpch.h"
#include "Renderer/RenderScene.h"
#include "Camera/CameraProjection.h"
#include "Geometry/MeshValidation.h"


namespace Catalpa {
    namespace {
        // 消费者入口拒绝任意非有限矩阵，避免坏包污染已接受的快照。
        bool IsFinite(const Math::Mat4& matrix) noexcept
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }
        // 相机位置与坐标轴也必须全部为有限值。
        bool IsFinite(const Math::Vec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    Math::MathStatus BuildRenderMeshInstance(RenderInstanceId id, const MeshResourceRef& mesh,
        const Math::Mat4& world, const Math::Vec4& color, RenderMeshInstance& output) noexcept
    {
        if (!id.HasValue()) return { Math::MathError::OutOfRange, "Mesh instance identity is invalid" };
        if (const auto status = ValidateMeshResource(mesh); !status.IsSuccess()) return { Math::MathError::OutOfRange, status.message };
        for (int channel = 0; channel < 4; ++channel)
            if (!std::isfinite(color[channel]) || color[channel] < 0.0f || (channel == 3 && color[channel] > 1.0f))
                return { Math::MathError::OutOfRange, "Mesh instance color is outside its finite linear range" };
        Math::Mat4 inverse{ 1.0f };
        if (const auto status = Math::InverseAffine(world, inverse); !status.IsSuccess()) return status;
        RenderMeshInstance candidate;
        candidate.id = id;
        candidate.mesh = mesh;
        candidate.worldMatrix = world;
        // n_world=(M^-1)^T*n_local；不能把非均匀缩放的normal当position直接乘M。
        candidate.normalMatrix = glm::transpose(Math::Mat3{ inverse });
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                if (!std::isfinite(candidate.normalMatrix[column][row]))
                    return { Math::MathError::NonFinite, "Mesh normal matrix is not finite" };
        if (const auto status = TransformBounds(mesh->GetData().localBounds, world, candidate.worldBounds); !status.IsSuccess()) return status;
        candidate.color = color;
        output = std::move(candidate);
        return {};
    }

    Math::MathStatus RenderScene::Apply(RenderFramePacket packet)
    {
        if (packet.sequence == 0 || packet.sequence <= m_Frame.sequence)
            return { Math::MathError::OutOfRange, "Render frame sequence is stale or invalid" };
        if (packet.sceneIdentity == 0 && (!packet.transforms.empty() || !packet.views.empty() || !packet.instances.empty()))
            return { Math::MathError::OutOfRange, "Render frame records require a source scene identity" };
        try {
            std::set<RenderInstanceId> instances;
            for (const auto& transform : packet.transforms) {
                if (!transform.id.HasValue() || transform.id.scene != packet.sceneIdentity || !instances.insert(transform.id).second)
                    return { Math::MathError::OutOfRange, "Render transform identity is foreign, invalid or duplicated" };
                if (!IsFinite(transform.worldMatrix)) return { Math::MathError::NonFinite, "Render transform matrix is not finite" };
                if (transform.worldMatrix[0][3] != 0.0f || transform.worldMatrix[1][3] != 0.0f
                    || transform.worldMatrix[2][3] != 0.0f || transform.worldMatrix[3][3] != 1.0f)
                    return { Math::MathError::NonAffine, "Render transform must preserve the affine world convention" };
            }
            std::set<RenderInstanceId> geometryInstances;
            for (const auto& instance : packet.instances) {
                if (instance.id.scene != packet.sceneIdentity || !instances.contains(instance.id)
                    || !geometryInstances.insert(instance.id).second)
                    return { Math::MathError::OutOfRange, "Mesh instance has foreign, duplicated or missing transform identity" };
                const auto source = std::find_if(packet.transforms.begin(), packet.transforms.end(),
                    [&](const RenderTransform& transform) { return transform.id == instance.id; });
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        if (instance.worldMatrix[column][row] != source->worldMatrix[column][row])
                            return { Math::MathError::OutOfRange, "Mesh instance and transform snapshot disagree" };
                RenderMeshInstance expected;
                if (const auto status = BuildRenderMeshInstance(instance.id, instance.mesh, instance.worldMatrix, instance.color, expected); !status.IsSuccess()) return status;
                for (int column = 0; column < 3; ++column)
                    for (int row = 0; row < 3; ++row) {
                        const double actual = instance.normalMatrix[column][row], required = expected.normalMatrix[column][row];
                        if (!std::isfinite(actual) || std::abs(actual - required) > Math::RecompositionTolerance * std::abs(required))
                            return { Math::MathError::OutOfRange, "Mesh normal matrix disagrees with inverse transpose" };
                    }
                if (const auto status = ValidateBounds(instance.worldBounds); !status.IsSuccess()) return status;
                if (instance.worldBounds.empty != expected.worldBounds.empty)
                    return { Math::MathError::OutOfRange, "Mesh world bounds have inconsistent empty state" };
                if (!expected.worldBounds.empty)
                    for (int axis = 0; axis < 3; ++axis)
                        if (instance.worldBounds.minimum[axis] > expected.worldBounds.minimum[axis]
                            || instance.worldBounds.maximum[axis] < expected.worldBounds.maximum[axis])
                            return { Math::MathError::OutOfRange, "Mesh world bounds are not conservative" };
            }
            std::set<uint64_t> views;
            for (const auto& view : packet.views) {
                if (view.id == 0 || view.historyRevision == 0 || !views.insert(view.id).second
                    || !view.camera.HasValue() || view.camera.scene != packet.sceneIdentity || !instances.contains(view.camera))
                    return { Math::MathError::OutOfRange, "Render view has invalid or duplicate identity or missing source transform" };
                if (view.viewport.width == 0 || view.viewport.height == 0
                    || !std::isfinite(view.viewport.jitterPixelsX) || !std::isfinite(view.viewport.jitterPixelsY))
                    return { Math::MathError::OutOfRange, "Render view has invalid extent or jitter" };
                if (const auto status = ValidateCameraLens(view.lens); !status.IsSuccess()) return status;
                const auto& camera = view.matrices;
                if (!IsFinite(camera.position) || !IsFinite(camera.right) || !IsFinite(camera.up) || !IsFinite(camera.forward)
                    || !IsFinite(camera.view) || !IsFinite(camera.projection) || !IsFinite(camera.viewProjection)
                    || !IsFinite(camera.inverseView) || !IsFinite(camera.inverseProjection) || !IsFinite(camera.inverseViewProjection))
                    return { Math::MathError::NonFinite, "Render view matrices or camera axes are not finite" };
            }
            m_Frame = std::move(packet);
            return {};
        } catch (const std::bad_alloc&) {
            return { Math::MathError::OutOfRange, "Cannot allocate render frame validation state" };
        }
    }

    void RenderScene::Clear() noexcept { m_Frame = {}; }

}
