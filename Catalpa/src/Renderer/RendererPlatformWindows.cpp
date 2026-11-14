#include "catpch.h"

#include "Renderer/RendererInternal.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {
	namespace {
		/**
		 * @brief 在窗口关闭或缩放前收尾旧帧及呈现操作，释放录制命令对旧附件的引用。
		 * 只在跨窗关闭/缩放时执行；正常帧继续使用帧槽的 fence。
		 *
		 * @param[in,out] device 负责等待空闲并丢弃旧帧命令的设备。
		 * @param[in,out] swapchain 待回收旧帧的窗口交换链，支持呈现栅栏时等待呈现完成。
		 *
		 * @return 返回设备命令及呈现等待的收尾状态；具体同步路径取决于设备能力。
		 */
		RHIStatus RetireWindowFrames(RHIDevice& device, RHISwapchain& swapchain) {
			if (auto status = device.WaitIdleAndDiscardFrameCommands(); !status.IsSuccess())
				return status;
			return device.GetCapabilities().presentFence ? swapchain.WaitForPresentCompletion() : RHIStatus{};
		}
	}

	bool Renderer::GetPlatformWindowPresentation(uint32_t id, PlatformWindowPresentation& output) const {
		if (id == 0) {
			output = m_MainPresentation;
			return output.sequence != 0;
		}
		const auto found = m_PlatformChains.find(id);
		if (found == m_PlatformChains.end())
			return false;
		output = found->second.presentation;
		return true;
	}

	RHIStatus Renderer::ReleasePlatformWindow(uint32_t id) {
		const auto found = m_PlatformChains.find(id);
		if (found == m_PlatformChains.end())
			return {};
		auto status = RetireWindowFrames(m_Device, *found->second.swapchain);
		/* 失设备时同样必须在 GLFW 销毁窗口之前收回原生表面。 */
		m_PlatformChains.erase(found);
		return status;
	}

	RHIStatus Renderer::RenderPlatformWindow(uint32_t id, void* window, Extent2D extent, const ImGuiFrameData& ui) {
		if (!m_PlatformFrameReady || extent.IsEmpty())
			return {};
		RHIAcquireResult acquired;
		RHISwapchain* chain = nullptr;
		bool accepted = false;
		const auto cancel = [&](RHIStatus failure) {
			if (chain && acquired.HasImage() && !accepted && failure.code != RHIErrorCode::DeviceLost) {
				RHICompletionPoint completion;
				const auto status = chain->CancelAcquire(acquired, completion);
				if (!status.IsSuccess())
					return status;
				m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
			}
			return failure;
		};
		try {
			auto it = m_PlatformChains.find(id);
			RHISwapchainDesc desc;
			desc.extent = extent;
			desc.preferredPresentMode = RHIPresentMode::Mailbox;
			if (it == m_PlatformChains.end()) {
				RHIStatus status;
				auto swapchain = m_Device.CreateWindowSwapchain(window, desc, status);
				if (!status.IsSuccess())
					return status;
				if (!swapchain)
					return {RHIErrorCode::InvalidState, 0, "Create platform window swapchain"};
				it = m_PlatformChains.emplace(id, PlatformWindow{std::move(swapchain), {}}).first;
			}
			chain = it->second.swapchain.get();
			const auto& info = chain->GetInfo();
			/* 原生 surface 丢失必须连同交换链一并释放，下一帧由 GLFW 窗口重新创建。 */
			if (info.state == RHISwapchainState::SurfaceLost)
				return ReleasePlatformWindow(id);
			if (info.state != RHISwapchainState::Ready || info.extent != extent) {
				if (auto s = RetireWindowFrames(m_Device, *chain); !s.IsSuccess())
					return s;
				if (auto s = chain->Recreate(desc); !s.IsSuccess())
					return info.state == RHISwapchainState::SurfaceLost ? ReleasePlatformWindow(id) : s;
				if (info.state != RHISwapchainState::Ready)
					return {};
			}

			RHIFrameResources slot;
			bool ready = false;
			constexpr uint64_t timeout = 16'000'000;
			if (auto s = m_Device.PrepareFrame(m_FrameSlot, timeout, slot, ready); !s.IsSuccess() || !ready)
				return s;
			if (auto s = m_UI.Prepare(m_FrameSlot, ui, info.format, false); !s.IsSuccess())
				return s;
			if (auto s = chain->Acquire(slot.token, timeout, acquired); !s.IsSuccess() || !acquired.HasImage())
				return info.state == RHISwapchainState::SurfaceLost ? ReleasePlatformWindow(id) : s;

			RenderGraph graph(m_Device);
			RGTexture color;
			if (auto s = graph.ImportTexture(acquired.view, color); !s.IsSuccess())
				return cancel(s);
			if (auto s = graph.AddClearPass("Platform window clear", color, m_Profile.clearColor); !s.IsSuccess())
				return cancel(s);
			if (auto s = m_UI.AddPasses(graph, color, info.extent, m_FrameSlot, ui); !s.IsSuccess())
				return cancel(s);
			if (auto s = graph.ExportPresent(color); !s.IsSuccess())
				return cancel(s);
			RenderGraphPlan plan;
			if (auto s = graph.Compile(plan); !s.IsSuccess())
				return cancel(s);
			const RHISubmission submission{.frame = slot.token,
			    .commands = slot.commands,
			    .wait = acquired.acquireReady,
			    .signal = acquired.renderFinished};
			FrameSubmissionResult result;
			auto s = FrameScheduler::Execute(plan, m_Device.GetGraphicsQueue(), submission, result, nullptr, false);
			accepted = result.accepted;
			if (result.anyAccepted)
				m_Textures.MarkUses(result.completions.GetPoints());
			if (!s.IsSuccess() || !accepted)
				return cancel(
				    s.IsSuccess() ? RHIStatus{RHIErrorCode::InvalidState, 0, "Platform window submission"} : s);
			m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
			RHIPresentOutcome outcome;
			const auto presented = chain->Present(acquired, outcome);
			if (presented.IsSuccess() &&
			    (outcome == RHIPresentOutcome::Queued || outcome == RHIPresentOutcome::Suboptimal)) {
				it->second.presentation.extent = info.extent;
				++it->second.presentation.sequence;
			}
			return outcome == RHIPresentOutcome::SurfaceLost ? ReleasePlatformWindow(id) : presented;
		} catch (const std::bad_alloc&) {
			return cancel({RHIErrorCode::OutOfMemory, 0, "Renderer/PlatformWindow"});
		}
	}
}
