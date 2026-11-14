#pragma once

#include "Core/Base.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIBufferDesc.h"
#include "RHI/RHICompletionPoint.h"
#include "RHI/RHIError.h"
#include "RHI/RHITextureDesc.h"
#include "RHI/RHICommandTypes.h"

/* 声明静态 Buffer 和颜色纹理上传调度器 */
namespace Catalpa {

	class RHIDevice;
	class RenderGraph;

	/*
	 * 当前由主线程调用，负责静态顶点/索引 Buffer（可附加 Storage 用途）和颜色纹理上传。
	 * 导出统一上传服务，供DLL外的GPU缓存契约测试复用同一实现与关闭路径。
	 */
	class CATALPA_API UploadScheduler {
	public:
		explicit UploadScheduler(RHIDevice& device) : m_Device(device) {}
		~UploadScheduler();

		UploadScheduler(const UploadScheduler&) = delete;
		UploadScheduler& operator=(const UploadScheduler&) = delete;

		/**
		 * @brief 新建目标缓冲和暂存缓冲，立即复制源字节并排队录制后续 GPU 上传。
		 * 立即复制 CPU 数据；目标所有权交给调用方，并须保留至请求提交或调度器关闭。
		 *
		 * @param[in] data 需要上传的 CPU 字节。
		 * @param[in] usage 目标缓冲区除复制外的用途。
		 * @param[out] buffer 接收目标缓冲区的所有权。
		 *
		 * @return 返回目标缓冲与上传请求的创建结果；GPU 复制由后续提交完成。
		 */
		[[nodiscard]] RHIStatus CreateBuffer(
		    std::span<const std::byte> data, RHIBufferUsage usage, BufferHandle& buffer);

		/**
		 * @brief 为单层、单级的紧密排列纹理计算复制区域，并将上传交给多子资源路径。
		 * 立即复制 CPU 数据；自动补充 TransferDestination。
		 * 纹理和 View 的所有权交给调用方，须保留至请求提交或调度器关闭。
		 *
		 * @param[in] data 在本次调用内复制的原始像素字节。
		 * @param[in] desc 目标纹理的格式和尺寸。
		 * @param[out] texture 接收纹理所有权。
		 * @param[out] view 接收该纹理的视图所有权。
		 *
		 * @return 返回紧密排列的单个子资源纹理及上传请求的创建状态。
		 */
		[[nodiscard]] RHIStatus CreateTexture(std::span<const std::byte> data, const RHITextureDesc& desc,
		    TextureHandle& texture, TextureViewHandle& view);
		/**
		 * @brief 核对每个纹理子资源的范围和字节偏移，创建目标与暂存资源并登记上传请求。
		 * 多mip/层逐项描述紧密载荷；偏移指向同一CPU副本，调度器不重排源字节。
		 *
		 * @param[in] data 各区域共同引用的源字节。
		 * @param[in] desc 目标纹理及其子资源布局。
		 * @param[in] copies 各复制项的字节偏移及目标子资源。
		 * @param[out] texture 接收纹理所有权。
		 * @param[out] view 接收该纹理的视图所有权。
		 *
		 * @return 返回多子资源纹理、视图及上传请求的创建状态；失败时清理本次临时资源。
		 */
		[[nodiscard]] RHIStatus CreateTexture(std::span<const std::byte> data, const RHITextureDesc& desc,
		    std::span<const RHIBufferTextureCopy> copies, TextureHandle& texture, TextureViewHandle& view);

		/**
		 * @brief 按此刻的队列长度登记复制任务。随后追加的请求属于下一次录制。
		 *
		 * @param[in,out] graph 接收复制任务的渲染图。
		 *
		 * @return 返回当前待上传批次加入渲染图的状态；后续新增请求留待下一次录制。
		 */
		[[nodiscard]] RHIStatus AddPasses(RenderGraph& graph);

		/**
		 * @brief 只接受本次真实绘制提交的完成点；提交成功不等于 GPU 已完成。
		 *
		 * @param[in] completion 队列已接受包含上传绘制的整批提交时签发的完成点。
		 */
		void OnSubmitted(const RHICompletionPoint& completion) noexcept;
		/**
		 * @brief 部分提交后，仅移除写入完成点已被该队列接受、且目标范围已标记初始化的请求；其余暂存数据保留供重录。
		 *
		 * @param[in] points 部分提交中已经由队列接受的完成点。
		 */
		void OnPartiallySubmitted(std::span<const RHICompletionPoint>) noexcept;

		/**
		 * @brief 放弃本次上传录制的计数，保留待上传资源以便下一帧重新录制。
		 * 取消本帧录制只复位计数，待上传请求及其暂存区仍可在下一帧重录。
		 */
		void OnCancelled() noexcept;
		/**
		 * @brief 释放所有仍由调度器持有的上传暂存区并清理待处理请求。
		 */
		void Shutdown() noexcept;

		const RHICompletionPoint& GetLastSubmission() const { return m_LastSubmission; }

	private:
		struct PendingBuffer {
			BufferHandle destination{};
			BufferHandle staging{};
			uint64_t size = 0;
			RHIBufferUsage usage = RHIBufferUsage::None;
		};

		/* 保存尚未提交的纹理上传请求，由调度器持有暂存缓冲区。 */
		struct PendingTexture {
			TextureViewHandle destination{};
			BufferHandle staging{};
			std::vector<RHIBufferTextureCopy> copies;
		};

		RHIDevice& m_Device;
		std::vector<PendingBuffer> m_Pending;
		std::vector<PendingTexture> m_PendingTextures;

		/* 仅加入本帧图的请求前缀属于此次提交。 */
		size_t m_RecordedCount = 0;
		size_t m_RecordedTextureCount = 0;
		bool m_Recorded = false;
		RHICompletionPoint m_LastSubmission{};
	};

}
