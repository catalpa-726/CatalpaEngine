#pragma once

#include "catpch.h"

#include "Camera/CameraLens.h"
#include "Camera/CameraMatrices.h"
#include "Core/Math/TransformMath.h"

namespace Catalpa {

	/* 渲染侧稳定实例身份，仅为值；不引入 ECS/World 的类型或存储布局。 */
	struct RenderInstanceId {
		uint64_t scene = 0;
		uint32_t index = 0;
		uint32_t generation = 0;
		uint64_t part = 0; /* 派生tile/粒子子实例；前三字段始终保持可拾取的Scene实体身份。 */
		[[nodiscard]] constexpr bool HasValue() const noexcept {
			return scene != 0 && generation != 0 && index != std::numeric_limits<uint32_t>::max();
		}
		constexpr auto operator<=>(const RenderInstanceId&) const = default;
	};

	/* 只随独立编辑视图提交；同一实体派生的粒子和地形分块共用选中状态。 */
	struct EditorViewDisplay {
		RenderInstanceId selected;
		bool showGrid = false;
		std::optional<bool> showNormals; /* 未指定时沿用当前渲染配置。 */
	};
	inline constexpr uint32_t SelectedPickBit = 0x80000000u;

	/* 每个逻辑视图独占相机和像素尺寸；目标代次只标记重建，不持有GPU资源。 */
	struct RenderView {
		uint64_t id = 0;
		RenderInstanceId camera;
		uint64_t historyRevision = 0;
		CameraLens lens;
		CameraViewport viewport;
		CameraMatrices matrices;
		uint64_t targetGeneration = 1;
		/* 独立视图由宿主提供刚性姿态，没有对应的 World 实体。 */
		bool independentCamera = false;
		EditorViewDisplay editorDisplay;
	};

	/**
	 * @brief 检查值快照的身份、镜头、刚性姿态、最终float矩阵组合和逆，不查询World。
	 *
	 * @param[in] view 待检视图，包含相机身份、目标代次、镜头、视口和重建校验所需的矩阵。
	 *
	 * @return 返回视图身份、投影、刚体姿态及派生矩阵的校验状态。
	 */
	[[nodiscard]] CATALPA_API Math::MathStatus ValidateRenderView(const RenderView& view) noexcept;

	/* 上一次成功提交的实际VP；首次、切镜头/尺寸/目标重建时明确无历史。 */
	struct RenderViewHistoryState {
		bool valid = false;
		uint64_t previousSequence = 0;
		Math::Mat4 previousViewProjection{1.0f};
	};

	/* 准备阶段自有相机值与派生历史，不向输入RenderFramePacket回写历史。 */
	struct PreparedRenderView {
		RenderView view;
		RenderViewHistoryState history;
	};

	/*
	 * 主线程提交事务：Stage只准备；仅真实队列接受后OnSubmitted推进历史。
	 * acquire/录制/提交失败均OnCancelled，不把未绘制的CPU帧当成前帧。
	 */
	class CATALPA_API RenderViewHistory {
	public:
		/**
		 * @brief 验证视图身份和矩阵，暂存与上次已提交视图对应的历史数据。
		 *
		 * @param[in] sequence 本次场景帧的序号。
		 * @param[in] sceneIdentity 产生该帧的场景标识。
		 * @param[in] views 本帧待呈现的各个独立视图。
		 *
		 * @return 返回新一帧历史候选的暂存结果；暂存成功后仍须按提交结果决定是否发布。
		 */
		[[nodiscard]] Math::MathStatus Stage(
		    uint64_t sequence, uint64_t sceneIdentity, std::span<const RenderView> views);
		[[nodiscard]] const std::vector<PreparedRenderView>& GetPending() const noexcept { return m_Pending; }
		/**
		 * @brief 将本次暂存的视图集合提升为下一帧可读取的历史。
		 */
		void OnSubmitted() noexcept;
		/**
		 * @brief 丢弃未提交的视图候选，保留已提交历史。
		 */
		void OnCancelled() noexcept;
		/**
		 * @brief 同时清除已提交历史与尚未提交的视图候选。
		 */
		void Clear() noexcept;

	private:
		std::vector<PreparedRenderView> m_Committed;
		std::vector<PreparedRenderView> m_Pending;
		uint64_t m_CommittedSequence = 0;
		uint64_t m_CommittedScene = 0;
		uint64_t m_PendingSequence = 0;
		uint64_t m_PendingScene = 0;
		bool m_HasPending = false;
	};

}
