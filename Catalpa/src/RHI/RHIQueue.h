#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHISubmission.h"
#include "RHICompletionPoint.h"

/* 公开提交与完成查询，上层不接触 Vulkan 队列 */
namespace Catalpa {
	class RHIDevice;

	/* 当前由主线程串行调用。 */
	class CATALPA_API RHIQueue {
	public:
		virtual ~RHIQueue() = default;

		virtual QueueId GetId() const = 0;
		virtual RHIDevice* GetDevice() noexcept { return nullptr; }

		/**
		 * @brief 将录制完成的命令和同步依赖交给队列，受理后签发完成点。
		 *
		 * @param[in] submission 命令列表及提交所需的同步依赖。
		 * @param[out] completion 接收已提交工作的队列完成点，供后续等待或回收使用。
		 *
		 * @return 返回提交受理结果；只有成功时才可使用 completion 跟踪该批次。
		 */
		[[nodiscard]] virtual RHIStatus Submit(const RHISubmission& submission, RHICompletionPoint& completion) = 0;

		/**
		 * @brief 查询指定提交是否已执行完毕，供上层决定是否复用资源。
		 *
		 * @param[in] point 待查询或等待的队列完成点。
		 * @param[out] complete 接收该提交是否已经执行完毕。
		 *
		 * @return 返回查询操作的状态，complete 给出完成点是否已达到。
		 */
		[[nodiscard]] virtual RHIStatus IsComplete(const RHICompletionPoint& point, bool& complete) = 0;
	};

} /* 命名空间 Catalpa */
