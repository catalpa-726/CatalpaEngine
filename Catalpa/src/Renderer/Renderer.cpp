#include "catpch.h"
#include "Renderer.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITextureDesc.h"
#include "RHI/RHIBindingDesc.h"
#include "RHI/RHISamplerDesc.h"
#include "TriangleVertexSpirv.h"
#include "TriangleFragmentSpirv.h"
#include "Geometry/VertexLayout.h"
#include "RenderGraph/RenderGraph.h"

namespace Catalpa {

    namespace {

        struct TriangleVertex {
            float position[2];
            float color[3];
            float uv[2];
        };

        static_assert(sizeof(float) == 4);
        static_assert(sizeof(TriangleVertex) == 28);
        static_assert(offsetof(TriangleVertex, position) == 0);
        static_assert(offsetof(TriangleVertex, color) == 8);
        static_assert(offsetof(TriangleVertex, uv) == 20);

        // 与 Slang 中两个 float4 对应。
        // transform.xy 为缩放，transform.zw 为平移。
        struct alignas(16) TriangleDrawParameters {
            std::array<float, 4> transform{};
            std::array<float, 4> tint{};
        };

        static_assert(std::is_standard_layout_v<TriangleDrawParameters>);
        static_assert(std::is_trivially_copyable_v<TriangleDrawParameters>);
        static_assert(sizeof(TriangleDrawParameters) == 32);
        static_assert(offsetof(TriangleDrawParameters, transform) == 0);
        static_assert(offsetof(TriangleDrawParameters, tint) == 16);

        constexpr std::array<TriangleVertex, 3> TriangleVertices{ {
            { { 0.0f, -0.6f }, { 1.0f, 0.0f, 1.0f }, { 0.5f, 0.0f } },
            { { 0.6f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
            { {-0.6f,  0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
        } };

        constexpr std::array<uint32_t, 3> TriangleIndices{ 0, 1, 2 };

        constexpr std::array<VertexAttribute, 3> TriangleAttributes{ {
            {.location = 0, .format = VertexFormat::Float2, .offset = static_cast<uint32_t>(offsetof(TriangleVertex, position)) },
            {.location = 1, .format = VertexFormat::Float3, .offset = static_cast<uint32_t>(offsetof(TriangleVertex, color)) },
            {.location = 2, .format = VertexFormat::Float2, .offset = static_cast<uint32_t>(offsetof(TriangleVertex, uv)) }
        } };

        constexpr std::array<RHIBindingLayoutEntry, 2> TriangleBindings{ {
            {.binding = 0, .type = RHIBindingType::SampledTexture, .visibility = RHIShaderVisibility::Fragment },
            {.binding = 1, .type = RHIBindingType::Sampler, .visibility = RHIShaderVisibility::Fragment }
        } };

        void SetTriangleDrawParameters(RGIndexedDrawDesc& draw, const TriangleDrawParameters& parameters)
        {
            const auto bytes = std::as_bytes(std::span{ &parameters, 1 });
            draw.pushConstants.assign(bytes.begin(), bytes.end());
        }

    }

    Renderer::~Renderer()
    {
        m_Uploads.Shutdown();
        m_Device.ReleaseSampler(m_TriangleSampler);
        m_Device.ReleaseTextureView(m_TriangleTextureView);
        m_Device.ReleaseTexture(m_TriangleTexture);
        m_Device.ReleaseBuffer(m_TriangleIndices);
        m_Device.ReleaseBuffer(m_TriangleVertices);
        m_Device.ReleaseGraphicsPipeline(m_TrianglePipeline);
    }

    RHIStatus Renderer::BuildFrameGraph(RenderGraph& graph, const RHIAcquireResult& acquired)
    {
        if (const RHIStatus status = m_Uploads.AddPasses(graph); !status.IsSuccess()) return status;

        RGBuffer vertices{}, indices{};
        RGTexture color{}, sampledTexture{};

        if (const RHIStatus status = graph.ImportBuffer(m_TriangleVertices, vertices); !status.IsSuccess()) return status;
        if (const RHIStatus status = graph.ImportBuffer(m_TriangleIndices, indices); !status.IsSuccess()) return status;
        if (const RHIStatus status = graph.ImportTexture(acquired.view, color); !status.IsSuccess()) return status;
        if (const RHIStatus status = graph.ImportTexture(m_TriangleTextureView, sampledTexture); !status.IsSuccess()) return status;

        const Extent2D extent = m_Swapchain.GetInfo().extent;

        RGIndexedDrawDesc draw{
            .vertices = vertices,
            .indices = indices,
            .color = color,
            .pipeline = m_TrianglePipeline,
            .vertexOffset = 0,
            .indexOffset = 0,
            .indexType = RHIIndexType::UInt32,
            .indexCount = static_cast<uint32_t>(TriangleIndices.size()),
            .loadOp = RHILoadOp::Clear,
            .storeOp = RHIStoreOp::Store,
            .clearColor = { 0.02f, 0.02f, 0.02f, 1.0f },
            .viewport = {
                .width = static_cast<float>(extent.width),
                .height = static_cast<float>(extent.height)
            },
            .scissor = {.extent = extent }
        };

        draw.sampledTextures.push_back({
            .texture = sampledTexture,
            .sampler = m_TriangleSampler,
            .textureBinding = 0,
            .samplerBinding = 1
            });

        constexpr TriangleDrawParameters left{
            .transform = { 0.60f, 0.60f, -0.42f, 0.0f },
            .tint = { 1.0f, 1.0f, 1.0f, 1.0f }
        };

        constexpr TriangleDrawParameters right{
            .transform = { 0.45f, 0.45f, 0.45f, 0.10f },
            .tint = { 0.4f, 0.8f, 1.0f, 1.0f }
        };

        SetTriangleDrawParameters(draw, left);
        if (const RHIStatus status = graph.AddDrawPass("Left Triangle - Clear", draw); !status.IsSuccess()) return status;

        // 首个 Pass 已保存自己的副本；此处修改不会覆盖它。
        draw.loadOp = RHILoadOp::Load;
        SetTriangleDrawParameters(draw, right);

        if (const RHIStatus status = graph.AddDrawPass("Right Triangle - Load", draw); !status.IsSuccess()) return status;
        return graph.ExportPresent(color);
    }

    RHIStatus Renderer::CancelFrame(RHIAcquireResult& acquired, RHIStatus failure)
    {
        // 取消路径不执行原来的复制命令，上传请求仍然待提交。
        m_Uploads.OnCancelled();

        if (failure.code == RHIErrorCode::DeviceLost) return failure;

        RHICompletionPoint completion{};
        const RHIStatus status = m_Swapchain.CancelAcquire(acquired, completion);

        if (!status.IsSuccess()) {
            if (status.code == RHIErrorCode::DeviceLost) return status;

            CAT_CORE_ASSERT(false, "Cannot safely finish an acquired frame after cancellation failed!");
            std::abort();
        }

        m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
        return failure;
    }

    RHIStatus Renderer::Render()
    {
        if (m_Swapchain.GetInfo().state != RHISwapchainState::Ready) return {};
        if (const RHIStatus status = EnsureTrianglePipeline(); !status.IsSuccess()) return status;
        if (const RHIStatus status = EnsureTriangleGeometry(); !status.IsSuccess()) return status;
        if (const RHIStatus status = EnsureTriangleTexture(); !status.IsSuccess()) return status;
        if (const RHIStatus status = EnsureTriangleSampler(); !status.IsSuccess()) return status;

        constexpr uint64_t timeoutNanoseconds = 16'000'000;
        RHIFrameResources frame{};
        bool ready = false;

        RHIStatus status = m_Device.PrepareFrame(m_FrameSlot, timeoutNanoseconds, frame, ready);
        if (!status.IsSuccess() || !ready) return status;

        RHIAcquireResult acquired{};
        status = m_Swapchain.Acquire(frame.token, timeoutNanoseconds, acquired);
        if (!status.IsSuccess() || !acquired.HasImage()) return status;

        RenderGraph graph(m_Device);
        RenderGraphPlan plan;

        status = BuildFrameGraph(graph, acquired);
        if (!status.IsSuccess()) return CancelFrame(acquired, status);

        status = graph.Compile(plan);
        if (!status.IsSuccess()) return CancelFrame(acquired, status);

        status = plan.Record(*frame.commands);
        if (!status.IsSuccess()) return CancelFrame(acquired, status);

        const RHISubmission submission{
            .frame = frame.token,
            .commands = frame.commands,
            .wait = acquired.acquireReady,
            .signal = acquired.renderFinished
        };

        RHICompletionPoint completion{};
        status = m_Device.GetGraphicsQueue().Submit(submission, completion);
        if (!status.IsSuccess()) return CancelFrame(acquired, status);
        m_Uploads.OnSubmitted(completion);

        m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();

        RHIPresentOutcome outcome{};
        return m_Swapchain.Present(acquired, outcome);
    }

    RHIStatus Renderer::EnsureTrianglePipeline()
    {
        const PixelFormat format = m_Swapchain.GetInfo().format;
        if (m_TrianglePipeline.HasValue() && m_TriangleFormat == format) return {};

        const RHIGraphicsPipelineDesc desc{
            .vertex = {.bytecode = TriangleVertexSpirv, .stage = RHIShaderStage::Vertex, .entryPoint = "vertexMain" },
            .fragment = {.bytecode = TriangleFragmentSpirv, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain" },
            .colorFormat = format,
            .vertexLayout = {
                .stride = static_cast<uint32_t>(sizeof(TriangleVertex)),
                .attributes = TriangleAttributes
            },
            .pushConstants = {
                .size = static_cast<uint32_t>(sizeof(TriangleDrawParameters)),
                .visibility = RHIShaderVisibility::VertexFragment
            },
            .bindingLayout = {.entries = TriangleBindings }
        };

        GraphicsPipelineHandle pipeline{};
        if (const RHIStatus status = m_Device.CreateGraphicsPipeline(desc, pipeline); !status.IsSuccess()) return status;

        const GraphicsPipelineHandle previous = std::exchange(m_TrianglePipeline, pipeline);
        m_TriangleFormat = format;
        m_Device.ReleaseGraphicsPipeline(previous);
        return {};
    }

    RHIStatus Renderer::EnsureTriangleGeometry()
    {
        if (!m_TriangleVertices.HasValue()) {
            const RHIStatus status = m_Uploads.CreateBuffer(std::as_bytes(std::span{ TriangleVertices }), RHIBufferUsage::Vertex, m_TriangleVertices);
            if (!status.IsSuccess()) return status;
        }

        if (!m_TriangleIndices.HasValue()) {
            const RHIStatus status = m_Uploads.CreateBuffer(std::as_bytes(std::span{ TriangleIndices }), RHIBufferUsage::Index, m_TriangleIndices);
            if (!status.IsSuccess()) return status;
        }

        return {};
    }

    RHIStatus Renderer::EnsureTriangleTexture()
    {
        if (m_TriangleTexture.HasValue()) return {};

        constexpr uint32_t side = 8;
        std::array<std::byte, side* side * 4> pixels{};

        // 生成 RGBA8 黑白棋盘，每格占 2 × 2 像素。
        for (uint32_t y = 0; y < side; ++y) {
            for (uint32_t x = 0; x < side; ++x) {
                const std::byte value = ((x / 2 + y / 2) & 1u) == 0 ? std::byte{ 0 } : std::byte{ 255 };
                const size_t offset = (static_cast<size_t>(y) * side + x) * 4;

                pixels[offset] = value;
                pixels[offset + 1] = value;
                pixels[offset + 2] = value;
                pixels[offset + 3] = std::byte{ 255 };
            }
        }

        const RHITextureDesc desc{
            .extent = {.width = side, .height = side },
            .format = PixelFormat::RGBA8_UNorm,
            .usage = RHITextureUsage::Sampled
        };

        return m_Uploads.CreateTexture(pixels, desc, m_TriangleTexture, m_TriangleTextureView);
    }

    RHIStatus Renderer::EnsureTriangleSampler()
    {
        if (m_TriangleSampler.HasValue()) return {};

        // 默认使用最近邻过滤和 Repeat 寻址。
        return m_Device.CreateSampler(RHISamplerDesc{}, m_TriangleSampler);
    }

}