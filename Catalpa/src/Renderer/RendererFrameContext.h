#pragma once

#include "catpch.h"
#include "Core/Base.h"
#include "RHI/RHIError.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHITypes.h"
#include "Images/PixelFormat.h"
#include "Renderer/RenderFramePacket.h"

namespace Catalpa {
    class RHIDevice;

    // 当前单视图路径的显式配置；正式画质分档和能力回退在后续扩展同一配置入口。
    struct RendererProfile {
        PixelFormat depthFormat = PixelFormat::D32_Float;
        bool depthPrepass = true;
        bool normalDebug = false;
        std::array<float, 4> clearColor{ 0.02f, 0.02f, 0.025f, 1.0f };
    };

    // 严格校验实际部署JSON；失败保留output，不读取cwd或创建GPU对象。
    [[nodiscard]] CATALPA_API RHIStatus ParseRendererProfile(std::string_view json, RendererProfile& output);

    // GPU ABI由float4列显式定义；与Common/Bindings.slang逐字段对应，不直接上传GLM对象。
    struct alignas(16) FrameGpuParameters {
        std::array<float, 4> time{};    // simulation, realDelta, fixedDelta, interpolationAlpha，秒。
        std::array<float, 4> options{}; // x=normalDebug，其余保留并清零。
    };
    struct alignas(16) ViewGpuParameters {
        std::array<std::array<float, 4>, 4> viewProjectionColumns{};
        std::array<float, 4> cameraPosition{};
    };
    struct alignas(16) DrawGpuParameters {
        std::array<std::array<float, 4>, 4> worldColumns{};
        std::array<std::array<float, 4>, 3> normalColumns{};
        std::array<float, 4> color{};
    };

    static_assert(sizeof(float) == 4);
    static_assert(sizeof(FrameGpuParameters) == 32 && offsetof(FrameGpuParameters, options) == 16);
    static_assert(sizeof(ViewGpuParameters) == 80 && offsetof(ViewGpuParameters, cameraPosition) == 64);
    static_assert(sizeof(DrawGpuParameters) == 128 && offsetof(DrawGpuParameters, normalColumns) == 64
        && offsetof(DrawGpuParameters, color) == 112);
    static_assert(std::is_trivially_copyable_v<FrameGpuParameters> && std::is_trivially_copyable_v<ViewGpuParameters>
        && std::is_trivially_copyable_v<DrawGpuParameters>);

    // data包含已清零的对齐间隙；每个binding使用自己的精确range，不把整个buffer误作一个UBO。
    struct PackedFrameUniforms {
        std::vector<std::byte> data;
        uint64_t viewOffset = 0;
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
    private:
        struct Slot {
            BufferHandle buffer{};
            uint64_t capacity = 0;
            PackedFrameUniforms packing;
        };
        RHIDevice& m_Device;
        std::vector<Slot> m_Slots;
    };
}
