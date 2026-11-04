#include "catpch.h"
#include "Runtime/Systems/CameraSystem.h"
#include "Camera/CameraProjection.h"
#include "Scene/Scene.h"
#include "Scene/Components/CameraComponent.h"


namespace Catalpa {
    namespace {
        // 用 double 合成派生矩阵；不让 float 中间溢出继续传播到渲染快照。
        bool Multiply(const Math::Mat4& left, const Math::Mat4& right, Math::Mat4& output) noexcept
        {
            using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;
            const DMat4 product = DMat4(left) * DMat4(right);
            Math::Mat4 candidate{ 0.0f };
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    const double value = product[column][row];
                    if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) return false;
                    candidate[column][row] = static_cast<float>(value);
                }
            }
            output = candidate;
            return true;
        }

        // 世界平移进入最终 float VP 后可能放大抵消误差；发布前检验双向残差。
        bool IsInversePair(const Math::Mat4& matrix, const Math::Mat4& inverse) noexcept
        {
            using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;
            constexpr double tolerance = 2.0e-4;
            const DMat4 products[]{ DMat4(matrix) * DMat4(inverse), DMat4(inverse) * DMat4(matrix) };
            for (const auto& product : products)
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        if (!std::isfinite(product[column][row])
                            || std::abs(product[column][row] - (column == row ? 1.0 : 0.0)) > tolerance) return false;
            return true;
        }
    }

    WorldStatus CameraSystem::Build(const Scene& scene, EntityId camera, const CameraViewport& viewport, CameraMatrices& output)
    {
        CameraComponent component;
        if (const auto status = scene.GetWorld().Get(camera, component); !status.IsSuccess()) return status;
        if (!component.enabled || component.viewId == 0 || component.historyRevision == 0)
            return { WorldError::InvalidArgument, "Camera is disabled or has invalid view identity" };
        TransformComponent transform;
        if (const auto status = scene.GetTransform(camera, transform); !status.IsSuccess()) return status;
        if (!transform.worldValid) return { WorldError::InvalidArgument, "Camera world transform has not been updated" };

        Math::TransformTRS pose;
        if (const auto status = Math::DecomposeTRS(transform.worldMatrix, pose); !status.IsSuccess())
            return { WorldError::InvalidArgument, status.message };
        // 相机坐标系必须刚性；明确拒绝缩放、反射、继承 shear，不静默丢弃作者数据。
        for (int axis = 0; axis < 3; ++axis)
            if (std::abs(static_cast<double>(pose.scale[axis]) - 1.0) > Math::RecompositionTolerance)
                return { WorldError::InvalidArgument, "Camera world transform must be rigid with unit positive scale" };

        CameraMatrices candidate;
        candidate.inverseView = transform.worldMatrix;
        if (const auto status = Math::InverseAffine(transform.worldMatrix, candidate.view); !status.IsSuccess())
            return { WorldError::InvalidArgument, status.message };
        if (const auto status = BuildCameraProjection(component.lens, viewport, candidate.projection, candidate.inverseProjection); !status.IsSuccess())
            return { WorldError::InvalidArgument, status.message };
        if (!Multiply(candidate.projection, candidate.view, candidate.viewProjection)
            || !Multiply(candidate.inverseView, candidate.inverseProjection, candidate.inverseViewProjection))
            return { WorldError::InvalidArgument, "Camera combined matrices exceed finite float range" };
        if (!IsInversePair(candidate.viewProjection, candidate.inverseViewProjection))
            return { WorldError::InvalidArgument, "Camera combined matrices exceed the inverse residual bound" };
        candidate.position = Math::Vec3(transform.worldMatrix[3]);
        candidate.right = Math::Vec3(transform.worldMatrix[0]);
        candidate.up = Math::Vec3(transform.worldMatrix[1]);
        candidate.forward = -Math::Vec3(transform.worldMatrix[2]);
        output = candidate;
        return {};
    }

}
