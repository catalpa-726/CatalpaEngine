#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHIRenderingDesc.h"
#include "RHICommandTypes.h"
#include "RHIResourceState.h"
#include "RHIBindingDesc.h"
#include "RHIQueryDesc.h"

/* 提供不包含 Vulkan 类型的命令录制接口 */
namespace Catalpa {

	enum class RHICommandListState { Uninitialized, Initial, Recording, Rendering, Executable, Pending, Invalid };

	class CATALPA_API RHICommandList {
	public:
		virtual ~RHICommandList() = default;

		/**
		 * @brief 开始录制命令列表。
		 *
		 * @return 返回进入录制状态的结果；未满足复用条件的命令列表不能开始录制。
		 */
		[[nodiscard]] virtual RHIStatus Begin() = 0;
		/**
		 * @brief 结束命令录制，使命令列表可提交。
		 *
		 * @return 返回结束录制的状态；仍有未闭合的渲染段时不能提交。
		 */
		[[nodiscard]] virtual RHIStatus End() = 0;

		[[nodiscard]] virtual RHIStatus ResetQueries(const std::shared_ptr<RHIQueryPool>&, uint32_t, uint32_t) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::ResetQueries"};
		}
		[[nodiscard]] virtual RHIStatus WriteTimestamp(const std::shared_ptr<RHIQueryPool>&, uint32_t, bool) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::WriteTimestamp"};
		}

		/**
		 * @brief 在开始录制后、首个资源命令之前安装本次录制的资源更新计划。
		 *
		 * @param[in] buffers 缓冲区的预期版本、访问与初始化范围。
		 * @param[in] textures 纹理视图及其父图像的预期状态。
		 *
		 * @return 返回安装资源更新计划的状态；失败的计划不可提交。
		 */
		[[nodiscard]] virtual RHIStatus SetResourceStatePlan(
		    std::span<const RHIBufferStateUpdate> buffers, std::span<const RHITextureStateUpdate> textures) = 0;

		/**
		 * @brief 以指定附件和加载、存储策略开始渲染段。
		 *
		 * @param[in] desc 颜色与深度附件、装载操作及渲染区域。
		 *
		 * @return 返回开始动态渲染的状态；附件或命令列表状态不满足要求时返回错误。
		 */
		[[nodiscard]] virtual RHIStatus BeginRendering(const RHIRenderingDesc& desc) = 0;

		/**
		 * @brief 结束当前的渲染段，回到普通命令录制状态。
		 *
		 * @return 返回结束渲染段的状态；当前不在渲染段内时返回错误。
		 */
		[[nodiscard]] virtual RHIStatus EndRendering() = 0;

		virtual RHICommandListState GetState() const = 0;

		/**
		 * @brief 设置后续绘制的视口与裁剪矩形。
		 *
		 * @param[in] viewport 视口坐标、尺寸及深度范围。
		 * @param[in] scissor 帧缓冲像素坐标下的裁剪范围。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor) = 0;

		/**
		 * @brief 在渲染段外记录纹理访问、布局及队列所有权的显式转换。
		 *
		 * @param[in] barriers 本次录制的图像访问、布局及所有权转换，同一图像不得在该批次重复出现。
		 *
		 * @return 返回图像屏障录制状态；布局、归属或子资源范围无效时返回错误。
		 */
		[[nodiscard]] virtual RHIStatus PipelineBarrier(std::span<const RHITextureBarrier> barriers) = 0;

		/**
		 * @brief 绑定图形管线并保留录制命令对其资源的引用。
		 *
		 * @param[in] pipeline 当前设备创建的图形管线句柄。
		 *
		 * @return 返回绑定图形管线的录制状态，含句柄及录制阶段检查结果。
		 */
		[[nodiscard]] virtual RHIStatus BindGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;

		[[nodiscard]] virtual RHIStatus BindComputePipeline(ComputePipelineHandle) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::BindComputePipeline"};
		}
		[[nodiscard]] virtual RHIStatus SetComputeBindings(std::span<const RHIBinding>) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::SetComputeBindings"};
		}
		[[nodiscard]] virtual RHIStatus SetComputePushConstants(std::span<const std::byte>) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::SetComputePushConstants"};
		}
		[[nodiscard]] virtual RHIStatus Dispatch(uint32_t, uint32_t, uint32_t) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::Dispatch"};
		}
		[[nodiscard]] virtual RHIStatus DrawIndexedIndirect(BufferHandle, uint64_t) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::DrawIndexedIndirect"};
		}

		/**
		 * @brief 向当前图形管线写入完整的推送常量块。
		 *
		 * @param[in] data 从偏移 0 写入的完整推送常量字节，长度须与当前图形管线的声明一致。
		 *
		 * @return 返回推送常量写入状态；当前管线与参数大小不匹配时返回错误。
		 */
		[[nodiscard]] virtual RHIStatus SetGraphicsPushConstants(std::span<const std::byte> data) = 0;

		/**
		 * @brief 为当前图形管线绑定第 0 组的全部资源。
		 *
		 * @param[in] bindings 当前管线第 0 组的全部资源绑定，编号及类型须与布局一致。
		 *
		 * @return 返回本次描述符绑定的状态，反映布局核对、资源引用及描述符集分配结果。
		 */
		[[nodiscard]] virtual RHIStatus SetGraphicsBindings(std::span<const RHIBinding> bindings) = 0;

		/**
		 * @brief 验证顶点读取范围并录制非索引绘制。
		 *
		 * @param[in] vertexCount 每个实例的顶点数。
		 * @param[in] instanceCount 绘制的实例数。
		 * @param[in] firstVertex 起始顶点索引。
		 * @param[in] firstInstance 首个实例的索引。
		 *
		 * @return 返回非索引绘制的录制状态，包括所需管线、缓冲区及读取边界检查。
		 */
		[[nodiscard]] virtual RHIStatus Draw(
		    uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

		/**
		 * @brief 在渲染段外记录缓冲区指定字节范围的访问及队列所有权转换。
		 *
		 * @param[in] barriers 每个缓冲区的字节范围、前后访问阶段及可选的队列所有权转移。
		 *
		 * @return 返回缓冲区屏障的录制状态；字节范围、用途或同步关系无效时返回错误。
		 */
		[[nodiscard]] virtual RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier> barriers) = 0;
		/**
		 * @brief 在两个缓冲区的给定字节范围之间录制复制。
		 *
		 * @param[in] source 读取数据的缓冲区。
		 * @param[in] destination 接收数据的缓冲区。
		 * @param[in] copy 源偏移、目标偏移及复制字节数。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus CopyBuffer(
		    BufferHandle source, BufferHandle destination, const RHIBufferCopy& copy) = 0;

		/**
		 * @brief 录制从缓冲区向指定纹理子资源的复制，访问屏障由调用方提供。
		 *
		 * @param[in] source 提供像素数据的缓冲区。
		 * @param[in] destination 接收像素的纹理视图。
		 * @param[in] copy 缓冲区偏移及目标图像的区域与子资源。
		 *
		 * @return 返回上传复制的录制状态；成功只表示命令已记录。
		 */
		[[nodiscard]] virtual RHIStatus CopyBufferToTexture(
		    BufferHandle source, TextureViewHandle destination, const RHIBufferTextureCopy& copy) = 0;

		/**
		 * @brief 录制从纹理指定矩形向缓冲区的复制，调用方负责主机读回同步。
		 * @details 第 1 个参数（TextureViewHandle）为输入。读取像素的纹理视图。
		 * @details 第 2 个参数（BufferHandle）为输入。接收像素的缓冲区。
		 * @details 第 3 个参数（const RHITextureBufferCopy&）为输入。纹理区域、子资源索引与目标字节偏移。
		 *
		 * @return 返回纹理读回复制的录制状态；读取主机内存仍须等待复制完成。
		 */
		[[nodiscard]] virtual RHIStatus CopyTextureToBuffer(
		    TextureViewHandle, BufferHandle, const RHITextureBufferCopy&) {
			return {.code = RHIErrorCode::Unsupported, .operation = "RHICommandList::CopyTextureToBuffer"};
		}

		/**
		 * @brief 指定当前顶点流的缓冲区及其起始字节偏移。
		 *
		 * @param[in] binding 顶点流绑定槽位。
		 * @param[in] buffer 保存顶点数据的缓冲区。
		 * @param[in] offset 首个顶点的字节偏移。
		 *
		 * @return 返回顶点缓冲绑定的录制状态，包括绑定位置、缓冲用途及偏移检查。
		 */
		[[nodiscard]] virtual RHIStatus BindVertexBuffer(
		    uint32_t binding, BufferHandle buffer, uint64_t offset = 0) = 0;
		/**
		 * @brief 设置索引绘制的数据来源及索引格式。
		 *
		 * @param[in] buffer 保存索引数据的缓冲区。
		 * @param[in] type 单个索引的整数宽度。
		 * @param[in] offset 首个索引的字节偏移。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] virtual RHIStatus BindIndexBuffer(
		    BufferHandle buffer, RHIIndexType type, uint64_t offset = 0) = 0;

		/**
		 * @brief 验证索引缓冲范围并录制索引绘制。
		 *
		 * @param[in] indexCount 每个实例读取的索引数。
		 * @param[in] instanceCount 绘制的实例数。
		 * @param[in] firstIndex 起始索引的位置，以索引为单位。
		 * @param[in] vertexOffset 加到各索引值上的带符号顶点偏移。
		 * @param[in] firstInstance 首个实例的索引。
		 *
		 * @return 返回索引绘制的录制状态，包含索引字节范围检查；索引引用的实际顶点由调用者保证。
		 */
		[[nodiscard]] virtual RHIStatus DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
		    uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

		/**
		 * @brief 开始一个诊断标签范围，成功时须配对结束。
		 *
		 * @param[in] name 以零结尾的 UTF-8 标签名称。
		 * @param[in] color 各分量位于 [0,1] 的 RGBA 标签颜色。
		 *
		 * @return 布尔结果，true 表示上述操作或判断成立。
		 */
		[[nodiscard]] virtual bool BeginDebugLabel(const char* name, const std::array<float, 4>& color) = 0;
		/**
		 * @brief 关闭最近开启的命令诊断标签范围。
		 */
		virtual void EndDebugLabel() = 0;
	};

	/* 这个类为一段命令自动配对标签，提前返回时也会关闭 */
	class RHICommandLabelScope {
	public:
		RHICommandLabelScope(RHICommandList& commands, const char* name, const std::array<float, 4>& color)
		    : m_Commands(commands), m_Active(commands.BeginDebugLabel(name, color)) {}

		~RHICommandLabelScope() { Close(); }

		RHICommandLabelScope(const RHICommandLabelScope&) = delete;
		RHICommandLabelScope& operator=(const RHICommandLabelScope&) = delete;
		RHICommandLabelScope(RHICommandLabelScope&&) = delete;
		RHICommandLabelScope& operator=(RHICommandLabelScope&&) = delete;

		void Close() {
			if (std::exchange(m_Active, false))
				m_Commands.EndDebugLabel();
		}

	private:
		RHICommandList& m_Commands;
		/* 只有 BeginDebugLabel 成功才配对 EndDebugLabel，标签不得跨越命令列表结束。 */
		bool m_Active = false;
	};

}
