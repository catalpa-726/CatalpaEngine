#include "catpch.h"
#include "EngineRuntime.h"

#include "Log/Log.h"
#include "Core/Window.h"

#include "Renderer/Renderer.h"

#include "RHI/RHIBackendFactory.h"
#include "RHI/RHISwapchain.h"

#include "Platform/Vulkan/VulkanBackend.h"
#include "Scene/Scene.h"
#include "ECS/SystemContext.h"
#include "Runtime/RuntimeServices.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Runtime/Systems/RenderExtractSystem.h"

namespace Catalpa {

    // 复用现有窗口接口，生成当前请求。最小化统一表达为零尺寸，初始化与后续更新都使用这个函数
    static RHISwapchainDesc MakeSwapchainDesc(const Window& window)
    {
        const auto [width, height] = window.GetFramebufferSize();

        return {
            .extent = window.IsMinimized() ? Extent2D{} : Extent2D{ width, height },
            .preferredPresentMode = window.IsVSync() ? RHIPresentMode::Fifo : RHIPresentMode::Immediate
        };
    }

    EngineRuntime::EngineRuntime() = default;

    EngineRuntime::~EngineRuntime() { Shutdown(); }

    RHIStatus EngineRuntime::Init(Window& window, const RHIBackendDesc& backendDesc)
    {
        CAT_CORE_ASSERT(!m_Device, "Engine runtime already initialized!");

        RHIBackendFactory factory;
        factory.Register(GraphicsBackend::Vulkan, CreateVulkanDevice);

        RHIStatus status;
        m_Device = factory.Create(backendDesc, window, status);
        if (!status.IsSuccess()) return status;

        const RHISwapchainDesc desc = MakeSwapchainDesc(window);
        m_Swapchain = m_Device->CreateSwapchain(desc, status);

        if (!status.IsSuccess()) {
            Shutdown();
            return status;
        }

        m_Renderer = std::make_unique<Renderer>(*m_Device, *m_Swapchain);
        m_SimulationStopped = false;
        m_FrameSequence = 0;
        m_InputSceneIdentity = 0;
        m_Clock.Reset();
        m_LastSwapchainDesc = desc;
        return {};
    }

    void EngineRuntime::Shutdown()
    {
        StopSimulation();
        // 本步没有异步 World 消费者；先清场景及未提交 CPU 命令。
        if (const WorldStatus status = m_Scenes.Clear(); !status.IsSuccess()) {
            CAT_CORE_CRITICAL("Cannot shut down while scene data is borrowed: {0}", status.message);
            std::abort();
        }
        if (m_Device) {
            // 兼容路径采用设备空闲收尾；支持呈现 Fence 时追加独立的呈现等待。
            RHIStatus status = m_Device->WaitIdleAndDiscardFrameCommands();
            if (status.IsSuccess() && m_Swapchain && m_Device->GetCapabilities().presentFence) status = m_Swapchain->WaitForPresentCompletion();

            if (!status.IsSuccess() && status.code != RHIErrorCode::DeviceLost) {
                CAT_CORE_ASSERT(false, "Cannot safely release resources after GPU or presentation wait failed!");
                std::abort();
            }
        }

        m_Renderer.reset();
        m_Swapchain.reset();
        m_Device.reset();
        m_LastSwapchainDesc = {};
        m_SwapchainDirty = true;
    }

    RHIStatus EngineRuntime::InitUI(std::span<const std::byte> fontPixels, Extent2D fontExtent)
    {
        if (!m_Renderer) return { RHIErrorCode::InvalidState, 0, "EngineRuntime::InitUI/Renderer" };
        return m_Renderer->InitUI(fontPixels, fontExtent);
    }

    WorldStatus EngineRuntime::UpdateScenes() { return m_Scenes.UpdateActive(); }

    const RenderFramePacket& EngineRuntime::GetSceneFrame() const noexcept
    {
        static const RenderFramePacket empty;
        return m_Renderer ? m_Renderer->GetSceneFrame() : empty;
    }

    void EngineRuntime::StopSimulation() noexcept
    {
        m_SimulationStopped = true;
        m_Input.ResetGameplayContext();
        if (m_Renderer) m_Renderer->ClearSceneFrame();
    }

    WorldStatus EngineRuntime::EnsureSceneSystems(Scene& scene)
    {
        auto& systems = scene.GetSystems();
        if (!systems.HasSystem("engine.transform")) {
            SystemDesc transform;
            transform.id = "engine.transform";
            transform.phase = SystemPhase::Transform;
            transform.accesses = { { "scene.commands", SystemAccessMode::Read }, { "scene.structure", SystemAccessMode::Write }, { "scene.transform", SystemAccessMode::Write } };
            transform.execute = [](SystemContext& context) {
                if (const auto status = context.scene.CommitPending(); !status.IsSuccess()) return status;
                return TransformSystem::Update(context.scene);
            };
            if (const auto status = systems.Add(std::move(transform)); !status.IsSuccess()) return status;
        }
        if (!systems.HasSystem("engine.extract")) {
            SystemDesc extract;
            extract.id = "engine.extract";
            extract.phase = SystemPhase::Extract;
            extract.accesses = { { "scene.transform", SystemAccessMode::Read }, { "scene.camera", SystemAccessMode::Read }, { "scene.mesh-renderer", SystemAccessMode::Read }, { "render.frame", SystemAccessMode::Write } };
            extract.after = { "engine.transform" };
            extract.execute = [](SystemContext& context) -> WorldStatus {
                const auto& services = context.services;
                if (!services.renderFrame) return { WorldError::InvalidArgument, "Render extraction target is missing" };
                return RenderExtractSystem::Extract(context.scene, services.renderFrame->sequence,
                    services.targetWidth, services.targetHeight, *services.renderFrame);
            };
            if (const auto status = systems.Add(std::move(extract)); !status.IsSuccess()) return status;
        }
        return systems.Compile();
    }

    WorldStatus EngineRuntime::UpdateSimulation(double realSeconds, InputCapture capture, uint32_t width, uint32_t height)
    {
        if (m_SimulationStopped || !m_Renderer) return { WorldError::Busy, "Simulation is stopped or renderer is unavailable" };
        if (m_FrameSequence == std::numeric_limits<uint64_t>::max()) return { WorldError::CapacityExceeded, "Presentation frame sequence exhausted" };
        if (!std::isfinite(realSeconds) || realSeconds < 0.0) return { WorldError::InvalidArgument, "Real frame duration must be finite and nonnegative" };
        try {
            const Scene* active = m_Scenes.GetActive();
            const uint64_t activeIdentity = active ? active->GetWorld().GetIdentity() : 0;
            if (activeIdentity != m_InputSceneIdentity) {
                m_Input.ResetGameplayContext();
                m_InputSceneIdentity = activeIdentity;
            }
            capture.gameplayActive = capture.gameplayActive && activeIdentity != 0;
            const auto& actions = m_Input.BeginFrame(capture);
            if (const auto* pause = actions.Find("simulation.pause"); pause && pause->pressed) {
                m_Clock.SetPaused(!m_Clock.IsPaused());
                CAT_CORE_INFO("[S03.2] Simulation paused = {0}", m_Clock.IsPaused());
            }
            if (const auto* step = actions.Find("simulation.step"); step && step->pressed) {
                if (const auto status = m_Clock.RequestSingleStep(); !status.IsSuccess()) CAT_CORE_WARN("[S03.2] Single step rejected: {0}", status.message);
            }
            if (const auto status = m_Clock.Advance(realSeconds, m_FrameTime); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
            if (m_FrameTime.droppedSeconds > 0.0) CAT_CORE_WARN("[S03.2] Dropped {0} simulation seconds at catch-up limit", m_FrameTime.droppedSeconds);
            RenderFramePacket packet;
            packet.sequence = m_FrameSequence + 1;
            RuntimeServices services{ &actions, width, height, &packet };
            const auto status = m_Scenes.WithActive([&](Scene& scene) -> WorldStatus {
                if (const auto ready = EnsureSceneSystems(scene); !ready.IsSuccess()) return ready;
                // 提交此前的宿主命令，即使这一展示帧没有模拟 tick 也发布派生状态。
                SystemContext presentation{ scene, m_FrameTime, services };
                auto& systems = scene.GetSystems();
                if (const auto updated = systems.RunPhase(SystemPhase::Transform, presentation); !updated.IsSuccess()) return updated;
                for (uint32_t index = 0; index < m_FrameTime.tickCount; ++index) {
                    FrameTime tick = m_FrameTime;
                    tick.tickCount = 1;
                    tick.firstTick += index;
                    tick.simulationSeconds -= (m_FrameTime.tickCount - 1 - index) * tick.fixedDeltaSeconds;
                    const ActionSnapshot tickInput = m_Input.ConsumeTick();
                    RuntimeServices tickServices{ &tickInput, width, height, &packet };
                    SystemContext context{ scene, tick, tickServices };
                    if (const auto executed = systems.RunPhase(SystemPhase::Gameplay, context); !executed.IsSuccess()) return executed;
                    if (const auto updated = systems.RunPhase(SystemPhase::Transform, context); !updated.IsSuccess()) return updated;
                }
                if (const auto camera = systems.RunPhase(SystemPhase::Camera, presentation); !camera.IsSuccess()) return camera;
                return systems.RunPhase(SystemPhase::Extract, presentation);
            });
            if (!status.IsSuccess()) return status;
            if (const auto applied = m_Renderer->SetSceneFrame(std::move(packet)); !applied.IsSuccess()) return { WorldError::InvalidArgument, applied.message };
            ++m_FrameSequence;
            return {};
        } catch (const std::bad_alloc&) { return { WorldError::AllocationFailed, "Cannot prepare simulation frame" }; }
    }

    // 每轮先根据事件标记、VSync 变化或交换链状态处理更新，再执行绘制。
    // UpdateSwapchain 负责查询和比较请求配置，确实需要时才重建。
    RHIStatus EngineRuntime::Update(Window& window, const ImGuiFrameData* uiFrame)
    {
        if (const RHIStatus status = UpdateSwapchain(window); !status.IsSuccess()) return status;
        return m_Renderer->Render(uiFrame);
    }

    RHIStatus EngineRuntime::UpdateSwapchain(Window& window)
    {
        const bool dirty = std::exchange(m_SwapchainDirty, false);
        const RHISwapchainState state = m_Swapchain->GetInfo().state;
        const RHIPresentMode requestedMode = window.IsVSync() ? RHIPresentMode::Fifo : RHIPresentMode::Immediate;

        const bool settingsChanged = requestedMode != m_LastSwapchainDesc.preferredPresentMode;
        const bool retrySuspended = state == RHISwapchainState::Suspended && !m_LastSwapchainDesc.extent.IsEmpty();

        if (!dirty && !settingsChanged && !retrySuspended && state != RHISwapchainState::OutOfDate) return {};

        const RHISwapchainDesc desc = MakeSwapchainDesc(window);
        const bool restoring = state == RHISwapchainState::Suspended && !desc.extent.IsEmpty();

        if (desc == m_LastSwapchainDesc && !restoring && state != RHISwapchainState::OutOfDate) return {};

        if (const RHIStatus status = m_Device->WaitIdleAndDiscardFrameCommands(); !status.IsSuccess()) return status;

        if (m_Device->GetCapabilities().presentFence) {
            if (const RHIStatus status = m_Swapchain->WaitForPresentCompletion(); !status.IsSuccess()) return status;
        }

        const RHIStatus status = m_Swapchain->Recreate(desc);

        if (status.IsSuccess()) m_LastSwapchainDesc = desc;

        return status;
    }

}
