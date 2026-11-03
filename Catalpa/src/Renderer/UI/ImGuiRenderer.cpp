#include "catpch.h"
#include "ImGuiRenderer.h"
#include "ImGuiFrameData.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "RHI/RHIDevice.h"
#include "ImGuiVertexSpirv.h"
#include "ImGuiFragmentSpirv.h"

namespace Catalpa {

    namespace {
        // transform.xy = 2 / DisplaySize；zw = -1 - DisplayPos * xy。
        // Vulkan 正高度 viewport 的左上角对应 NDC (-1,-1)，无需额外翻转 Y。
        struct alignas(16) ImGuiParameters { std::array<float, 4> transform; };
        static_assert(sizeof(ImGuiParameters) == 16);
        static_assert(sizeof(ImGuiVertex) == 32);
        static_assert(offsetof(ImGuiVertex, position) == 0);
        static_assert(offsetof(ImGuiVertex, uv) == 8);
        static_assert(offsetof(ImGuiVertex, color) == 16);

        constexpr std::array<VertexAttribute, 3> Attributes{{
            {.location = 0, .format = VertexFormat::Float2, .offset = 0},
            {.location = 1, .format = VertexFormat::Float2, .offset = 8},
            {.location = 2, .format = VertexFormat::Float4, .offset = 16}
        }};
        constexpr std::array<RHIBindingLayoutEntry, 2> Bindings{{
            {.binding = 0, .type = RHIBindingType::SampledTexture, .visibility = RHIShaderVisibility::Fragment},
            {.binding = 1, .type = RHIBindingType::Sampler, .visibility = RHIShaderVisibility::Fragment}
        }};
    }

    ImGuiRenderer::ImGuiRenderer(RHIDevice& device) : m_Device(device), m_Frames(device.GetFrameCount()) {}

    ImGuiRenderer::~ImGuiRenderer()
    {
        for (const auto& frame : m_Frames) {
            m_Device.ReleaseBuffer(frame.vertices);
            m_Device.ReleaseBuffer(frame.indices);
        }
        m_Device.ReleaseGraphicsPipeline(m_Pipeline);
        m_Device.ReleaseSampler(m_Sampler);
        m_Device.ReleaseTextureView(m_FontView);
        m_Device.ReleaseTexture(m_Font);
    }

    RHIStatus ImGuiRenderer::InitFont(UploadScheduler& uploads, std::span<const std::byte> pixels, Extent2D extent)
    {
        if (m_Font.HasValue()) return { .code = RHIErrorCode::InvalidState, .operation = "ImGuiRenderer::InitFont" };
        const RHISamplerDesc sampler{
            .minFilter = RHIFilter::Linear, .magFilter = RHIFilter::Linear,
            .addressU = RHISamplerAddressMode::ClampToEdge, .addressV = RHISamplerAddressMode::ClampToEdge
        };
        if (!m_Sampler.HasValue()) {
            if (const RHIStatus status = m_Device.CreateSampler(sampler, m_Sampler); !status.IsSuccess()) return status;
        }
        const RHITextureDesc desc{ .extent = extent, .format = PixelFormat::RGBA8_UNorm, .usage = RHITextureUsage::Sampled };
        return uploads.CreateTexture(pixels, desc, m_Font, m_FontView);
    }

    RHIStatus ImGuiRenderer::EnsurePipeline(PixelFormat format)
    {
        if (m_Pipeline.HasValue() && m_Format == format) return {};
        // UI 颜色在 shader 中解码，附件负责唯一一次 sRGB 编码与线性混合。
        if (!GetPixelFormatInfo(format).isSRGB)
            return { .code = RHIErrorCode::Unsupported, .operation = "ImGuiRenderer::SRGBAttachmentRequired" };
        const RHIGraphicsPipelineDesc desc{
            .vertex = {.bytecode = ImGuiVertexSpirv, .stage = RHIShaderStage::Vertex, .entryPoint = "vertexMain"},
            .fragment = {.bytecode = ImGuiFragmentSpirv, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain"},
            .colorFormat = format,
            .blendMode = RHIBlendMode::StraightAlpha,
            .vertexLayout = {.stride = sizeof(ImGuiVertex), .attributes = Attributes},
            .pushConstants = {.size = sizeof(ImGuiParameters), .visibility = RHIShaderVisibility::Vertex},
            .bindingLayout = {.entries = Bindings}
        };
        GraphicsPipelineHandle pipeline{};
        if (const RHIStatus status = m_Device.CreateGraphicsPipeline(desc, pipeline); !status.IsSuccess()) return status;
        m_Device.ReleaseGraphicsPipeline(std::exchange(m_Pipeline, pipeline));
        m_Format = format;
        return {};
    }

    RHIStatus ImGuiRenderer::WriteDynamicBuffer(BufferHandle& buffer, uint64_t& capacity,
        RHIBufferUsage usage, std::span<const std::byte> bytes)
    {
        if (bytes.empty()) return {};
        if (capacity < bytes.size()) {
            const RHIBufferDesc desc{ .size = bytes.size(), .usage = usage, .memory = RHIMemoryDomain::Upload };
            BufferHandle replacement{};
            if (const RHIStatus status = m_Device.CreateBuffer(desc, replacement); !status.IsSuccess()) return status;
            const RHIStatus status = m_Device.WriteBuffer(replacement, 0, bytes);
            if (!status.IsSuccess()) { m_Device.ReleaseBuffer(replacement); return status; }
            m_Device.ReleaseBuffer(std::exchange(buffer, replacement));
            capacity = bytes.size();
            return {};
        }
        // 余下容量已在初次创建时初始化；本帧索引仅引用当前有效顶点。
        return m_Device.WriteBuffer(buffer, 0, bytes);
    }

    RHIStatus ImGuiRenderer::Prepare(uint32_t slot, const ImGuiFrameData& frame, PixelFormat format)
    {
        if (frame.draws.empty()) return {};
        if (slot >= m_Frames.size() || !m_FontView.HasValue() || frame.vertices.empty() || frame.indices.empty())
            return { .code = RHIErrorCode::InvalidState, .operation = "ImGuiRenderer::Prepare" };
        if (const RHIStatus status = EnsurePipeline(format); !status.IsSuccess()) return status;
        auto& buffers = m_Frames[slot];
        if (const RHIStatus status = WriteDynamicBuffer(buffers.vertices, buffers.vertexCapacity,
            RHIBufferUsage::Vertex, std::as_bytes(std::span{frame.vertices})); !status.IsSuccess()) return status;
        return WriteDynamicBuffer(buffers.indices, buffers.indexCapacity,
            RHIBufferUsage::Index, std::as_bytes(std::span{frame.indices}));
    }

    RHIStatus ImGuiRenderer::AddPasses(RenderGraph& graph, RGTexture color, Extent2D extent,
        uint32_t slot, const ImGuiFrameData& frame)
    {
        if (frame.draws.empty() || extent.IsEmpty()) return {};
        constexpr const char* operation = "ImGuiRenderer::AddPasses";
        if (slot >= m_Frames.size() || extent.width > INT32_MAX || extent.height > INT32_MAX)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        for (int axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(frame.displayPos[axis]) || !std::isfinite(frame.displaySize[axis]) ||
                !std::isfinite(frame.framebufferScale[axis]) || frame.displaySize[axis] <= 0 || frame.framebufferScale[axis] <= 0)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        }

        RGBuffer vertices{}, indices{};
        RGTexture font{};
        if (const RHIStatus status = graph.ImportBuffer(m_Frames[slot].vertices, vertices); !status.IsSuccess()) return status;
        if (const RHIStatus status = graph.ImportBuffer(m_Frames[slot].indices, indices); !status.IsSuccess()) return status;
        if (const RHIStatus status = graph.ImportTexture(m_FontView, font); !status.IsSuccess()) return status;

        ImGuiParameters parameters{};
        for (int axis = 0; axis < 2; ++axis) {
            parameters.transform[axis] = 2.0f / frame.displaySize[axis];
            parameters.transform[axis + 2] = -1.0f - frame.displayPos[axis] * parameters.transform[axis];
        }
        RGIndexedDrawDesc draw{
            .vertices = vertices, .indices = indices, .color = color, .pipeline = m_Pipeline,
            .indexType = RHIIndexType::UInt32,
            .loadOp = RHILoadOp::Load, .storeOp = RHIStoreOp::Store,
            .viewport = {.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)}
        };
        const auto bytes = std::as_bytes(std::span{&parameters, 1});
        draw.pushConstants.assign(bytes.begin(), bytes.end());
        draw.sampledTextures.push_back({.texture = font, .sampler = m_Sampler});

        for (const auto& command : frame.draws) {
            if (command.indexCount == 0) continue;
            if (command.firstIndex > frame.indices.size() || command.indexCount > frame.indices.size() - command.firstIndex || command.baseVertex < 0)
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            // 检查实际索引，不能用缓冲容量充当有效顶点数量。
            for (uint64_t i = command.firstIndex; i < uint64_t(command.firstIndex) + command.indexCount; ++i) {
                if (uint64_t(frame.indices[static_cast<size_t>(i)]) + command.baseVertex >= frame.vertices.size())
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            }
            // 以 double 夹取，避免大整数边界先舍入到 float 后超出 int32 范围。
            double clip[4]{};
            for (int i = 0; i < 4; ++i) {
                const int axis = i % 2;
                const double value = (static_cast<double>(command.clipRect[i]) - frame.displayPos[axis]) * frame.framebufferScale[axis];
                if (!std::isfinite(value)) return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                clip[i] = std::clamp(value, 0.0, static_cast<double>(axis == 0 ? extent.width : extent.height));
            }
            if (clip[2] <= clip[0] || clip[3] <= clip[1]) continue;
            const int32_t x = static_cast<int32_t>(std::floor(clip[0]));
            const int32_t y = static_cast<int32_t>(std::floor(clip[1]));
            draw.scissor = {.offset = {x, y}, .extent = {
                static_cast<uint32_t>(std::ceil(clip[2])) - static_cast<uint32_t>(x),
                static_cast<uint32_t>(std::ceil(clip[3])) - static_cast<uint32_t>(y)}};
            draw.firstIndex = command.firstIndex;
            draw.indexCount = command.indexCount;
            draw.baseVertex = command.baseVertex;
            if (const RHIStatus status = graph.AddDrawPass("ImGui", draw); !status.IsSuccess()) return status;
        }
        return {};
    }

}
