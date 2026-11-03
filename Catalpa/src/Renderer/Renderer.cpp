#include "catpch.h"
#include "Renderer.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHICommandList.h"
#include "TriangleVertexSpirv.h"
#include "TriangleFragmentSpirv.h"

namespace Catalpa {

    Renderer::~Renderer()
    {
        m_Device.ReleaseGraphicsPipeline(m_TrianglePipeline);
    }

    RHIStatus Renderer::RecordFrame(RHICommandList& commands, const RHIAcquireResult& acquired)
    {
        if (const RHIStatus status = commands.Begin(); !status.IsSuccess()) return status;

        const RHITextureAccess colorAccess{
            .stage = RHIPipelineStage::ColorAttachmentOutput,
            .access = RHIResourceAccess::ColorWrite,
            .layout = RHITextureLayout::ColorAttachment
        };

        const RHITextureBarrier toColor{
            .view = acquired.view,
            .before = {.stage = RHIPipelineStage::AllCommands, .layout = RHITextureLayout::Undefined },
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
            .renderArea = {.extent = extent }
        };

        const RHIViewport viewport{ .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
        const Rect2D scissor{ .extent = extent };

        if (const RHIStatus status = commands.BeginRendering(rendering); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.BindGraphicsPipeline(m_TrianglePipeline); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.SetViewportAndScissor(viewport, scissor); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.Draw(3); !status.IsSuccess()) return status;
        if (const RHIStatus status = commands.EndRendering(); !status.IsSuccess()) return status;

        const RHITextureBarrier toPresent{
            .view = acquired.view,
            .before = colorAccess,
            .after = {.layout = RHITextureLayout::Present }
        };

        if (const RHIStatus status = commands.PipelineBarrier({ &toPresent, 1 }); !status.IsSuccess()) return status;
        return commands.End();
    }

    RHIStatus Renderer::CancelFrame(RHIAcquireResult& acquired, RHIStatus failure)
    {
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
            .colorFormat = format
        };

        GraphicsPipelineHandle pipeline{};
        if (const RHIStatus status = m_Device.CreateGraphicsPipeline(desc, pipeline); !status.IsSuccess()) return status;

        const GraphicsPipelineHandle previous = std::exchange(m_TrianglePipeline, pipeline);
        m_TriangleFormat = format;
        m_Device.ReleaseGraphicsPipeline(previous);
        return {};
    }

}