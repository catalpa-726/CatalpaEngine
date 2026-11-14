#pragma once

#include "Core/Base.h"
#include "RHIBackend.h"
#include "RHISwapchain.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHICapabilities.h"
#include "RHIQueue.h"
#include "RHIPipelineDesc.h"
#include "RHIBufferDesc.h"
#include "RHIResourceState.h"
#include "RHITextureDesc.h"
#include "RHISamplerDesc.h"
#include "RHIQueryDesc.h"
#include "RHIMemoryBudget.h"
#include "RHIQueueTypes.h"

namespace Catalpa {

	class CATALPA_API RHIDevice {
	public:
		virtual ~RHIDevice() = default;

		virtual GraphicsBackend GetBackend() const = 0;

		/**
		 * @brief 等待设备各队列上已经提交的工作完成。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus WaitIdle() const = 0;

		/**
		 * @brief 为设备的主呈现表面创建交换链。
		 *
		 * @param[in] desc 请求的呈现像素尺寸、格式和呈现模式。
		 * @param[out] status 输出交换链创建的结果和诊断信息。
		 *
		 * @return 成功返回由调用者持有的交换链，交换链须先于设备释放；失败时返回空指针并写入 status。
		 */
		[[nodiscard]] virtual std::unique_ptr<RHISwapchain> CreateSwapchain(
		    const RHISwapchainDesc& desc, RHIStatus& status) = 0;

		virtual std::unique_ptr<RHISwapchain> CreateWindowSwapchain(void*, const RHISwapchainDesc&, RHIStatus& status) {
			status = {RHIErrorCode::Unsupported, 0, "RHIDevice/CreateWindowSwapchain"};
			return {};
		}

		virtual const RHICapabilities& GetCapabilities() const = 0;
		[[nodiscard]] virtual RHIStatus GetMemoryBudgets(std::vector<RHIMemoryHeapBudget>&) const {
			return {RHIErrorCode::Unsupported, 0, "RHIDevice/MemoryBudgets"};
		}

		/**
		 * @brief 逐项查询指定纹理格式对采样、过滤、存储、附件和传输等用途的支持。
		 *
		 * @param[in] format 待检查用途支持的引擎像素格式。
		 *
		 * @return 返回各用途的独立布尔标志，可据此选择目标格式或过滤方式。
		 */
		virtual RHIFormatSupport GetFormatSupport(PixelFormat format) const = 0;

		/**
		 * @brief 在 GPU 使用已结束后释放逻辑设备及其队列和帧资源。
		 */
		virtual void Shutdown() = 0;

		virtual DeviceId GetDeviceId() const = 0;

		/**
		 * @brief 等待设备空闲并释放帧命令持有的资源引用。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus WaitIdleAndDiscardFrameCommands() = 0;

		virtual RHIQueue& GetGraphicsQueue() = 0;
		virtual RHIQueue* GetQueue(QueueId id) {
			auto& queue = GetGraphicsQueue();
			return queue.GetId() == id ? &queue : nullptr;
		}
		virtual RHIQueue& GetQueue(RHIQueueClass) { return GetGraphicsQueue(); }
		virtual RHIQueueInfo GetQueueInfo(QueueId id) const { return {id, 0, true, true, true}; }
		/**
		 * @brief 按完成点所属的设备和队列查询已受理批次。
		 *
		 * @param[in] point 由受理该批次的队列签发的完成点。
		 * @param[out] complete 输出该批次是否已执行完毕，查询失败或尚未完成时为 false。
		 *
		 * @return 返回查询操作的状态；是否执行完毕由 complete 给出。
		 */
		[[nodiscard]] virtual RHIStatus QueryCompletion(const RHICompletionPoint& point, bool& complete) {
			complete = false;
			auto* queue = GetQueue(point.queue);
			if (!queue || point.device != GetDeviceId())
				return {RHIErrorCode::InvalidDescription, 0, "RHIDevice/CompletionIdentity"};
			return queue->IsComplete(point, complete);
		}
		[[nodiscard]] virtual RHIStatus PrepareAuxiliaryCommands(RHIFrameToken, QueueId, uint32_t, RHIFrameResources&) {
			return {RHIErrorCode::Unsupported, 0, "RHIDevice/AuxiliaryCommands"};
		}
		virtual bool SupportsScheduledBatches() const noexcept { return false; }
		virtual uint32_t GetFrameCount() const = 0;

		/**
		 * @brief 等待指定帧槽可复用，并提供本次录制的命令资源。
		 *
		 * @param[in] slot 待复用的帧槽索引。
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] frame 接收新的帧令牌和可用命令资源。
		 * @param[out] ready 接收帧槽是否就绪，等待超时时为假。
		 *
		 * @return 返回帧槽等待与重置结果；超时通过 ready 为 false 区分，须在 ready 为 true 时使用 resources。
		 */
		[[nodiscard]] virtual RHIStatus PrepareFrame(
		    uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& frame, bool& ready) = 0;

		/**
		 * @brief 按容量、用途和内存域申请设备缓冲区。
		 *
		 * @param[in] desc 缓冲区的字节容量、访问用途和内存域。
		 * @param[out] buffer 成功时接收新缓冲区句柄。
		 *
		 * @return 返回缓冲区创建和登记结果，成功时可使用 buffer。
		 */
		[[nodiscard]] virtual RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer) = 0;

		/**
		 * @brief 将一段主机字节写入指定缓冲区范围。
		 *
		 * @param[in] buffer 接收数据的缓冲区。
		 * @param[in] offset 写入起点距缓冲区首字节的偏移。
		 * @param[in] data 本次写入的字节内容。
		 *
		 * @return 返回主机写入及刷新结果，缓冲区仍被命令引用时返回错误。
		 */
		[[nodiscard]] virtual RHIStatus WriteBuffer(
		    BufferHandle buffer, uint64_t offset, std::span<const std::byte> data) = 0;

		[[nodiscard]] virtual RHIStatus ReadBuffer(BufferHandle, uint64_t, std::span<std::byte>, RHICompletionPoint) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHIDevice::ReadBuffer"};
		}

		/**
		 * @brief 放弃缓冲区的逻辑所有权，已有命令引用仍保持资源有效。
		 *
		 * @param[in] buffer 调用方不再拥有的缓冲区句柄。
		 */
		virtual void ReleaseBuffer(BufferHandle buffer) = 0;

		/**
		 * @brief 创建具有指定格式、用途、层级和数组层数的纹理。
		 *
		 * @param[in] desc 纹理格式、像素尺寸、多级渐远纹理层级数、数组层数和用途。
		 * @param[out] texture 成功时接收纹理句柄。
		 *
		 * @return 返回自有图像的创建及登记结果，成功时 texture 是当前设备句柄。
		 */
		[[nodiscard]] virtual RHIStatus CreateTexture(const RHITextureDesc& desc, TextureHandle& texture) = 0;
		/**
		 * @brief 为指定纹理创建限定子资源范围的视图。
		 *
		 * @param[in] texture 视图引用的父纹理。
		 * @param[in] desc 视图格式及子资源选择。
		 * @param[out] view 成功时接收纹理视图句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus CreateTextureView(
		    TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view) = 0;

		/**
		 * @brief 放弃自有纹理的逻辑所有权，存活视图仍保活父图像。
		 *
		 * @param[in] texture 待释放的自有纹理句柄。
		 */
		virtual void ReleaseTexture(TextureHandle texture) = 0;
		/**
		 * @brief 放弃自有纹理视图的逻辑所有权。
		 *
		 * @param[in] view 待释放的自有视图句柄。
		 */
		virtual void ReleaseTextureView(TextureViewHandle view) = 0;

		/**
		 * @brief 按寻址、过滤等参数创建可供着色器绑定的采样器。
		 *
		 * @param[in] desc 过滤、地址模式及相关采样参数。
		 * @param[out] sampler 成功时接收采样器句柄。
		 *
		 * @return 返回采样器的创建及登记结果，成功时 sampler 是当前设备句柄。
		 */
		[[nodiscard]] virtual RHIStatus CreateSampler(const RHISamplerDesc& desc, SamplerHandle& sampler) = 0;

		/**
		 * @brief 放弃采样器的逻辑所有权，命令引用独立跟踪。
		 *
		 * @param[in] sampler 待释放的采样器句柄。
		 */
		virtual void ReleaseSampler(SamplerHandle sampler) = 0;

		[[nodiscard]] virtual RHIStatus CreateQueryPool(const RHIQueryDesc&, std::shared_ptr<RHIQueryPool>& pool) {
			pool.reset();
			return {.code = RHIErrorCode::Unsupported, .operation = "RHIDevice::CreateQueryPool"};
		}

		/**
		 * @brief 读取队列已接受的缓冲区状态快照。
		 *
		 * @param[in] buffer 要查询的缓冲区。
		 * @param[out] snapshot 接收缓冲区描述、版本和访问状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus GetBufferState(BufferHandle buffer, RHIBufferStateSnapshot& snapshot) const = 0;
		/**
		 * @brief 从视图读取父纹理已接受的状态和布局。
		 *
		 * @param[in] view 待查询的纹理视图。
		 * @param[out] snapshot 接收视图描述及父图像状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus GetTextureState(
		    TextureViewHandle view, RHITextureStateSnapshot& snapshot) const = 0;

		/**
		 * @brief 用着色器和固定功能状态创建图形管线。
		 *
		 * @param[in] desc 着色器、顶点布局及固定功能状态。
		 * @param[out] pipeline 成功时接收管线句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus CreateGraphicsPipeline(
		    const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline) = 0;

		[[nodiscard]] virtual RHIStatus CreateComputePipeline(const RHIComputePipelineDesc&, ComputePipelineHandle&) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHIDevice::CreateComputePipeline"};
		}
		virtual void ReleaseComputePipeline(ComputePipelineHandle handle) { ReleaseGraphicsPipeline(handle); }

		/**
		 * @brief 放弃图形管线的逻辑所有权，已录制命令仍可保活对象。
		 *
		 * @param[in] pipeline 待释放的管线句柄。
		 */
		virtual void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;
	};

}
