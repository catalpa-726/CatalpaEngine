#pragma once

#include "RHI/RHIDevice.h"
#include "VulkanHeaders.h"
#include "VulkanQueue.h"

#include <mutex>

namespace Catalpa {

	class VulkanBackend;
	class VulkanFrameContext;
	class VulkanQueue;
	class VulkanResourceRegistry;
	class VulkanMemoryAllocator;
	class VulkanDebugUtils;
	class VulkanPipelineCache;
	class VulkanAuxiliaryContext;

	class VulkanDevice : public RHIDevice {
	public:
		static constexpr uint32_t FramesInFlight = 2;

	public:
		/**
		 * @brief 初始化 VulkanDevice。
		 */
		VulkanDevice();
		~VulkanDevice() override;

		VulkanDevice(const VulkanDevice&) = delete;
		VulkanDevice& operator=(const VulkanDevice&) = delete;

		/**
		 * @brief 接管后端并初始化逻辑设备、命令队列和帧资源。
		 *
		 * @param[in] backend 移交所有权的已初始化后端，提供实例、表面和选定的物理设备。
		 *
		 * @return 逻辑设备、队列同步和所有在飞帧上下文均就绪时返回 true，否则返回 false。
		 */
		bool Init(std::unique_ptr<VulkanBackend> backend);
		GraphicsBackend GetBackend() const override { return GraphicsBackend::Vulkan; }
		/**
		 * @brief 设备必须处于有效状态，调用期间停止向队列提交工作。
		 * 设备空闲不等同于呈现 Fence 的资源回收证明。
		 *
		 * @return 返回等待逻辑设备空闲的状态；呈现资源的回收还取决于呈现同步策略。
		 */
		[[nodiscard]] RHIStatus WaitIdle() const override;

		/**
		 * @brief 返回的交换链必须先于设备释放；失败时返回空指针并填写 status。
		 *
		 * @param[in] desc 主窗口交换链请求的像素尺寸、图像格式和呈现模式。
		 * @param[out] status 输出交换链初始化结果，失败时返回空指针。
		 *
		 * @return 成功返回主窗口交换链的所有权，失败返回空指针并填写 status。
		 */
		[[nodiscard]] std::unique_ptr<RHISwapchain> CreateSwapchain(
		    const RHISwapchainDesc& desc, RHIStatus& status) override;

		/**
		 * @brief 停止设备内部的帧和队列资源并释放 Vulkan 设备，外部子资源由持有方预先释放。
		 * 调用前结束 GPU 使用，并释放外部持有的设备子资源；内部帧和队列由此回收。
		 * 调用前结束 GPU 使用，并释放设备创建的子资源。
		 */
		void Shutdown() override;

		VkDevice GetHandle() const { return m_Device; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
		VkPipelineCache GetPipelineCache() const;

		/**
		 * @brief 附加原生窗口使用同一逻辑设备。交换链拥有其新建的表面，窗口须晚于它销毁。
		 *
		 * @param[in] nativeWindow 其寿命须覆盖交换链的原生窗口。
		 * @param[in] desc 所需交换链尺寸、格式及呈现配置。
		 * @param[out] status 接收创建结果和失败原因。
		 *
		 * @return 成功返回附加窗口交换链的所有权，失败返回空指针并填写 status。
		 */
		std::unique_ptr<RHISwapchain> CreateWindowSwapchain(
		    void* nativeWindow, const RHISwapchainDesc&, RHIStatus&) override;
		VkQueue GetPresentQueue() const { return m_PresentQueue; }
		VulkanQueue& GetGraphicsQueue() override { return *m_GraphicsQueue; }
		/**
		 * @brief 根据完整队列标识查找逻辑设备持有的队列。
		 *
		 * @param[in] id 需精确匹配的设备队列标识。
		 *
		 * @return 返回匹配的图形、计算或传输队列的借用指针；无匹配项时返回 nullptr。
		 */
		RHIQueue* GetQueue(QueueId) override;
		/**
		 * @brief 按任务类别选择可用队列，在专用队列缺失时使用备用队列。
		 *
		 * @param[in] kind 要调度的工作类别，影响选择传输、计算或图形队列的顺序。
		 *
		 * @return 返回设备持有的队列引用。传输任务优先使用传输队列，其次为计算队列；计算任务优先使用计算队列，最终均可回退到图形队列。
		 */
		RHIQueue& GetQueue(RHIQueueClass) override;
		/**
		 * @brief 按队列标识查询其队列族以及支持的任务类别。
		 *
		 * @param[in] id 所属逻辑设备内的队列标识。
		 *
		 * @return 返回匹配队列的信息；标识未登记时返回默认构造的信息。
		 */
		RHIQueueInfo GetQueueInfo(QueueId) const override;
		/**
		 * @brief 为本次已获取图像的帧，在指定队列上准备独占的辅助命令上下文。
		 *
		 * @param[in] token 已完成图像获取的当前帧令牌，确定所属设备、帧槽及代次。
		 * @param[in] queue 本设备上将执行该辅助批次的队列标识。
		 * @param[in] ordinal 帧槽内的辅助上下文编号，允许复用已存在编号或紧接末尾添加，当前上限为 1279。
		 * @param[out] output 成功时输出帧令牌与对应的命令列表指针；失败时为空。
		 *
		 * @return 返回辅助上下文准备状态；没有可用帧、时间线能力或有效独占序号时返回错误。
		 */
		[[nodiscard]] RHIStatus PrepareAuxiliaryCommands(RHIFrameToken, QueueId, uint32_t, RHIFrameResources&) override;
		/**
		 * @brief 核对帧令牌、命令列表身份、代次和队列来定位辅助上下文。
		 *
		 * @param[in] token 签发辅助命令的帧槽和代次。
		 * @param[in] commands 需匹配到该上下文所持对象的命令列表指针。
		 * @param[in] queue 分配该辅助上下文时使用的队列标识。
		 *
		 * @return 匹配时返回设备持有的辅助上下文指针；任一身份条件失配时返回 nullptr。
		 */
		VulkanAuxiliaryContext* ResolveAuxiliary(RHIFrameToken, RHICommandList*, QueueId);
		[[nodiscard]] bool HasTimelineSemaphores() const noexcept { return m_TimelineEnabled; }
		uint32_t GetFrameCount() const override { return FramesInFlight; }

		/**
		 * @brief 等待指定帧槽，再重置它的命令资源；超时通过 ready == false 表示。
		 *
		 * @param[in] slot 待复用的帧槽索引。
		 * @param[in] timeoutNanoseconds 等待相关同步条件成立的最长时间，单位为纳秒。
		 * @param[out] frame 接收新的帧令牌和可用命令资源。
		 * @param[out] ready 接收帧槽是否就绪，等待超时时为假。
		 *
		 * @return 返回帧槽等待与重置结果；超时通过 ready 为 false 区分，须在 ready 为 true 时使用 resources。
		 */
		[[nodiscard]] RHIStatus PrepareFrame(
		    uint32_t slot, uint64_t timeoutNanoseconds, RHIFrameResources& frame, bool& ready) override;

		/**
		 * @brief 检查帧令牌的设备身份、槽位和代次，定位当前上下文。
		 *
		 * @param[in] token 使用 PrepareFrame 签发的设备、槽位和代次标识。
		 *
		 * @return 匹配时返回设备持有的帧上下文指针，令牌无效或过期时返回 nullptr。
		 */
		VulkanFrameContext* ResolveFrame(RHIFrameToken token);

		/**
		 * @brief 只登记失设备状态，不在当前调用栈内销毁资源。
		 *
		 * @param[in] status 刚完成的设备操作状态；若为 DeviceLost，则记录为设备故障并原样返回。
		 *
		 * @return 原样返回所观察的状态；设备丢失会同时记入设备的持久故障状态。
		 */
		RHIStatus ObserveStatus(RHIStatus status);
		RHIStatus GetFailure() const {
			std::lock_guard lock(m_StatusMutex);
			return m_DeviceFailure;
		}

		uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
		uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }

		VulkanFrameContext& GetFrameContext(uint32_t frameSlot) { return *m_Frames[frameSlot]; }

		const VulkanDebugUtils& GetDebugUtils() const;

		DeviceId GetDeviceId() const override;

		VulkanResourceRegistry& GetResourceRegistry() { return *m_ResourceRegistry; }
		const VulkanResourceRegistry& GetResourceRegistry() const { return *m_ResourceRegistry; }

		const RHICapabilities& GetCapabilities() const override { return m_Capabilities; }
		bool SupportsScheduledBatches() const noexcept override { return m_TimelineEnabled; }
		/**
		 * @brief 查询设备内存堆的分配量和预算。
		 *
		 * @param[out] output 接收各内存堆的预算记录，按字节计。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus GetMemoryBudgets(std::vector<RHIMemoryHeapBudget>&) const override;
		/**
		 * @brief 查询像素格式在最优平铺图像上的采样、存储及附件等能力。
		 * 查询普通 GPU 纹理的单项格式能力；未知格式返回全 false。
		 *
		 * @param[in] format 要检查的纹理像素格式。
		 *
		 * @return 返回该格式逐项支持的纹理能力；未知格式的各项能力均为 false。
		 */
		RHIFormatSupport GetFormatSupport(PixelFormat format) const override;

		VulkanMemoryAllocator& GetMemoryAllocator() { return *m_MemoryAllocator; }
		const VulkanMemoryAllocator& GetMemoryAllocator() const { return *m_MemoryAllocator; }

		/**
		 * @brief 调用前停止录制和提交，并收尾尚未提交的 Acquire。
		 * 等待设备后，丢弃设备管理的帧命令及其资源引用。
		 * DeviceLost 表示进入失败关闭，不能继续使用该设备。
		 *
		 * @return 返回等待设备并丢弃旧帧命令的状态；设备丢失时进入故障收尾。
		 */
		[[nodiscard]] RHIStatus WaitIdleAndDiscardFrameCommands() override;

		/**
		 * @brief 按计算着色器及其接口创建计算管线。
		 *
		 * @param[in] desc 计算着色器与绑定布局描述。
		 * @param[out] pipeline 成功时接收计算管线句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus CreateComputePipeline(const RHIComputePipelineDesc&, ComputePipelineHandle&) override;
		/**
		 * @brief 同步创建管线；成功后由设备管理原生资源。
		 *
		 * @param[in] desc 着色器、顶点布局及固定功能状态。
		 * @param[out] pipeline 成功时接收管线句柄。
		 *
		 * @return 返回图形管线创建和登记结果，成功时可使用 pipeline。
		 */
		[[nodiscard]] RHIStatus CreateGraphicsPipeline(
		    const RHIGraphicsPipelineDesc& desc, GraphicsPipelineHandle& pipeline) override;
		void ReleaseGraphicsPipeline(GraphicsPipelineHandle pipeline) override;

		/**
		 * @brief 创建 Buffer；成功后由设备登记并管理原生资源。
		 *
		 * @param[in] desc 缓冲区的字节容量、访问用途和内存域。
		 * @param[out] buffer 成功时接收新缓冲区句柄。
		 *
		 * @return 返回缓冲区创建和登记结果，成功时可使用 buffer。
		 */
		[[nodiscard]] RHIStatus CreateBuffer(const RHIBufferDesc& desc, BufferHandle& buffer) override;
		/**
		 * @brief 同步复制 CPU 数据并完成必要的 flush；当前拒绝写入仍被命令引用的 Buffer。
		 *
		 * @param[in] buffer 接收数据的缓冲区。
		 * @param[in] offset 写入起点距缓冲区首字节的偏移。
		 * @param[in] data 本次写入的字节内容。
		 *
		 * @return 返回主机写入及刷新结果，缓冲区仍被命令引用时返回错误。
		 */
		[[nodiscard]] RHIStatus WriteBuffer(
		    BufferHandle buffer, uint64_t offset, std::span<const std::byte> data) override;
		/**
		 * @brief 读回只消费已经完成且包含最近使用的提交，不把任意旧 Fence 当作本次复制的证明。
		 * 非阻塞读回：completion 须覆盖该 Buffer 最近一次接受的使用，完成后先 invalidate。
		 *
		 * @param[in] buffer 读回数据所在的缓冲区。
		 * @param[in] offset 读取起点距缓冲区首字节的偏移。
		 * @param[out] data 接收读回字节的主机内存。
		 * @param[in] completion 覆盖最近一次缓冲区使用的队列完成点。
		 *
		 * @return 返回非阻塞读回状态，读取须由覆盖该缓冲区最近使用的完成点证明安全。
		 */
		[[nodiscard]] RHIStatus ReadBuffer(
		    BufferHandle buffer, uint64_t offset, std::span<std::byte> data, RHICompletionPoint completion) override;
		void ReleaseBuffer(BufferHandle buffer) override;

		/**
		 * @brief 返回独立快照；已接受的队列状态不等于 GPU 已执行完成。
		 *
		 * @param[in] buffer 要查询的缓冲区。
		 * @param[out] snapshot 接收缓冲区描述、版本和访问状态。
		 *
		 * @return 返回缓冲区状态快照的查询结果，成功时 snapshot 是已接受提交的独立快照。
		 */
		[[nodiscard]] RHIStatus GetBufferState(BufferHandle buffer, RHIBufferStateSnapshot& snapshot) const override;
		/**
		 * @brief 从视图读取父纹理已接受的状态和布局。
		 *
		 * @param[in] view 待查询的纹理视图。
		 * @param[out] snapshot 接收视图描述及父图像状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus GetTextureState(
		    TextureViewHandle view, RHITextureStateSnapshot& snapshot) const override;

		/**
		 * @brief 创建指定格式和用途的图像，并由设备注册表管理原生资源。
		 * 自有纹理及 View 的创建和逻辑所有权。
		 * 创建图像及其视图；当前每张图像最多保留一个活视图。
		 *
		 * @param[in] desc 纹理格式、像素尺寸、多级渐远纹理层级数、数组层数和用途。
		 * @param[out] texture 成功时接收纹理句柄。
		 *
		 * @return 返回自有图像的创建及登记结果，成功时 texture 是当前设备句柄。
		 */
		[[nodiscard]] RHIStatus CreateTexture(const RHITextureDesc& desc, TextureHandle& texture) override;
		/**
		 * @brief 为指定纹理创建限定子资源范围的视图。
		 *
		 * @param[in] texture 视图引用的父纹理。
		 * @param[in] desc 视图格式及子资源选择。
		 * @param[out] view 成功时接收纹理视图句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus CreateTextureView(
		    TextureHandle texture, const RHITextureViewDesc& desc, TextureViewHandle& view) override;
		void ReleaseTexture(TextureHandle texture) override;
		void ReleaseTextureView(TextureViewHandle view) override;

		/**
		 * @brief Sampler 创建和逻辑所有权。
		 * 创建采样方式，成功后由设备管理原生对象。
		 *
		 * @param[in] desc 过滤、地址模式及相关采样参数。
		 * @param[out] sampler 成功时接收采样器句柄。
		 *
		 * @return 返回采样器的创建及登记结果，成功时 sampler 是当前设备句柄。
		 */
		[[nodiscard]] RHIStatus CreateSampler(const RHISamplerDesc& desc, SamplerHandle& sampler) override;
		void ReleaseSampler(SamplerHandle sampler) override;
		/**
		 * @brief 可选时间戳能力，池必须先于设备释放；命令会保留池的寿命引用。
		 *
		 * @param[in] desc 查询类型与槽位数。
		 * @param[out] pool 接收查询池所有权。
		 *
		 * @return 返回时间戳查询池的创建状态；能力不可用或创建失败时返回错误。
		 */
		[[nodiscard]] RHIStatus CreateQueryPool(const RHIQueryDesc& desc, std::shared_ptr<RHIQueryPool>& pool) override;

	private:
		/* 后端环境与设备句柄 */
		std::unique_ptr<VulkanBackend> m_Backend;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;

		/* 队列及其所属队列族 */
		std::unique_ptr<VulkanQueue> m_GraphicsQueue;
		std::unique_ptr<VulkanQueue> m_ComputeQueue, m_TransferQueue;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;

		uint32_t m_GraphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
		uint32_t m_PresentQueueFamily = VK_QUEUE_FAMILY_IGNORED;
		std::array<std::vector<std::unique_ptr<VulkanAuxiliaryContext>>, FramesInFlight> m_Auxiliary;
		std::vector<RHIQueueInfo> m_QueueInfos;
		bool m_TimelineEnabled = false;

		/* 帧资源先于登记表和逻辑设备释放 */
		std::array<std::unique_ptr<VulkanFrameContext>, FramesInFlight> m_Frames{};

		/* 登记表内的资源释放完毕后，才能销毁分配器。 */
		std::unique_ptr<VulkanMemoryAllocator> m_MemoryAllocator;

		std::unique_ptr<VulkanResourceRegistry> m_ResourceRegistry;
		std::unique_ptr<VulkanPipelineCache> m_PipelineCache;

		/*
		 * 已启用能力与终止性设备状态
		 * 初始化时记录设备上限与实际启用的可选能力。
		 */
		RHICapabilities m_Capabilities{};
		RHIStatus m_DeviceFailure{};
		mutable std::mutex m_StatusMutex;
	};

}
