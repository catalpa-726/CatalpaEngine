#pragma once

#include "VulkanHeaders.h"
#include "VulkanBuffer.h"
#include "VulkanPipeline.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanResourceRetirement.h"

#include "Images/PixelFormat.h"

#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "RHI/RHIResourceState.h"

#include <mutex>

/* 登记自有资源和交换链借用图像/视图，并校验设备身份、槽位和代次。 */
namespace Catalpa {

	class VulkanDevice;
	class VulkanResourceStateTracker;

	class VulkanResourceRegistry {
	public:
		VulkanResourceRegistry(VulkanDevice& device, DeviceId id)
		    : m_Device(device), m_DeviceId(id), m_Retirement(device) {}
		/**
		 * @brief 结束 VulkanResourceRegistry 对象的生命周期。
		 */
		~VulkanResourceRegistry();

		VulkanResourceRegistry(const VulkanResourceRegistry&) = delete;
		VulkanResourceRegistry& operator=(const VulkanResourceRegistry&) = delete;

		DeviceId GetDeviceId() const { return m_DeviceId; }
		RHIQueueInfo GetQueueInfo(QueueId) const noexcept;

		/**
		 * @brief 创建自有图像和独立视图；本阶段每张图像最多保留一个活视图。
		 *
		 * @param[in] desc 指定格式、尺寸、层级、数组层和用途的纹理创建配置。
		 * @param[out] texture 成功时接收登记到当前设备的新纹理句柄。
		 *
		 * @return 返回自有纹理的创建与登记结果。
		 */
		[[nodiscard]] RHIStatus CreateTexture(const RHITextureDesc& desc, TextureHandle& texture);
		/**
		 * @brief 为自有纹理创建并登记视图，占用父纹理的一个视图引用。
		 *
		 * @param[in] texture 尚未请求释放且没有存活视图的自有纹理句柄。
		 * @param[in] desc 指定视图格式、类型及覆盖的细节层级与数组层。
		 * @param[out] view 进入时清空，成功后接收当前设备签发的视图句柄。
		 *
		 * @return 父纹理可用且视图创建、登记完成时成功；已有存活视图时返回 Unsupported。
		 */
		[[nodiscard]] RHIStatus CreateTextureView(
		    TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view);

		/**
		 * @brief 请求释放所有者；活 View 保留图像，命令引用保留 View。
		 *
		 * @param[in] texture 待释放的自有纹理句柄。
		 */
		void ReleaseTexture(TextureHandle texture) noexcept;
		/**
		 * @brief 放弃自有纹理视图的逻辑所有权。
		 *
		 * @param[in] view 待释放的自有视图句柄。
		 */
		void ReleaseTextureView(TextureViewHandle view) noexcept;

		/**
		 * @brief 交换链专用登记：同时登记借用图像和 View，不接管原生对象。
		 *
		 * @param[in] record 由交换链继续拥有的原生图像、视图和格式信息，注册表只借用。
		 * @param[out] handle 成功时接收用于后续命令的注册表句柄。
		 *
		 * @return 返回借用图像及视图的登记状态；成功时通过 handle 引用交换链所有的视图。
		 */
		[[nodiscard]] RHIStatus RegisterBorrowedTextureView(VulkanTextureViewRecord record, TextureViewHandle& handle);

		/**
		 * @brief 必须已无命令引用；撤销登记但不销毁原生对象。
		 *
		 * @param[in] handle 仍由交换链拥有的图像视图句柄。
		 */
		void UnregisterBorrowedTextureView(TextureViewHandle handle) noexcept;

		/**
		 * @brief 返回临时借用记录，不得跨登记或注销操作保存此指针。
		 *
		 * @param[in] handle 同一设备上要解析的视图句柄。
		 *
		 * @return 返回临时借用的纹理视图记录；句柄不可解析时返回空指针。
		 */
		const VulkanTextureViewRecord* ResolveTextureView(TextureViewHandle handle) const noexcept;

		/**
		 * @brief 命令引用与 View 所有者释放分开。
		 *
		 * @param[in] handle 本命令需要保活的纹理视图句柄。
		 *
		 * @return 返回增加纹理视图命令引用的状态，句柄无效或资源不允许新引用时返回错误。
		 */
		[[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle) noexcept;
		/**
		 * @brief 减少录制命令持有的纹理视图引用，符合条件时回收槽位。
		 *
		 * @param[in] handle 本命令不再引用的纹理视图。
		 */
		void ReleaseTextureViewReference(TextureViewHandle handle) noexcept;

		/**
		 * @brief 创建采样器、登记设备句柄，并管理所有者与命令持有的引用。
		 * Sampler 所有权与命令引用。
		 *
		 * @param[in] desc 指定过滤、寻址和细节层级范围的采样器配置。
		 * @param[out] sampler 成功时接收登记到当前设备的新采样器句柄。
		 *
		 * @return 返回采样器的创建与登记结果。
		 */
		[[nodiscard]] RHIStatus CreateSampler(const RHISamplerDesc& desc, SamplerHandle& sampler);
		const VulkanSamplerRecord* ResolveSampler(SamplerHandle sampler) const noexcept;
		/**
		 * @brief 放弃采样器的逻辑所有权，命令引用独立跟踪。
		 *
		 * @param[in] sampler 待释放的采样器句柄。
		 */
		void ReleaseSampler(SamplerHandle sampler) noexcept;

		/**
		 * @brief 为已录制命令增加采样器的引用计数。
		 *
		 * @param[in] sampler 本命令需要保活的采样器句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus RetainSampler(SamplerHandle sampler) noexcept;
		/**
		 * @brief 减少录制命令持有的采样器引用，符合条件时回收槽位。
		 *
		 * @param[in] sampler 本命令不再引用的采样器。
		 */
		void ReleaseSamplerReference(SamplerHandle sampler) noexcept;

		/**
		 * @brief 自有管线：创建后由登记表持有原生载荷。
		 *
		 * @param[in] desc 图形管线的着色器、资源布局和固定功能状态。
		 * @param[out] pipeline 成功时接收当前设备注册表签发的图形管线句柄。
		 *
		 * @return 返回原生图形管线的创建与登记状态。
		 */
		[[nodiscard]] RHIStatus CreateGraphicsPipeline(
		    const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline);

		/**
		 * @brief 验证并创建计算管线，在注册表中分配可供命令引用的槽位。
		 *
		 * @param[in] desc 着色器计算入口、推送常量和描述符绑定配置。
		 * @param[out] pipeline 进入时清空，成功后接收携带设备标识、槽位和代次的计算管线句柄。
		 *
		 * @return 管线创建并登记后成功；槽位不足或管线创建失败时返回对应错误。
		 */
		[[nodiscard]] RHIStatus CreateComputePipeline(const RHIComputePipelineDesc&, ComputePipelineHandle&);

		/**
		 * @brief 只解析仍允许新使用的管线。
		 *
		 * @param[in] pipeline 要解析的图形管线句柄，须属于当前设备及有效代次。
		 *
		 * @return 返回可供新命令使用的管线记录；句柄不可用时返回空指针。
		 */
		const VulkanPipelineRecord* ResolveGraphicsPipeline(GraphicsPipelineHandle pipeline) const noexcept;

		/**
		 * @brief 提出所有者释放请求；有命令引用时延迟退休。
		 *
		 * @param[in] pipeline 待释放的管线句柄。
		 */
		void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) noexcept;

		/**
		 * @brief 为已录制命令增加管线的引用计数。
		 *
		 * @param[in] pipeline 本命令需要保活的管线句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus RetainPipeline(GraphicsPipelineHandle pipeline) noexcept;
		/**
		 * @brief 归还命令持有的管线引用，符合条件时回收槽位。
		 *
		 * @param[in] pipeline 本次命令不再引用的管线。
		 */
		void ReleasePipeline(GraphicsPipelineHandle pipeline) noexcept;

		/**
		 * @brief 创建底层缓冲并登记其设备标识、用途、状态及所有者引用。
		 * 自有 Buffer：逻辑所有权与命令引用分别管理。
		 *
		 * @param[in] desc 待创建缓冲区的字节容量、用途及内存域。
		 * @param[out] buffer 成功时接收已登记的缓冲区句柄。
		 *
		 * @return 返回缓冲区的创建与登记状态。
		 */
		[[nodiscard]] RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer);
		/**
		 * @brief 向上传缓冲的映射区写入数据，并登记已初始化的字节范围。
		 *
		 * @param[in] buffer 当前设备中尚未请求释放、没有命令引用的上传缓冲。
		 * @param[in] offset 目标缓冲首字节到写入起点的距离，单位为字节。
		 * @param[in] data 待复制的源字节；空数据在句柄、内存域和范围检查通过后直接成功。
		 *
		 * @return 成功时登记已写入范围；刷新失败时清空已初始化范围，因为映射内存可能已经被改写。
		 */
		[[nodiscard]] RHIStatus WriteBuffer(BufferHandle buffer, uint64_t offset, std::span<const std::byte> data);
		const VulkanBufferRecord* ResolveBuffer(BufferHandle buffer) const noexcept;
		/**
		 * @brief 放弃缓冲区的逻辑所有权，已有命令引用仍保持资源有效。
		 *
		 * @param[in] buffer 调用方不再拥有的缓冲区句柄。
		 */
		void ReleaseBuffer(BufferHandle buffer) noexcept;

		/**
		 * @brief 为已录制命令增加缓冲区的引用计数。
		 *
		 * @param[in] buffer 本命令需要保活的缓冲区句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus RetainBuffer(BufferHandle buffer) noexcept;
		/**
		 * @brief 减少录制命令持有的缓冲区引用，符合条件时回收槽位。
		 *
		 * @param[in] buffer 本命令不再引用的缓冲区。
		 */
		void ReleaseBufferReference(BufferHandle buffer) noexcept;

	private:
		friend class VulkanResourceStateTracker;

		static constexpr uint32_t InvalidIndex = GenerationalHandle<RHITextureViewTag>::InvalidIndex;

		struct TextureSlot {
			uint32_t generation = 1;
			uint32_t nextFree = InvalidIndex;

			/* 由活 View 持有；本阶段最多为 1。 */
			uint32_t references = 0;
			bool releaseRequested = false;
			VulkanTextureRecord record{};

			/* 同一图像的唯一状态；View 重建不重置这里。 */
			RHITextureState state{};
		};

		struct ViewSlot {
			uint32_t generation = 1;
			uint32_t nextFree = InvalidIndex;

			/* 由命令列表持有，保留至安全 reset。 */
			uint32_t references = 0;
			bool releaseRequested = false;
			VulkanTextureViewRecord record{};
		};

		struct PipelineSlot {
			uint32_t generation = 1;
			uint32_t nextFree = InvalidIndex;
			uint32_t references = 0;
			bool releaseRequested = false;
			VulkanPipelineRecord record{};
		};

		struct SamplerSlot {
			uint32_t generation = 1;
			uint32_t nextFree = InvalidIndex;
			uint32_t references = 0;
			bool releaseRequested = false;
			VulkanSamplerRecord record{};
		};

		struct BufferSlot {
			uint32_t generation = 1;
			uint32_t nextFree = InvalidIndex;
			uint32_t references = 0;
			bool releaseRequested = false;
			VulkanBufferRecord record{};
			RHIBufferState state{};
		};

		const TextureSlot* FindTexture(TextureHandle texture) const noexcept;
		const ViewSlot* FindTextureView(TextureViewHandle view) const noexcept;
		const PipelineSlot* FindPipeline(GraphicsPipelineHandle pipeline) const noexcept;
		const BufferSlot* FindBuffer(BufferHandle buffer) const noexcept;
		const SamplerSlot* FindSampler(SamplerHandle sampler) const noexcept;

		void ReleaseTextureReference(TextureHandle texture) noexcept;

		void RecycleTexture(uint32_t index) noexcept;
		void RecycleTextureView(uint32_t index) noexcept;
		void RecyclePipeline(uint32_t index) noexcept;
		void RecycleBuffer(uint32_t index) noexcept;
		void RecycleSampler(uint32_t index) noexcept;

	private:
		VulkanDevice& m_Device;
		/* 录制worker只修改引用计数；资源创建/释放和状态接受仍由宿主线程串行执行。 */
		mutable std::recursive_mutex m_CommandReferenceMutex;
		const DeviceId m_DeviceId;

		std::vector<ViewSlot> m_TextureViews;
		uint32_t m_FirstFree = InvalidIndex;

		std::vector<TextureSlot> m_Textures;
		uint32_t m_FirstFreeTexture = InvalidIndex;

		/* 自有资源通过唯一退休入口销毁。 */
		VulkanResourceRetirement m_Retirement;

		std::vector<PipelineSlot> m_Pipelines;
		uint32_t m_FirstFreePipeline = InvalidIndex;

		std::vector<BufferSlot> m_Buffers;
		uint32_t m_FirstFreeBuffer = InvalidIndex;

		std::vector<SamplerSlot> m_Samplers;
		uint32_t m_FirstFreeSampler = InvalidIndex;
	};

} /* 命名空间 Catalpa */
