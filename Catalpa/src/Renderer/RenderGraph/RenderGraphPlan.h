#pragma once

#include "RenderGraphPass.h"
#include "RHI/RHIResourceState.h"
#include "RHI/RHIError.h"
#include "Core/Base.h"
#include "RHI/RHIQueryDesc.h"
#include "RenderGraphTransient.h"

/* 持有编译结果：稳定拓扑顺序、屏障和待提交状态；外部只能读取诊断，不能改写命令步骤。 */
namespace Catalpa {

	class RHICommandList;

	class RenderGraphPlan {
	public:
		RenderGraphPlan() = default;

		RenderGraphPlan(const RenderGraphPlan&) = delete;
		RenderGraphPlan& operator=(const RenderGraphPlan&) = delete;

		RenderGraphPlan(RenderGraphPlan&& other) noexcept { *this = std::move(other); }

		/**
		 * @brief 将渲染计划的任务、资源及瞬态存储转移到当前计划。
		 *
		 * @param[in,out] other 转出编译状态和资源所有权的源计划。
		 *
		 * @return 返回当前对象的引用；自移动时立即返回。
		 */
		RenderGraphPlan& operator=(RenderGraphPlan&& other) noexcept {
			if (this == &other)
				return *this;

			m_Steps = std::move(other.m_Steps);
			m_BufferUpdates = std::move(other.m_BufferUpdates);
			m_TextureUpdates = std::move(other.m_TextureUpdates);
			m_Schedule = std::move(other.m_Schedule);
			m_Queries = std::move(other.m_Queries);
			m_PresentView = other.m_PresentView;
			m_QueryBegin = other.m_QueryBegin;
			m_QueryEnd = other.m_QueryEnd;
			m_Transient = std::move(other.m_Transient);
			m_Valid = std::exchange(other.m_Valid, false);
			return *this;
		}

		[[nodiscard]] CATALPA_API RHIStatus Record(RHICommandList& commands) const;
		[[nodiscard]] bool IsValid() const noexcept { return m_Valid; }
		[[nodiscard]] const RGCompiledSchedule& GetSchedule() const noexcept { return m_Schedule; }

	private:
		friend class RenderGraphCompiler;
		friend class RenderGraphExecutor;
		friend class FrameScheduler;

		enum class StepKind { Copy, TextureUpload, TextureReadback, Draw, Transition, Clear, Compute };

		struct Step {
			std::string name;
			StepKind kind = StepKind::Copy;
			std::vector<RHIBufferBarrier> bufferBarriers;
			std::vector<RHITextureBarrier> textureBarriers;
			BufferHandle source{};
			BufferHandle destination{};
			RHIBufferCopy copy{};
			TextureViewHandle destinationView{};
			RHIBufferTextureCopy textureCopy{};
			TextureViewHandle sourceView{};
			RHITextureBufferCopy readbackCopy{};
			RGIndexedDrawDesc draw{};
			std::vector<RHIBinding> bindings;
			BufferHandle vertices{};
			BufferHandle indices{};
			TextureViewHandle color{};
			TextureViewHandle depth{};
			Extent2D extent{};
			RGComputeDesc compute{};
			BufferHandle indirect{};
			/* 每一步结束时的数据状态，不携带未来完成点；scheduler按实际批次绑定新revision。 */
			std::vector<RHIBufferStateUpdate> bufferStates;
			std::vector<RHITextureStateUpdate> textureStates;
			uint32_t sourcePass = UINT32_MAX;
			RHIQueueClass queue = RHIQueueClass::Graphics;
		};

		std::vector<Step> m_Steps;
		std::vector<RHIBufferStateUpdate> m_BufferUpdates;
		std::vector<RHITextureStateUpdate> m_TextureUpdates;
		RGCompiledSchedule m_Schedule;
		std::shared_ptr<RHIQueryPool> m_Queries;
		TextureViewHandle m_PresentView{};
		bool m_QueryBegin = true, m_QueryEnd = true;
		std::shared_ptr<RGTransientStorage> m_Transient;
		bool m_Valid = false;
	};

}
