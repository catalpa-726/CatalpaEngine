#include "catpch.h"
#include "Renderer.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHICommandList.h"

namespace Catalpa {

    RHIStatus Renderer::RecordClear(RHICommandList& commands, const RHIAcquireResult& acquired)
    {
        if (const RHIStatus status = commands.Begin(); !status.IsSuccess()) return status;

        const RHITextureAccess colorAccess{
            .stage = RHIPipelineStage::ColorAttachmentOutput,
            .access = RHIResourceAccess::ColorWrite,
            .layout = RHITextureLayout::ColorAttachment
        };

        // 整张图像会被清除，因此丢弃上次内容；等待 Acquire 后才能转换布局。
        const RHITextureBarrier toColor{
            .view = acquired.view,
            .before = {.stage = RHIPipelineStage::AllCommands, .layout = RHITextureLayout::Undefined },
            .after = colorAccess
        };

        if (const RHIStatus status = commands.PipelineBarrier({ &toColor, 1 }); !status.IsSuccess()) return status;

        const RHIColorAttachment color{
            .view = acquired.view,
            .loadOp = RHILoadOp::Clear,
            .storeOp = RHIStoreOp::Store,
            .clearColor = { 0.02f, 0.02f, 0.02f, 1.0f }
        };

        const RHIRenderingDesc rendering{
            .colorAttachments = { &color, 1 },
            .renderArea = {.extent = m_Swapchain.GetInfo().extent }
        };

        if (const RHIStatus status = commands.BeginRendering(rendering); !status.IsSuccess()) return status;
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

        constexpr uint64_t timeoutNanoseconds = 16'000'000;
        RHIFrameResources frame{};
        bool ready = false;

        RHIStatus status = m_Device.PrepareFrame(m_FrameSlot, timeoutNanoseconds, frame, ready);
        if (!status.IsSuccess() || !ready) return status;

        RHIAcquireResult acquired{};
        status = m_Swapchain.Acquire(frame.token, timeoutNanoseconds, acquired);
        if (!status.IsSuccess() || !acquired.HasImage()) return status;

        status = RecordClear(*frame.commands, acquired);
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

}