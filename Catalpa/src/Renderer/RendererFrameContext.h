#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHITypes.h"
#include "Images/PixelFormat.h"
#include "Renderer/RenderFramePacket.h"
#include "Renderer/Visibility/VisibilitySystem.h"

namespace Catalpa {
    class RHIDevice;
    struct ShaderMetadata;

    // 各独立视图共用的显式表现配置；正式画质分档在后续扩展同一入口。
    struct RendererProfile {
        PixelFormat depthFormat = PixelFormat::D32_Float;
        bool depthPrepass = true;
        bool normalDebug = false;
        std::array<float, 4> clearColor{ 0.02f, 0.02f, 0.025f, 1.0f };
        bool asyncQueues = true; // false保留同一状态/完成协议，全部批次交给graphics串行执行。
    };

    // 严格校验实际部署JSON；失败保留output，不读取cwd或创建GPU对象。
    [[nodiscard]] CATALPA_API RHIStatus ParseRendererProfile(std::string_view json, RendererProfile& output);

    // GPU ABI由float4列显式定义；与Common/Bindings.slang逐字段对应，不直接上传GLM对象。
    struct alignas(16) FrameGpuParameters {
        std::array<float, 4> time{};    // simulation%4096, realDelta, fixedDelta, interpolationAlpha，秒。
        std::array<float, 4> options{}; // x=normalDebug，y=Blend拾取alpha阈值，z=4096秒epoch，w保留。
    };
    struct alignas(16) ViewGpuParameters {
        std::array<std::array<float, 4>, 4> viewProjectionColumns{};
        std::array<float, 4> cameraPosition{};
        std::array<std::array<float, 4>, 4> inverseViewProjectionColumns{};
        std::array<std::array<float, 4>, 4> viewColumns{};
    };
    struct alignas(16) DrawGpuParameters {
        std::array<std::array<float, 4>, 4> worldColumns{};
        std::array<std::array<float, 4>, 3> normalColumns{};
        std::array<float, 4> color{};
        std::array<uint32_t, 4> ids{}; // x为一基实例token，y为镜像手性bit；0 token专属背景。
    };

    static_assert(sizeof(float) == 4);
    static_assert(sizeof(FrameGpuParameters) == 32 && offsetof(FrameGpuParameters, options) == 16);
    static_assert(sizeof(ViewGpuParameters) == 208 && offsetof(ViewGpuParameters, cameraPosition) == 64
        && offsetof(ViewGpuParameters, inverseViewProjectionColumns) == 80 && offsetof(ViewGpuParameters, viewColumns) == 144);
    static_assert(sizeof(DrawGpuParameters) == 144 && offsetof(DrawGpuParameters, normalColumns) == 64
        && offsetof(DrawGpuParameters, color) == 112 && offsetof(DrawGpuParameters, ids) == 128);
    static_assert(std::is_trivially_copyable_v<FrameGpuParameters> && std::is_trivially_copyable_v<ViewGpuParameters>
        && std::is_trivially_copyable_v<DrawGpuParameters>);

    // 纯CPU核对forward shader的Frame(FS)/View(VS)/Draw(VS+FS)与实际host packing。
    // 接受直接float4数组或Slang精确的单data成员数组包装；保留原始metadata并严格核对两层布局。
    [[nodiscard]] CATALPA_API bool ValidateFrameShaderInterface(const ShaderMetadata& metadata,
        std::string_view vertexEntry, std::string_view fragmentEntry, std::string& error);

    // data包含已清零的对齐间隙；每个binding使用自己的精确range，不把整个buffer误作一个UBO。
    struct PackedFrameUniforms {
        std::vector<std::byte> data;
        // 与packet.views逐项对应；Frame和共享Draw只保存一份，各view拥有精确独立range。
        std::vector<uint64_t> viewOffsets;
        std::vector<uint64_t> drawOffsets;
    };

    // 按实际展示目标尺寸重建投影/逆与最终VP；不回写Scene或来源视图，Y翻转仍仅在投影一次。
    [[nodiscard]] CATALPA_API RHIStatus BuildPresentationView(const RenderView& source, Extent2D extent, RenderView& output) noexcept;

    // 纯CPU packing：输入须已由RenderScene验证身份/几何，本函数另验上传数值、布局与范围。
    // alignment/maxRange来自设备保存的能力，不进行硬件探测；失败保留output。
    [[nodiscard]] CATALPA_API RHIStatus BuildFrameUniformData(const RenderFramePacket& packet,
        const RendererProfile& profile, uint64_t alignment, uint64_t maxRange, PackedFrameUniforms& output);

    // 每帧槽独占一个Upload UBO；调用者必须先成功PrepareFrame(slot)再覆写，图导入发生在写入之后。
    class RendererFrameContext {
    public:
        explicit RendererFrameContext(RHIDevice& device) : m_Device(device) {}
        ~RendererFrameContext();
        RendererFrameContext(const RendererFrameContext&) = delete;
        RendererFrameContext& operator=(const RendererFrameContext&) = delete;

        [[nodiscard]] RHIStatus Prepare(uint32_t slot, const RenderFramePacket& packet, const RendererProfile& profile, Extent2D targetExtent);
        [[nodiscard]] BufferHandle GetBuffer(uint32_t slot) const noexcept;
        [[nodiscard]] const PackedFrameUniforms* GetPacking(uint32_t slot) const noexcept;
        // 借用该帧槽已准备的实际目标相机/可见清单；不存在的槽或view返回nullptr。
        [[nodiscard]] const RenderView* GetView(uint32_t slot, size_t index = 0) const noexcept;
        [[nodiscard]] const VisibilityResult* GetVisibility(uint32_t slot, size_t index = 0) const noexcept;
        // 历史只在整批图命令被队列接受后推进；获取/录制/提交失败均丢弃pending。
        void OnSubmitted() noexcept { m_ViewHistory.OnSubmitted(); }
        void OnCancelled() noexcept { m_ViewHistory.OnCancelled(); }
        void ClearHistory() noexcept { m_ViewHistory.Clear(); }
    private:
        struct Slot {
            BufferHandle buffer{};
            uint64_t capacity = 0;
            PackedFrameUniforms packing;
            std::vector<RenderView> views;
            std::vector<VisibilityResult> visibility;
        };
        RHIDevice& m_Device;
        std::vector<Slot> m_Slots;
        RenderViewHistory m_ViewHistory;
    };
}
