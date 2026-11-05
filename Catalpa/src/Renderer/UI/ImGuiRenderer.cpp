#include "catpch.h"
#include "ImGuiRenderer.h"
#include "ImGuiFrameData.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "RHI/RHIDevice.h"
#include "Assets/AssetManager.h"
#include "Shaders/ShaderLibrary.h"
#include "Log/Log.h"

namespace Catalpa {

    namespace {
        // transform.xy = 2 / DisplaySize；zw = -1 - DisplayPos * xy。
        // Vulkan 正高度 viewport 的左上角对应 NDC (-1,-1)，无需额外翻转 Y。
        struct alignas(16) ImGuiParameters {
            std::array<float, 4> transform{};
            std::array<float, 4> textureOptions{ 0, 15, 0, 0 };
        };
        static_assert(sizeof(ImGuiParameters) == 32);
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

    ImGuiRenderer::ImGuiRenderer(RHIDevice& device, AssetManager& assets, TextureGpuCache& textures, PipelineLibrary& pipelines)
        : m_Device(device), m_Frames(device.GetFrameCount()), m_Assets(assets), m_Textures(textures), m_Pipelines(pipelines) {}

    ImGuiRenderer::~ImGuiRenderer()
    {
        for (const auto& frame : m_Frames) {
            m_Device.ReleaseBuffer(frame.vertices);
            m_Device.ReleaseBuffer(frame.indices);
        }
        m_Device.ReleaseSampler(m_Sampler);
        m_Device.ReleaseSampler(m_NearestSampler);
    }

    RHIStatus ImGuiRenderer::InitFont(const AssetVersionRef<ImageAsset>& version)
    {
        if(m_Font) return {.code=RHIErrorCode::InvalidState,.operation="ImGuiRenderer::InitFont"};
        const RHISamplerDesc sampler{.minFilter=RHIFilter::Linear,.magFilter=RHIFilter::Linear,
            .addressU=RHISamplerAddressMode::ClampToEdge,.addressV=RHISamplerAddressMode::ClampToEdge,.maxLod=32};
        if(!m_Sampler.HasValue())
            if(const auto status=m_Device.CreateSampler(sampler,m_Sampler);!status.IsSuccess()) return status;
        if (!m_NearestSampler.HasValue()) {
            const RHISamplerDesc nearest{ .mipFilter = RHIFilter::Nearest, .addressU = RHISamplerAddressMode::ClampToEdge,
                .addressV = RHISamplerAddressMode::ClampToEdge, .maxLod = 32 };
            if (const auto status = m_Device.CreateSampler(nearest, m_NearestSampler); !status.IsSuccess()) return status;
        }
        return m_Textures.Acquire(version,m_Font);
    }

    RHIStatus ImGuiRenderer::EnsurePipeline(PixelFormat format)
    {
        // UI 颜色在 shader 中解码，附件负责唯一一次 sRGB 编码与线性混合。
        if (!GetPixelFormatInfo(format).isSRGB)
            return { .code = RHIErrorCode::Unsupported, .operation = "ImGuiRenderer::SRGBAttachmentRequired" };
        AssetId id; AssetVersionRef<ShaderAsset> shader; bool fallback=false; std::string error;
        if(!ShaderLibrary::BuiltinId("ImGui.slang",id,error)||!m_Assets.Acquire(id,shader,fallback,error)||fallback)
            return {.code=RHIErrorCode::InvalidState,.operation="ImGuiRenderer/cooked shader"};
        if(m_Pipeline&&m_Format==format&&m_Pipeline->vertex.Get()==shader.Get()) return {};
        const auto* vertex=FindShaderEntry(shader.Get()->metadata,"vertexMain",ShaderStage::Vertex);
        if(!vertex||vertex->pushConstantBytes!=32||vertex->pushConstantMembers.size()!=2)
            return {.code=RHIErrorCode::InvalidDescription,.operation="ImGuiRenderer/push ABI"};
        const auto& transform=vertex->pushConstantMembers.front();
        if(transform.name!="transform"||transform.offset!=0||transform.size!=16||transform.paddedSize!=16
            ||transform.type.scalar!=ShaderScalarKind::Float||transform.type.width!=32||transform.type.rows!=1
            ||transform.type.columns!=4||transform.type.arrayCount!=1||transform.arrayStride||transform.matrixStride||!transform.members.empty())
            return {.code=RHIErrorCode::InvalidDescription,.operation="ImGuiRenderer/transform ABI"};
        const auto& options = vertex->pushConstantMembers[1];
        if (options.name != "textureOptions" || options.offset != 16 || options.size != 16
            || options.type.scalar != ShaderScalarKind::Float || options.type.width != 32 || options.type.columns != 4)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "ImGuiRenderer/texture ABI" };
        const RHIGraphicsPipelineDesc desc{
            .vertex = {.asset = shader, .stage = RHIShaderStage::Vertex, .entryPoint = "vertexMain"},
            .fragment = {.asset = shader, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain"},
            .colorFormat = format,
            .blendMode = RHIBlendMode::StraightAlpha,
            .vertexLayout = {.stride = sizeof(ImGuiVertex), .attributes = Attributes},
            .pushConstants = {.size = sizeof(ImGuiParameters), .visibility = RHIShaderVisibility::VertexFragment},
            .bindingLayout = {.entries = Bindings}
        };
        if (const auto status = m_Pipelines.Acquire(desc, m_Pipeline); !status.IsSuccess()) return status;
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
        // 关闭/重建时旧token可保活到最后一个CPU draw包消费完；GPU命令另有后端引用。
        std::set<uint64_t> referenced;
        for (const auto& draw : frame.draws) referenced.insert(draw.textureToken);
        for (auto it = m_Registered.begin(); it != m_Registered.end();)
            if (it->second.retired && !referenced.contains(it->first)) it = m_Registered.erase(it);
            else ++it;
        if (frame.draws.empty()) return {};
        if (slot >= m_Frames.size() || !m_Font || frame.vertices.empty() || frame.indices.empty())
            return { .code = RHIErrorCode::InvalidState, .operation = "ImGuiRenderer::Prepare" };
        if (const RHIStatus status = EnsurePipeline(format); !status.IsSuccess()) return status;
        auto& buffers = m_Frames[slot];
        if (const RHIStatus status = WriteDynamicBuffer(buffers.vertices, buffers.vertexCapacity,
            RHIBufferUsage::Vertex, std::as_bytes(std::span{frame.vertices})); !status.IsSuccess()) return status;
        return WriteDynamicBuffer(buffers.indices, buffers.indexCapacity,
            RHIBufferUsage::Index, std::as_bytes(std::span{frame.indices}));
    }

    RHIStatus ImGuiRenderer::RegisterTexture(TextureViewHandle view, std::shared_ptr<void> lease,
        uint64_t& token, uint32_t mip, uint32_t channelMask, bool preview)
    {
        if (!view.HasValue() || !lease || channelMask == 0 || channelMask > 15
            || m_NextTextureToken == UINT64_MAX || m_Registered.size() >= 1024)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "ImGuiRenderer::RegisterTexture" };
        RHITextureStateSnapshot snapshot;
        if (const auto status = m_Device.GetTextureState(view, snapshot); !status.IsSuccess()) return status;
        const bool linear = m_Device.GetFormatSupport(snapshot.format).linearFiltering;
        m_Registered.emplace(m_NextTextureToken, RegisteredTexture{ view, std::move(lease), mip, channelMask, preview, false, linear });
        if (!linear && m_ReportedNearestFormats.insert(snapshot.format).second)
            CAT_CORE_WARN("UI image format lacks linear filtering; this preview uses nearest sampling without altering the image.");
        token = m_NextTextureToken++;
        return {};
    }

    void ImGuiRenderer::ReleaseTexture(uint64_t token) noexcept
    {
        if (const auto found = m_Registered.find(token); found != m_Registered.end()) found->second.retired = true;
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
        if (const RHIStatus status = graph.ImportTexture(m_Font->view, font); !status.IsSuccess()) return status;

        ImGuiParameters parameters{};
        for (int axis = 0; axis < 2; ++axis) {
            parameters.transform[axis] = 2.0f / frame.displaySize[axis];
            parameters.transform[axis + 2] = -1.0f - frame.displayPos[axis] * parameters.transform[axis];
        }
        RGIndexedDrawDesc draw{
            .vertices = vertices, .indices = indices, .color = color, .pipeline = m_Pipeline->pipeline,
            .indexType = RHIIndexType::UInt32,
            .loadOp = RHILoadOp::Load, .storeOp = RHIStoreOp::Store,
            .viewport = {.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height)}
        };
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
            RGTexture sampled = font;
            SamplerHandle sampler = m_Sampler;
            parameters.textureOptions = { 0, 15, 0, 0 };
            if (command.textureToken != 0) {
                const auto found = m_Registered.find(command.textureToken);
                if (found == m_Registered.end())
                    return { .code = RHIErrorCode::InvalidState, .operation = "ImGuiRenderer/stale texture token" };
                if (const auto status = graph.ImportTexture(found->second.view, sampled); !status.IsSuccess()) return status;
                parameters.textureOptions = { static_cast<float>(found->second.mip),
                    static_cast<float>(found->second.channelMask), found->second.preview ? 1.0f : 0.0f, 0 };
                if (!found->second.linearFiltering) sampler = m_NearestSampler;
            }
            const auto bytes = std::as_bytes(std::span{ &parameters, 1 });
            draw.pushConstants.assign(bytes.begin(), bytes.end());
            draw.sampledTextures = { { .texture = sampled, .sampler = sampler } };
            if (const RHIStatus status = graph.AddDrawPass("ImGui", draw); !status.IsSuccess()) return status;
        }
        return {};
    }

}
