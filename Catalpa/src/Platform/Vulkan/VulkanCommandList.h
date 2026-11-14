#pragma once

#include "VulkanHeaders.h"
#include "VulkanDescriptorAllocator.h"
#include "RHI/RHICommandList.h"
#include "Images/PixelFormat.h"

/* 实现 Vulkan 命令录制，借用命令池分配的缓冲 */
namespace Catalpa {

	class VulkanCommandPool;
	class VulkanFrameContext;
	class VulkanDevice;

	class VulkanCommandList : public RHICommandList {
	public:
		explicit VulkanCommandList(VulkanDevice& device) : m_Device(device), m_Descriptors(device) {}
		~VulkanCommandList() override;

		VulkanCommandList(const VulkanCommandList&) = delete;
		VulkanCommandList& operator=(const VulkanCommandList&) = delete;

		/**
		 * @brief 从对应队列族的命令池分配待录制的主命令缓冲区。
		 *
		 * @param[in] pool 提供缓冲区的命令池。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Init(VulkanCommandPool& pool);
		/**
		 * @brief 开始向命令列表录制本批 GPU 操作。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Begin() override;
		/**
		 * @brief 结束录制，将命令列表置为可提交状态。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus End() override;
		/**
		 * @brief 时间戳由帧槽独占；Reset 必须在 rendering scope 之外，结果读取不阻塞队列。
		 *
		 * @param[in] pool 本帧使用的查询池。
		 * @param[in] first 首个槽位的零基索引。
		 * @param[in] count 连续重置的槽位数。
		 *
		 * @return 返回录制查询重置的状态；查询池、索引范围或录制阶段无效时返回错误。
		 */
		[[nodiscard]] RHIStatus ResetQueries(
		    const std::shared_ptr<RHIQueryPool>& pool, uint32_t first, uint32_t count) override;
		/**
		 * @brief 将指定流水线阶段的 GPU 时间戳写入查询池。
		 *
		 * @param[in] pool 保存时间戳的池。
		 * @param[in] query 目标槽位索引。
		 * @param[in] bottom 为 true 时写入流水线底部时间戳，为 false 时写入顶部时间戳。
		 *
		 * @return 返回时间戳写入命令的录制状态；实际查询结果须在 GPU 执行后读取。
		 */
		[[nodiscard]] RHIStatus WriteTimestamp(
		    const std::shared_ptr<RHIQueryPool>& pool, uint32_t query, bool bottom) override;

		/**
		 * @brief Begin 后、首个资源命令前设置一次；实现持有更新副本及资源引用。
		 * 访问声明须与实际录制一致，设置失败后不得继续提交。
		 *
		 * @param[in] buffers 缓冲区的预期版本、访问与初始化范围。
		 * @param[in] textures 纹理视图及其父图像的预期状态。
		 *
		 * @return 返回安装资源更新计划的状态；失败的计划不可提交。
		 */
		[[nodiscard]] RHIStatus SetResourceStatePlan(
		    std::span<const RHIBufferStateUpdate> buffers, std::span<const RHITextureStateUpdate> textures) override;

		RHICommandListState GetState() const override { return m_State; }
		VkCommandBuffer GetHandle() const { return m_CommandBuffer; }
		uint32_t GetQueueFamily() const noexcept { return m_QueueFamily; }
		std::span<const RHICompletionPoint> GetRequiredGpuDependencies() const noexcept {
			return m_RequiredGpuDependencies;
		}

		/**
		 * @brief 标记已受理的命令列表，防止在队列仍使用它时再次录制。
		 * 记录已成功接受的提交，本身不执行提交
		 */
		void MarkSubmitted();

		/**
		 * @brief 在渲染段外录制显式依赖。同一调用中每张图像最多出现一次；状态基线和提交顺序由调用者保证。
		 *
		 * @param[in] barriers 本次录制的图像访问、布局及所有权转换，同一图像不得在该批次重复出现。
		 *
		 * @return 返回图像屏障录制状态；布局、归属或子资源范围无效时返回错误。
		 */
		[[nodiscard]] RHIStatus PipelineBarrier(std::span<const RHITextureBarrier> barriers) override;

		/**
		 * @brief 附件须已处于目标布局，只读深度附件须使用 Load/Store。支持单层单采样渲染，可仅清除或仅使用深度附件。
		 *
		 * @param[in] desc 颜色与深度附件、装载操作及渲染区域。
		 *
		 * @return 返回开始动态渲染的状态；附件或命令列表状态不满足要求时返回错误。
		 */
		[[nodiscard]] RHIStatus BeginRendering(const RHIRenderingDesc& desc) override;
		/**
		 * @brief 结束动态渲染段，继续录制渲染段外的命令。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus EndRendering() override;

		/**
		 * @brief 在普通录制阶段或渲染段内设置首个动态视口和裁剪区，坐标由调用者提供。
		 *
		 * @param[in] viewport 视口坐标、尺寸及深度范围。
		 * @param[in] scissor 帧缓冲像素坐标下的裁剪范围。
		 *
		 * @return 返回视口和裁剪区的设置状态；无效范围或录制状态会返回错误。
		 */
		[[nodiscard]] RHIStatus SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor) override;

		/**
		 * @brief 录制管线绑定，并保留其命令引用。
		 *
		 * @param[in] pipeline 当前设备创建的图形管线句柄。
		 *
		 * @return 返回绑定图形管线的录制状态，含句柄及录制阶段检查结果。
		 */
		[[nodiscard]] RHIStatus BindGraphicsPipeline(GraphicsPipelineHandle pipeline) override;
		/**
		 * @brief 写入当前图形管线的完整参数块，偏移固定为 0。
		 * 每次成功绑定含参数块的管线后，必须重新写入。
		 * 数据只在本次调用期间借用，字节数须与管线声明完全一致。
		 *
		 * @param[in] data 从偏移 0 写入的完整推送常量字节，长度须与当前图形管线的声明一致。
		 *
		 * @return 返回推送常量写入状态；当前管线与参数大小不匹配时返回错误。
		 */
		[[nodiscard]] RHIStatus SetGraphicsPushConstants(std::span<const std::byte> data) override;

		/**
		 * @brief 校验完整绑定布局，分配新 set，并保留其引用资源。
		 * 提供当前管线要求的完整 set 0；每次成功绑定管线后重新设置。
		 *
		 * @param[in] bindings 当前管线第 0 组的全部资源绑定，编号及类型须与布局一致。
		 *
		 * @return 返回本次描述符绑定的状态，反映布局核对、资源引用及描述符集分配结果。
		 */
		[[nodiscard]] RHIStatus SetGraphicsBindings(std::span<const RHIBinding> bindings) override;
		/**
		 * @brief 为后续计算分派绑定计算管线。
		 *
		 * @param[in] pipeline 当前设备创建的计算管线句柄。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus BindComputePipeline(ComputePipelineHandle pipeline) override;
		/**
		 * @brief 设置当前计算管线的资源绑定。
		 *
		 * @param[in] bindings 计算着色器使用的缓冲区、纹理和采样器绑定项。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus SetComputeBindings(std::span<const RHIBinding> bindings) override;
		/**
		 * @brief 向当前计算管线写入推送常量。
		 *
		 * @param[in] data 与声明的常量块大小一致的字节序列。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus SetComputePushConstants(std::span<const std::byte> data) override;
		/**
		 * @brief 录制一次计算着色器分派。
		 *
		 * @param[in] x X 方向工作组数。
		 * @param[in] y Y 方向工作组数。
		 * @param[in] z Z 方向工作组数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Dispatch(uint32_t x, uint32_t y, uint32_t z) override;
		/**
		 * @brief 使用缓冲区中的参数录制间接索引绘制。
		 *
		 * @param[in] buffer 保存间接绘制参数的缓冲区。
		 * @param[in] offset 参数记录的起始字节偏移。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus DrawIndexedIndirect(BufferHandle buffer, uint64_t offset) override;
		/**
		 * @brief 非索引绘制；管线有顶点输入时检查绑定与读取范围。
		 *
		 * @param[in] vertexCount 每个实例的顶点数。
		 * @param[in] instanceCount 绘制的实例数。
		 * @param[in] firstVertex 起始顶点索引。
		 * @param[in] firstInstance 首个实例的索引。
		 *
		 * @return 返回非索引绘制的录制状态，包括所需管线、缓冲区及读取边界检查。
		 */
		[[nodiscard]] RHIStatus Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
		    uint32_t firstInstance = 0) override;

		/**
		 * @brief 仅在渲染段之外录制；复制不隐式插入访问屏障。
		 *
		 * @param[in] barriers 每个缓冲区的字节范围、前后访问阶段及可选的队列所有权转移。
		 *
		 * @return 返回缓冲区屏障的录制状态；字节范围、用途或同步关系无效时返回错误。
		 */
		[[nodiscard]] RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier> barriers) override;
		/**
		 * @brief 在两个缓冲区的给定字节范围之间录制复制。
		 *
		 * @param[in] source 读取数据的缓冲区。
		 * @param[in] destination 接收数据的缓冲区。
		 * @param[in] copy 源偏移、目标偏移及复制字节数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus CopyBuffer(
		    BufferHandle source, BufferHandle destination, const RHIBufferCopy& copy) override;
		/**
		 * @brief 录制整幅颜色纹理上传；不隐式生成访问屏障。
		 * 在 rendering scope 外复制整幅颜色纹理；访问屏障由调用方提供。
		 *
		 * @param[in] source 提供像素数据的缓冲区。
		 * @param[in] destination 接收像素的纹理视图。
		 * @param[in] copy 缓冲区偏移及目标图像的区域与子资源。
		 *
		 * @return 返回上传复制的录制状态；成功只表示命令已记录。
		 */
		[[nodiscard]] RHIStatus CopyBufferToTexture(
		    BufferHandle source, TextureViewHandle destination, const RHIBufferTextureCopy& copy) override;
		/**
		 * @brief GPU 写入与主机读取之间由图显式插入 HostRead 屏障；这里仅记录指定像素矩形复制。
		 * 在渲染段外复制，不能借此替代显式 TransferSource/HostRead 访问依赖。
		 *
		 * @param[in] source 读取像素的纹理视图。
		 * @param[in] destination 接收像素的缓冲区。
		 * @param[in] copy 纹理区域、子资源索引与目标字节偏移。
		 *
		 * @return 返回纹理读回复制的录制状态；读取主机内存仍须等待复制完成。
		 */
		[[nodiscard]] RHIStatus CopyTextureToBuffer(
		    TextureViewHandle source, BufferHandle destination, const RHITextureBufferCopy& copy) override;
		/**
		 * @brief 当前支持 binding 0 的单个交错顶点流。
		 *
		 * @param[in] binding 顶点流绑定槽位。
		 * @param[in] buffer 保存顶点数据的缓冲区。
		 * @param[in] offset 首个顶点的字节偏移。
		 *
		 * @return 返回顶点缓冲绑定的录制状态，包括绑定位置、缓冲用途及偏移检查。
		 */
		[[nodiscard]] RHIStatus BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset = 0) override;
		/**
		 * @brief 设置索引绘制的数据来源及索引格式。
		 *
		 * @param[in] buffer 保存索引数据的缓冲区。
		 * @param[in] type 单个索引的整数宽度。
		 * @param[in] offset 首个索引的字节偏移。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus BindIndexBuffer(BufferHandle buffer, RHIIndexType type, uint64_t offset = 0) override;
		/**
		 * @brief 检查索引字节范围；索引值及 vertexOffset 对应的顶点范围由调用方保证。
		 *
		 * @param[in] indexCount 每个实例读取的索引数。
		 * @param[in] instanceCount 绘制的实例数。
		 * @param[in] firstIndex 起始索引的位置，以索引为单位。
		 * @param[in] vertexOffset 加到各索引值上的带符号顶点偏移。
		 * @param[in] firstInstance 首个实例的索引。
		 *
		 * @return 返回索引绘制的录制状态，包含索引字节范围检查；索引引用的实际顶点由调用者保证。
		 */
		[[nodiscard]] RHIStatus DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
		    int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

		/**
		 * @brief 仅用于诊断；name 为 UTF-8 终止字符串，color 为 [0, 1] 范围的 RGBA。
		 * 返回 true 才需要配对 End；标签必须在当前命令列表 End 前闭合。
		 *
		 * @param[in] name 以零结尾的 UTF-8 标签名称。
		 * @param[in] color 各分量位于 [0,1] 的 RGBA 标签颜色。
		 *
		 * @return 实际写入开始标签时返回 true，此时须与结束标签配对。
		 */
		[[nodiscard]] bool BeginDebugLabel(const char* name, const std::array<float, 4>& color) override;
		/**
		 * @brief 关闭最近开启的命令诊断标签范围。
		 */
		void EndDebugLabel() override;

	private:
		[[nodiscard]] RHIStatus SetBindings(std::span<const RHIBinding> bindings);
		struct LocalBufferState {
			BufferHandle buffer{};
			RHIBufferStateSnapshot snapshot;
		};
		[[nodiscard]] RHIStatus GetLocalBufferState(BufferHandle buffer, LocalBufferState*& state);
		[[nodiscard]] RHIStatus ApplyOwnership(const RHIQueueTransfer&, const RHIResourceOwnership&,
		    RHIResourceOwnership&, RHITextureLayout oldLayout = RHITextureLayout::Undefined,
		    RHITextureLayout newLayout = RHITextureLayout::Undefined) const;
		bool ProvesOwnership(const RHIResourceOwnership&, const RHIResourceOwnership&) const noexcept;

		[[nodiscard]] RHIStatus RetainTextureView(TextureViewHandle handle);

		void ReleaseTextureViews(size_t first = 0) noexcept;

		[[nodiscard]] RHIStatus RetainSampler(SamplerHandle sampler);

		void ReleaseSamplers(size_t first = 0) noexcept;

		[[nodiscard]] RHIStatus OnPoolReset();

		void Invalidate() { m_State = RHICommandListState::Invalid; }

		void ReleasePipelines() noexcept;

		[[nodiscard]] RHIStatus RetainBuffer(BufferHandle handle);
		void ReleaseBuffers(size_t first = 0) noexcept;

		[[nodiscard]] RHIStatus ValidateResourceStatePlan() const noexcept;

		void CommitResourceStatePlan(RHICompletionPoint completion) noexcept;

		bool ExportsPresent(TextureViewHandle view) const noexcept;

	private:
		/* 帧复用与队列提交负责内部状态变更 */
		friend class VulkanFrameContext;
		friend class VulkanQueue;
		friend class VulkanAuxiliaryContext;

		/* 原生命令缓冲及录制状态 */
		VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
		uint32_t m_QueueFamily = VK_QUEUE_FAMILY_IGNORED;
		/* Invalid 标记命令列表失效；已有资源引用仍须等 GPU 使用结束后释放。 */
		RHICommandListState m_State = RHICommandListState::Uninitialized;

		uint32_t m_DebugLabelDepth = 0;

		/* 借用设备，并保留录制所引用的资源。 */
		VulkanDevice& m_Device;
		VulkanDescriptorAllocator m_Descriptors;
		std::vector<SamplerHandle> m_RetainedSamplers;
		std::vector<std::shared_ptr<RHIQueryPool>> m_RetainedQueries;
		std::vector<TextureViewHandle> m_RetainedViews;
		std::vector<GraphicsPipelineHandle> m_RetainedPipelines;

		/* 通过命令引用保活的管线布局，并记录参数是否准备好 */
		VkPipelineLayout m_BoundPipelineLayout = VK_NULL_HANDLE;
		uint32_t m_PushConstantBytes = 0;
		VkShaderStageFlags m_PushConstantStages = 0;
		bool m_PushConstantsSet = false;

		VkDescriptorSetLayout m_BoundBindingLayout = VK_NULL_HANDLE;
		std::array<RHIBindingLayoutEntry, RHIMaxBindingsPerSet> m_BoundBindings{};
		size_t m_BoundBindingCount = 0;
		bool m_GraphicsBindingsSet = false;
		bool m_BoundCompute = false;
		std::vector<RHIBinding> m_StorageBindings;

		/* 当前管线、渲染附件与动态状态 */
		PixelFormat m_BoundColorFormat = PixelFormat::Unknown;
		PixelFormat m_RenderingColorFormat = PixelFormat::Unknown;
		PixelFormat m_BoundDepthFormat = PixelFormat::Unknown;
		PixelFormat m_RenderingDepthFormat = PixelFormat::Unknown;
		bool m_BoundDepthWrite = false;
		bool m_RenderingDepthReadOnly = false;
		bool m_ViewportAndScissorSet = false;

		std::vector<BufferHandle> m_RetainedBuffers;
		/* 命令局部访问/初始化账本，不推导依赖、不发布全局已接受状态。 */
		std::vector<LocalBufferState> m_LocalBufferStates;
		struct LocalTextureOwnership {
			TextureViewHandle view;
			RHIResourceOwnership ownership;
		};
		std::vector<LocalTextureOwnership> m_LocalTextureOwnership;
		std::vector<RHICompletionPoint> m_RequiredGpuDependencies;

		std::vector<RHIBufferStateUpdate> m_BufferStateUpdates;
		std::vector<RHITextureStateUpdate> m_TextureStateUpdates;
		bool m_ResourceStatePlanBound = false;
		bool m_ResourceStatePlanCommitted = false;

		uint32_t m_VertexStride = 0;
		uint32_t m_VertexAttributeEnd = 0;
		uint64_t m_VertexBufferBytes = 0;
		uint64_t m_IndexBufferBytes = 0;
		uint32_t m_IndexSize = 0;
		bool m_VertexBufferBound = false;
		bool m_IndexBufferBound = false;
	};

}
