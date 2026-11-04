#include "catpch.h"
#include "Renderer/RenderView.h"
#include "Camera/CameraProjection.h"

namespace Catalpa {
    namespace {
        using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;

        // 派生float值只容忍其自身量级的舍入，不用1.0下限掩盖微小非零系数丢失。
        bool SameDerived(double actual, double expected) noexcept
        {
            const double rounding = 8.0 * std::numeric_limits<float>::epsilon()
                * std::max(std::abs(actual), std::abs(expected)) + std::numeric_limits<float>::denorm_min();
            return std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= rounding;
        }

        // 比较最终矩阵与double组合；不改写来源矩阵或重新归一化相机姿态。
        bool SameDerived(const Math::Mat4& actual, const DMat4& expected) noexcept
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!SameDerived(actual[column][row], expected[column][row])) return false;
            return true;
        }

        // 采用CameraSystem相同的最终float双向残差上限，而不是只检查determinant。
        bool InversePair(const Math::Mat4& matrix, const Math::Mat4& inverse) noexcept
        {
            const DMat4 products[]{ DMat4(matrix) * DMat4(inverse), DMat4(inverse) * DMat4(matrix) };
            for (const auto& product : products)
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        if (!std::isfinite(product[column][row])
                            || std::abs(product[column][row] - (column == row ? 1.0 : 0.0)) > 2.0e-4) return false;
            return true;
        }

        // 抖动属于每帧投影变化，历史须保存真实旧VP；其余契约变化会切断连续性。
        bool SameHistoryDomain(const RenderView& oldView, const RenderView& view) noexcept
        {
            return oldView.id == view.id && oldView.camera == view.camera
                && oldView.historyRevision == view.historyRevision && oldView.targetGeneration == view.targetGeneration
                && oldView.lens == view.lens && oldView.viewport.width == view.viewport.width
                && oldView.viewport.height == view.viewport.height && oldView.viewport.flipY == view.viewport.flipY;
        }
    }

    Math::MathStatus ValidateRenderView(const RenderView& view) noexcept
    {
        if (view.id == 0 || view.historyRevision == 0 || view.targetGeneration == 0 || !view.camera.HasValue())
            return { Math::MathError::OutOfRange, "Render view identity, camera, history or target generation is invalid" };
        Math::Mat4 expectedProjection{ 1.0f }, expectedInverse{ 1.0f };
        if (const auto status = BuildCameraProjection(view.lens, view.viewport, expectedProjection, expectedInverse); !status.IsSuccess()) return status;
        const auto& matrices = view.matrices;
        if (!SameDerived(matrices.projection, DMat4(expectedProjection))
            || !SameDerived(matrices.inverseProjection, DMat4(expectedInverse)))
            return { Math::MathError::OutOfRange, "Render view projection disagrees with its lens or pixel extent" };

        Math::TransformTRS pose;
        if (const auto status = Math::DecomposeTRS(matrices.inverseView, pose); !status.IsSuccess()) return status;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(static_cast<double>(pose.scale[axis]) - 1.0) > Math::RecompositionTolerance)
                return { Math::MathError::OutOfRange, "Render view camera world transform must have unit positive scale" };
            if (!SameDerived(matrices.position[axis], matrices.inverseView[3][axis])
                || !SameDerived(matrices.right[axis], matrices.inverseView[0][axis])
                || !SameDerived(matrices.up[axis], matrices.inverseView[1][axis])
                || !SameDerived(matrices.forward[axis], -matrices.inverseView[2][axis]))
                return { Math::MathError::OutOfRange, "Render view camera axes or position disagree with its rigid world pose" };
        }
        if (matrices.view[0][3] != 0.0f || matrices.view[1][3] != 0.0f
            || matrices.view[2][3] != 0.0f || matrices.view[3][3] != 1.0f)
            return { Math::MathError::NonAffine, "Render view world-to-camera matrix must remain affine" };
        if (!InversePair(matrices.view, matrices.inverseView)
            || !InversePair(matrices.projection, matrices.inverseProjection)
            || !InversePair(matrices.viewProjection, matrices.inverseViewProjection)
            || !SameDerived(matrices.viewProjection, DMat4(matrices.projection) * DMat4(matrices.view))
            || !SameDerived(matrices.inverseViewProjection, DMat4(matrices.inverseView) * DMat4(matrices.inverseProjection)))
            return { Math::MathError::OutOfRange, "Render view combined matrices or inverse residuals are inconsistent" };
        return {};
    }

    Math::MathStatus RenderViewHistory::Stage(uint64_t sequence, uint64_t sceneIdentity, std::span<const RenderView> views)
    {
        if (sequence == 0 || sequence < m_CommittedSequence || (sceneIdentity == 0 && !views.empty()))
            return { Math::MathError::OutOfRange, "View history sequence is stale or source scene identity is missing" };
        try {
            std::vector<PreparedRenderView> candidate;
            if (views.size() > candidate.max_size())
                return { Math::MathError::OutOfRange, "View history candidate count exceeds its container range" };
            candidate.reserve(views.size());
            std::set<uint64_t> identities;
            for (const auto& view : views) {
                if (const auto status = ValidateRenderView(view); !status.IsSuccess()) return status;
                if (view.camera.scene != sceneIdentity || !identities.insert(view.id).second)
                    return { Math::MathError::OutOfRange, "View history has a foreign camera or duplicate logical view" };
                PreparedRenderView prepared{ view, {} };
                if (sceneIdentity == m_CommittedScene) {
                    const auto previous = std::find_if(m_Committed.begin(), m_Committed.end(),
                        [&](const PreparedRenderView& oldView) { return oldView.view.id == view.id; });
                    if (previous != m_Committed.end() && SameHistoryDomain(previous->view, view)) {
                        prepared.history.valid = true;
                        prepared.history.previousSequence = m_CommittedSequence;
                        prepared.history.previousViewProjection = previous->view.matrices.viewProjection;
                    }
                }
                candidate.push_back(std::move(prepared));
            }
            m_Pending.swap(candidate);
            m_PendingSequence = sequence;
            m_PendingScene = sceneIdentity;
            m_HasPending = true;
            return {};
        } catch (const std::bad_alloc&) {
            return { Math::MathError::OutOfRange, "Cannot allocate independent view history candidates" };
        }
    }

    void RenderViewHistory::OnSubmitted() noexcept
    {
        if (!m_HasPending) return;
        m_Committed.swap(m_Pending);
        m_CommittedSequence = m_PendingSequence;
        m_CommittedScene = m_PendingScene;
        OnCancelled();
    }

    void RenderViewHistory::OnCancelled() noexcept
    {
        m_Pending.clear();
        m_PendingSequence = 0;
        m_PendingScene = 0;
        m_HasPending = false;
    }

    void RenderViewHistory::Clear() noexcept
    {
        OnCancelled();
        m_Committed.clear();
        m_CommittedSequence = 0;
        m_CommittedScene = 0;
    }
}
