#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHIRenderingDesc.h"
#include "RHICommandTypes.h"

// 提供不包含 Vulkan 类型的命令录制接口
namespace Catalpa {

    enum class RHICommandListState { Uninitialized, Initial, Recording, Rendering, Executable, Pending, Invalid };

    class CATALPA_API RHICommandList {
    public:
        virtual ~RHICommandList() = default;

        // 命令列表录制边界。
        [[nodiscard]] virtual RHIStatus Begin() = 0;
        [[nodiscard]] virtual RHIStatus End() = 0;

        // 当前支持二维、单层、单采样颜色附件。
        // 附件执行时须已处于颜色附件状态；本函数不执行布局转换。
        [[nodiscard]] virtual RHIStatus BeginRendering(const RHIRenderingDesc& desc) = 0;

        [[nodiscard]] virtual RHIStatus EndRendering() = 0;

        virtual RHICommandListState GetState() const = 0;

        [[nodiscard]] virtual RHIStatus SetViewportAndScissor(const RHIViewport& viewport, const Rect2D& scissor) = 0;

        // 仅在渲染段之外调用；同一次 PipelineBarrier 调用中每张图像最多出现一次。
        // 录制不代表转换已经执行，调用者负责正确的状态基线与提交顺序。
        [[nodiscard]] virtual RHIStatus PipelineBarrier(std::span<const RHITextureBarrier> barriers) = 0;

        // 录制管线绑定，并保留其命令引用。
        [[nodiscard]] virtual RHIStatus BindGraphicsPipeline(GraphicsPipelineHandle pipeline) = 0;

        // 当前支持无顶点缓冲的非索引绘制。
        [[nodiscard]] virtual RHIStatus Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

        // 仅在渲染段之外录制；复制不隐式插入访问屏障。
        [[nodiscard]] virtual RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier> barriers) = 0;
        [[nodiscard]] virtual RHIStatus CopyBuffer(BufferHandle source, BufferHandle destination, const RHIBufferCopy& copy) = 0;

        // 当前支持 binding 0 的单个交错顶点流。
        [[nodiscard]] virtual RHIStatus BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset = 0) = 0;
        [[nodiscard]] virtual RHIStatus BindIndexBuffer(BufferHandle buffer, RHIIndexType type, uint64_t offset = 0) = 0;

        // 检查索引字节范围；索引值及 vertexOffset 对应的顶点范围由调用方保证。
        [[nodiscard]] virtual RHIStatus DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

    };

}