#include "catpch.h"
#include "Renderer.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHICommandList.h"
#include "TriangleVertexSpirv.h"
#include "TriangleFragmentSpirv.h"
#include "Geometry/VertexLayout.h"

namespace Catalpa {

    namespace {

        struct TriangleVertex {
            float position[2];
            float color[3];
        };

        static_assert(sizeof(float) == 4);
        static_assert(sizeof(TriangleVertex) == 20);
        static_assert(offsetof(TriangleVertex, position) == 0);
        static_assert(offsetof(TriangleVertex, color) == 8);

        constexpr std::array<TriangleVertex, 3> TriangleVertices{ {
            { { 0.0f, -0.6f }, { 1.0f, 0.0f, 1.0f } },
            { { 0.6f,  0.5f }, { 0.0f, 1.0f, 0.0f } },
            { {-0.6f,  0.5f }, { 1.0f, 1.0f, 1.0f } }
        } };

        constexpr std::array<uint32_t, 3> TriangleIndices{ 0, 1, 2 };

        constexpr std::array<VertexAttribute, 2> TriangleAttributes{ {
            {.location = 0, .format = VertexFormat::Float2, .offset = static_cast<uint32_t>(offsetof(TriangleVertex, position)) },
            {.location = 1, .format = VertexFormat::Float3, .offset = static_cast<uint32_t>(offsetof(TriangleVertex, color)) }
        } };

    }

    Renderer::~Renderer()
    {
        m_Uploads.Shutdown();
        m_Device.ReleaseBuffer(m_TriangleIndices);
        m_Device.ReleaseBuffer(m_TriangleVertices);
        m_Device.ReleaseGraphicsPipeline(m_TrianglePipeline);
    }

    RHIStatus Renderer::RecordFrame(RHICommandList& commands, const RHIAcquireResult& acquired)
    {
        if (const RHIStatus status = commands.Begin(); !status.IsSuccess()) return status;
        if (const RHIStatus status = m_Uploads.RecordPending(commands); !status.IsSuccess()) return status;

        const RHITextureAccess colorAccess{
            .stage = RHIPipelineStage::ColorAttachmentOutput,
            .access = RHIResourceAccess::ColorWrite,
            .layout = RHITextureLayout::ColorAttachment
        };

        const RHITextureBarrier toColor{
            .view = acquired.view,
            .before = { .stage = RHIPipelineStage::AllCommands, .layout = RHITextureLayout::Undefined },
            .after = colorAccess
        };

        if (const RHIStatus status = commands.PipelineBarrier({ &toColor, 1 }); !status.IsSuccess()) return status;

        const Extent2D extent = m_Swapchain.GetInfo().extent;

        const RHIColorAttachment color{
            .view = acquired.view,
            .loadOp = RHILoadOp::Clear,
            .storeOp = RHIStoreOp::Store,
            .clearColor = { 0.02f, 0.02f, 0.02f, 1.0f }
        };

        const RHIRenderingDesc rendering{
            .colorAttachments = { &color, 1 },
            .renderArea = { .extent = extent }
        };

        const RHIViewport viewport{ .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
        const Rect2D scissor{ .extent = extent };

        if (const RHIStatus status = commands.BeginRendering(rendering); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.BindGraphicsPipeline(m_TrianglePipeline); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.BindVertexBuffer(0, m_TriangleVertices); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.BindIndexBuffer(m_TriangleIndices, RHIIndexType::UInt32); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.SetViewportAndScissor(viewport, scissor); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.DrawIndexed(static_cast<uint32_t>(TriangleIndices.size())); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.EndRendering(); !status.IsSuccess()) return status;

        const RHITextureBarrier toPresent{
            .view = acquired.view,
            .before = colorAccess,
            .after = { .layout = RHITextureLayout::Present }
        };

        if (const RHIStatus status = commands.PipelineBarrier({ &toPresent, 1 }); !status.IsSuccess()) return status;
        return commands.End();
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

        constexpr uint64_t timeoutNanoseconds = 16'000'000;
        RHIFrameResources frame{};
        bool ready = false;

        RHIStatus status = m_Device.PrepareFrame(m_FrameSlot, timeoutNanoseconds, frame, ready);
        if (!status.IsSuccess() || !ready) return status;

        RHIAcquireResult acquired{};
        status = m_Swapchain.Acquire(frame.token, timeoutNanoseconds, acquired);
        if (!status.IsSuccess() || !acquired.HasImage()) return status;

        status = RecordFrame(*frame.commands, acquired);
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
            .vertex = { .bytecode = TriangleVertexSpirv, .stage = RHIShaderStage::Vertex, .entryPoint = "vertexMain" },
            .fragment = { .bytecode = TriangleFragmentSpirv, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain" },
            .colorFormat = format,
            .vertexLayout = {
                .stride = static_cast<uint32_t>(sizeof(TriangleVertex)),
                .attributes = TriangleAttributes
            }
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

}