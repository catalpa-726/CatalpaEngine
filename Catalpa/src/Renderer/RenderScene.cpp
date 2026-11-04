#include "catpch.h"
#include "Renderer/RenderScene.h"
#include "Camera/CameraProjection.h"


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

    Math::MathStatus RenderScene::Apply(RenderFramePacket packet)
    {
        if (packet.sequence == 0 || packet.sequence <= m_Frame.sequence)
            return { Math::MathError::OutOfRange, "Render frame sequence is stale or invalid" };
        if (packet.sceneIdentity == 0 && (!packet.transforms.empty() || !packet.views.empty()))
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
