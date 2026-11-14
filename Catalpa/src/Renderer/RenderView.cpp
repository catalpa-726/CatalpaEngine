#include "catpch.h"

#include "Renderer/RenderView.h"

#include "Camera/CameraProjection.h"

namespace Catalpa {
	namespace {
		using DMat4 = glm::mat<4, 4, double, glm::packed_highp>;

		/**
		 * @brief 派生float值只容忍其自身量级的舍入，不用1.0下限掩盖微小非零系数丢失。
		 *
		 * @param[in] actual 待验证的派生标量。
		 * @param[in] expected 重建得到的参考值，与实际值一起确定浮点舍入容差。
		 *
		 * @return 实际值与重建参考值的差处于其量级对应的舍入容差内时返回 true。
		 */
		bool SameDerived(double actual, double expected) noexcept {
			const double rounding =
			    8.0 * std::numeric_limits<float>::epsilon() * std::max(std::abs(actual), std::abs(expected)) +
			    std::numeric_limits<float>::denorm_min();
			return std::isfinite(actual) && std::isfinite(expected) && std::abs(actual - expected) <= rounding;
		}

		/**
		 * @brief 比较最终矩阵与统一构建器的派生结果；不改写来源矩阵或相机姿态。
		 *
		 * @param[in] actual 待验证的单精度派生矩阵。
		 * @param[in] expected 按相同列、行索引逐元素比较的双精度参考矩阵。
		 *
		 * @return 矩阵各元素与双精度参考值的差均在舍入容差内时返回 true。
		 */
		bool SameDerived(const Math::Mat4& actual, const DMat4& expected) noexcept {
			for (int column = 0; column < 4; ++column)
				for (int row = 0; row < 4; ++row)
					if (!SameDerived(actual[column][row], expected[column][row]))
						return false;
			return true;
		}

		/**
		 * @brief 抖动属于每帧投影变化，历史须保存真实旧VP；其余契约变化会切断连续性。
		 *
		 * @param[in] oldView 已提交历史所对应的旧视图。
		 * @param[in] view 本帧视图；视图身份、相机、修订号、目标代次、镜头和视口应与旧视图兼容。
		 *
		 * @return 可将旧视图用作当前视图的历史数据时返回 true。
		 */
		bool SameHistoryDomain(const RenderView& oldView, const RenderView& view) noexcept {
			return oldView.id == view.id && oldView.camera == view.camera &&
			    oldView.independentCamera == view.independentCamera &&
			    oldView.historyRevision == view.historyRevision && oldView.targetGeneration == view.targetGeneration &&
			    oldView.lens == view.lens && oldView.viewport.width == view.viewport.width &&
			    oldView.viewport.height == view.viewport.height && oldView.viewport.flipY == view.viewport.flipY;
		}
	}

	Math::MathStatus ValidateRenderView(const RenderView& view) noexcept {
		if (view.id == 0 || view.historyRevision == 0 || view.targetGeneration == 0 ||
		    (view.independentCamera ? view.camera != RenderInstanceId{} : !view.camera.HasValue()))
			return {
			    Math::MathError::OutOfRange, "Render view identity, camera, history or target generation is invalid"};
		const auto& matrices = view.matrices;
		/* 从同一作者姿态重建，而非另设一套绝对残差阈值；同时复核镜头精度与完整派生快照。 */
		CameraMatrices expected;
		if (const auto status = BuildCameraMatrices(matrices.inverseView, view.lens, view.viewport, expected);
		    !status.IsSuccess())
			return status;
		if (!SameDerived(matrices.projection, DMat4(expected.projection)) ||
		    !SameDerived(matrices.inverseProjection, DMat4(expected.inverseProjection)))
			return {Math::MathError::OutOfRange, "Render view projection disagrees with its lens or pixel extent"};
		for (int axis = 0; axis < 3; ++axis) {
			if (!SameDerived(matrices.position[axis], expected.position[axis]) ||
			    !SameDerived(matrices.right[axis], expected.right[axis]) ||
			    !SameDerived(matrices.up[axis], expected.up[axis]) ||
			    !SameDerived(matrices.forward[axis], expected.forward[axis]))
				return {Math::MathError::OutOfRange,
				    "Render view camera axes or position disagree with its rigid world pose"};
		}
		if (matrices.view[0][3] != 0.0f || matrices.view[1][3] != 0.0f || matrices.view[2][3] != 0.0f ||
		    matrices.view[3][3] != 1.0f)
			return {Math::MathError::NonAffine, "Render view world-to-camera matrix must remain affine"};
		if (!SameDerived(matrices.view, DMat4(expected.view)) ||
		    !SameDerived(matrices.viewProjection, DMat4(expected.viewProjection)) ||
		    !SameDerived(matrices.inverseViewProjection, DMat4(expected.inverseViewProjection)))
			return {Math::MathError::OutOfRange, "Render view combined matrices or inverse residuals are inconsistent"};
		return {};
	}

	Math::MathStatus RenderViewHistory::Stage(
	    uint64_t sequence, uint64_t sceneIdentity, std::span<const RenderView> views) {
		if (sequence == 0 || sequence < m_CommittedSequence || (sceneIdentity == 0 && !views.empty()))
			return {Math::MathError::OutOfRange, "View history sequence is stale or source scene identity is missing"};
		try {
			std::vector<PreparedRenderView> candidate;
			if (views.size() > candidate.max_size())
				return {Math::MathError::OutOfRange, "View history candidate count exceeds its container range"};
			candidate.reserve(views.size());
			std::set<uint64_t> identities;
			for (const auto& view : views) {
				if (const auto status = ValidateRenderView(view); !status.IsSuccess())
					return status;
				if ((!view.independentCamera && view.camera.scene != sceneIdentity) ||
				    !identities.insert(view.id).second)
					return {Math::MathError::OutOfRange, "View history has a foreign camera or duplicate logical view"};
				PreparedRenderView prepared{view, {}};
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
			return {Math::MathError::OutOfRange, "Cannot allocate independent view history candidates"};
		}
	}

	void RenderViewHistory::OnSubmitted() noexcept {
		if (!m_HasPending)
			return;
		m_Committed.swap(m_Pending);
		m_CommittedSequence = m_PendingSequence;
		m_CommittedScene = m_PendingScene;
		OnCancelled();
	}

	void RenderViewHistory::OnCancelled() noexcept {
		m_Pending.clear();
		m_PendingSequence = 0;
		m_PendingScene = 0;
		m_HasPending = false;
	}

	void RenderViewHistory::Clear() noexcept {
		OnCancelled();
		m_Committed.clear();
		m_CommittedSequence = 0;
		m_CommittedScene = 0;
	}
}
