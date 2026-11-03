#pragma once

#include "Core/Base.h"
#include "RHIError.h"
#include "RHITypes.h"
#include "RHIRenderingDesc.h"
#include "RHICommandTypes.h"
#include "RHIResourceState.h"

// 提供不包含 Vulkan 类型的命令录制接口
namespace Catalpa {

    enum class RHICommandListState { Uninitialized, Initial, Recording, Rendering, Executable, Pending, Invalid };

    class CATALPA_API RHICommandList {
    public:
        virtual ~RHICommandList() = default;

        // 命令列表录制边界。
        [[nodiscard]] virtual RHIStatus Begin() = 0;
        [[nodiscard]] virtual RHIStatus End() = 0;

        // Begin 后、首个资源命令前设置一次；实现持有更新副本及资源引用。
        // 访问声明须与实际录制一致，设置失败后不得继续提交。
        [[nodiscard]] virtual RHIStatus SetResourceStatePlan(
            std::span<const RHIBufferStateUpdate> buffers,
            std::span<const RHITextureStateUpdate> textures) = 0;

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

        // 写入当前图形管线的完整参数块，偏移固定为 0。
        // 每次成功绑定含参数块的管线后，必须重新写入。
        // 数据只在本次调用期间借用，字节数须与管线声明完全一致。
        [[nodiscard]] virtual RHIStatus SetGraphicsPushConstants(std::span<const std::byte> data) = 0;

        // 非索引绘制；管线有顶点输入时检查绑定与读取范围。
        [[nodiscard]] virtual RHIStatus Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

        // 仅在渲染段之外录制；复制不隐式插入访问屏障。
        [[nodiscard]] virtual RHIStatus PipelineBarrier(std::span<const RHIBufferBarrier> barriers) = 0;
        [[nodiscard]] virtual RHIStatus CopyBuffer(BufferHandle source, BufferHandle destination, const RHIBufferCopy& copy) = 0;

        // 在 rendering scope 外复制整幅颜色纹理；访问屏障由调用方提供。
        [[nodiscard]] virtual RHIStatus CopyBufferToTexture(BufferHandle source, TextureViewHandle destination, const RHIBufferTextureCopy& copy) = 0;

        // 当前支持 binding 0 的单个交错顶点流。
        [[nodiscard]] virtual RHIStatus BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset = 0) = 0;
        [[nodiscard]] virtual RHIStatus BindIndexBuffer(BufferHandle buffer, RHIIndexType type, uint64_t offset = 0) = 0;

        // 检查索引字节范围；索引值及 vertexOffset 对应的顶点范围由调用方保证。
        [[nodiscard]] virtual RHIStatus DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

        // 仅用于诊断；name 为 UTF-8 终止字符串，color 为 [0, 1] 范围的 RGBA。
        // 返回 true 才需要配对 End；标签必须在当前命令列表 End 前闭合。
        [[nodiscard]] virtual bool BeginDebugLabel(const char* name, const std::array<float, 4>& color) = 0;
        virtual void EndDebugLabel() = 0;

    };

    // 这个类为一段命令自动配对标签，提前返回时也会关闭
    class RHICommandLabelScope {
    public:
        // 只在后端实际开始标签后，承担关闭责任。
        RHICommandLabelScope(RHICommandList& commands, const char* name, const std::array<float, 4>& color)
            : m_Commands(commands), m_Active(commands.BeginDebugLabel(name, color)) {
        }

        ~RHICommandLabelScope() { Close(); }

        RHICommandLabelScope(const RHICommandLabelScope&) = delete;
        RHICommandLabelScope& operator=(const RHICommandLabelScope&) = delete;
        RHICommandLabelScope(RHICommandLabelScope&&) = delete;
        RHICommandLabelScope& operator=(RHICommandLabelScope&&) = delete;

        // 提前关闭当前标签，确保不会跨过 commands.End()。
        void Close() { if (std::exchange(m_Active, false)) m_Commands.EndDebugLabel(); }

    private:
        RHICommandList& m_Commands;
        bool m_Active = false;
    };

}