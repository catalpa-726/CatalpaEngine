#include "catpch.h"
#include "Runtime/RuntimeRegistration.h"
#include "Shaders/ShaderLibrary.h"
#include "Assets/BuiltinAssets.h"
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
        // 重复初始化不得先重建资产服务或拆掉仍在使用的运行时。
        if (m_Device || m_Assets)
            return { .code = RHIErrorCode::InvalidState, .operation = "EngineRuntime::Init/AlreadyInitialized" };
        std::string assetError;
        if (!InitAssets(assetError)) {
            CAT_CORE_ERROR("Runtime assets unavailable: {0}", assetError);
            Shutdown();
            return { .code=RHIErrorCode::InvalidState,.operation="EngineRuntime::InitAssets" };
        }

        CAT_CORE_ASSERT(!m_Device, "Engine runtime already initialized!");

        RHIBackendFactory factory;
        factory.Register(GraphicsBackend::Vulkan, CreateVulkanDevice);

        RHIStatus status;
        m_Device = factory.Create(backendDesc, window, status);
        if (!status.IsSuccess()) { Shutdown(); return status; }

        const RHISwapchainDesc desc = MakeSwapchainDesc(window);
        m_Swapchain = m_Device->CreateSwapchain(desc, status);

        if (!status.IsSuccess()) {
            Shutdown();
            return status;
        }

        m_Renderer = std::make_unique<Renderer>(*m_Device, *m_Swapchain, *m_Assets, m_RendererProfile);
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
        m_PreviewRequests.clear();
        m_LastPreviewError.clear();
        m_EnvironmentRequestErrors.clear();
        // 停止资产发布并等待CPU任务；旧固定版本仍由场景/渲染快照保活。
        if (m_Assets) m_Assets->Shutdown();
        m_Jobs.Shutdown();
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
        m_Assets.reset();
        m_LastSwapchainDesc = {};
        m_SwapchainDirty = true;
    }

    bool EngineRuntime::ConfigureResources(std::filesystem::path absoluteRoot, std::string& error)
    {
        if(m_Assets||m_Device||!absoluteRoot.is_absolute()) {error="Resources require an absolute root before initialization";return false;}
        m_ResourceRoot=std::move(absoluteRoot); error.clear(); return true;
    }
    bool EngineRuntime::InitAssets(std::string& error)
    {
        try {
        if(m_Assets||!m_ResourceRoot.is_absolute()) {error="Asset services require configured resources";return false;}
        auto schemas=std::make_shared<ComponentRegistry>();
        if(!RegisterRuntimeComponentSchemas(*schemas,error)||!m_Jobs.Start({},error)) return false;
        const auto shaders=m_ResourceRoot/"Shaders";
        std::vector<AssetRecord> records;
        VirtualFileSystem files; AssetRegistry registry;
        if(!ShaderLibrary::ReadBuiltinCatalog(shaders,"shaders",records,error)
            ||!registry.Replace(records,error)||!files.MountReadOnly("shaders",shaders,0,error)) return false;
        // 4K RGBA8加布局需超过64MiB；逐资产准入，含解码工作区的同一CPU预算为1GiB。
        AssetManagerConfig assetConfig;
        assetConfig.maxFileBytes = 96 * 1024 * 1024;
        assetConfig.maxResidentBytes = 160 * 1024 * 1024;
        assetConfig.memoryBudgetBytes = 1024 * 1024 * 1024;
        m_Assets = std::make_unique<AssetManager>(m_Jobs, std::move(files), std::move(registry), assetConfig);
        if(!RegisterRuntimeAssetLoaders(*m_Assets,error)) return false;
        // 启动只等待必须的内置shader。实际读取/解码始终在worker，发布仍在本线程。
        // 逐项等待避免文件数增长时把队列预算误当可无限并行的承诺。
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        for(const auto& record:records) {
            if(!m_Assets->Request(record.id,error)) return false;
            for(;;) {
                m_Assets->Pump();
                const auto status=m_Assets->GetStatus(record.id);
                if(status==AssetStatus::Ready) break;
                if(status==AssetStatus::Failed||status==AssetStatus::Cancelled) {error=m_Assets->GetDiagnostic(record.id);return false;}
                if(std::chrono::steady_clock::now()>=deadline) {error="Builtin shader load timed out";return false;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        for(const auto name:{"Pbr.slang","Unlit.slang","Depth.slang","Shadow.slang","ImGui.slang",
            "Skybox.slang","Tonemap.slang","Present.slang","Picking.slang","DebugDraw.slang"}) {
            AssetId id;AssetVersionRef<ShaderAsset> shader;bool fallback=false;
            if(!ShaderLibrary::BuiltinId(name,id,error)||!m_Assets->Acquire(id,shader,fallback,error)||fallback) {error="Required cooked builtin shader missing: "+std::string(name);return false;}
        }
        AssetId fallbackShader;
        if(!ShaderLibrary::BuiltinId("Unlit.slang",fallbackShader,error)||!m_Assets->SetFallback<ShaderAsset>(fallbackShader,error)) return false;
        // 内置材质发布必须晚于shader就绪；固定依赖一次绑定，不修改已经发布的definition。
        if(!RegisterBuiltinAssets(*m_Assets,error)) return false;
        if(const auto status=m_Scenes.SetComponentSchemas(schemas);!status.IsSuccess()) {error=status.message;return false;}
        m_ComponentSchemas=std::move(schemas);
        error.clear();return true;
        } catch(const std::exception& exception) {error=exception.what();return false;}
    }

    RHIStatus EngineRuntime::ConfigureRendererProfile(std::string_view json)
    {
        if (m_Device) return { RHIErrorCode::InvalidState, 0, "EngineRuntime::ConfigureRendererProfile/AlreadyInitialized" };
        return ParseRendererProfile(json, m_RendererProfile);
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

    bool EngineRuntime::SetEditorPreview(SceneHandle scene, uint64_t viewId, Extent2D extent, std::string& error)
    {
        if (!m_Renderer || !m_Scenes.GetScene(scene) || viewId == 0 || extent.IsEmpty()) {
            error = "Preview requires a live scene, nonzero view ID and nonempty extent";
            return false;
        }
        const auto& caps = m_Device->GetCapabilities();
        if (extent.width > caps.maxRenderTargetExtent.width || extent.height > caps.maxRenderTargetExtent.height) {
            error = "Preview extent exceeds render-target limits";
            return false;
        }
        const auto previous = m_PreviewRequests.find(viewId);
        if (previous != m_PreviewRequests.end() && previous->second.scene == scene && previous->second.extent == extent) {
            error.clear();
            return true;
        }
        if (m_NextPreviewGeneration == std::numeric_limits<uint64_t>::max()) {
            error = "Preview generation exhausted";
            return false;
        }
        try {
            auto candidate = m_PreviewRequests;
            candidate.insert_or_assign(viewId, PreviewRequest{ scene, extent, m_NextPreviewGeneration });
            m_PreviewRequests.swap(candidate);
            ++m_NextPreviewGeneration;
            error.clear();
            return true;
        } catch (const std::bad_alloc&) { error = "Cannot capture preview request"; return false; }
    }

    bool EngineRuntime::GetEditorPreview(uint64_t viewId, RenderSurfaceSnapshot& surface) const
    {
        return m_Renderer && m_Renderer->GetPreviewSurface(viewId, surface);
    }

    void EngineRuntime::CloseEditorPreview(uint64_t viewId)
    {
        m_PreviewRequests.erase(viewId);
        if (m_Renderer) m_Renderer->ReleasePreviewSurface(viewId);
    }

    bool EngineRuntime::SetEditorPreviewImage(uint64_t viewId, const AssetVersionRef<ImageAsset>& image,
        uint32_t mip, uint32_t layer, uint32_t channelMask, std::string& error)
    {
        if (!m_Renderer) { error = "Renderer is unavailable"; return false; }
        if (!m_Renderer->SetPreviewImage(viewId, image, mip, layer, channelMask, error)) return false;
        m_PreviewRequests.erase(viewId);
        return true;
    }

    bool EngineRuntime::RequestEditorPick(const PickingRequest& request, uint64_t& ticket, std::string& error)
    {
        ticket = 0;
        if (!m_Renderer) { error = "Renderer is unavailable"; return false; }
        return m_Renderer->RequestPick(request, ticket, error);
    }

    bool EngineRuntime::PollEditorPick(uint64_t ticket, PickingResult& result)
    {
        return m_Renderer && m_Renderer->PollPick(ticket, result);
    }

    void EngineRuntime::CancelEditorPick(uint64_t ticket)
    {
        if (m_Renderer) m_Renderer->CancelPick(ticket);
    }

    bool EngineRuntime::GetGpuFrameMilliseconds(double& milliseconds) const
    {
        milliseconds = 0.0;
        if (!m_Renderer || !m_Renderer->GetStatistics().gpuDurationAvailable) return false;
        milliseconds = m_Renderer->GetStatistics().gpuMilliseconds;
        return true;
    }

    bool EngineRuntime::AddDebugBounds(const Aabb& bounds, const Math::Vec4& color, std::string& error, uint64_t surfaceId)
    {
        if (!m_Renderer) { error = "Renderer is unavailable"; return false; }
        const auto status = m_Renderer->GetDebugDraw().AddBounds(bounds, color);
        if (!status.IsSuccess()) { error = status.operation ? status.operation : "Invalid debug bounds"; return false; }
        m_Renderer->SetDebugSurface(surfaceId);
        error.clear();
        return true;
    }

    void EngineRuntime::RequestFrameAssets(const RenderFramePacket& packet)
    {
        if (!m_Assets || !packet.environment || !packet.environment->asset.HasValue()) return;
        const auto id = packet.environment->asset.id;
        if (m_Assets->GetStatus(id) != AssetStatus::Unloaded) return;
        std::string error;
        if (!m_Assets->Request(id, error)) {
            const auto previous = m_EnvironmentRequestErrors.find(id);
            if (previous == m_EnvironmentRequestErrors.end() || previous->second != error) {
                CAT_CORE_WARN("Environment request {0} failed: {1}", id.ToString(), error);
                m_EnvironmentRequestErrors.insert_or_assign(id, std::move(error));
            }
        } else m_EnvironmentRequestErrors.erase(id);
    }

    bool EngineRuntime::UpdateEditorPreviews(uint64_t sequence, std::string& error)
    {
        error.clear();
        for (auto it = m_PreviewRequests.begin(); it != m_PreviewRequests.end();) {
            Scene* scene = m_Scenes.GetScene(it->second.scene);
            if (!scene) {
                m_Renderer->ReleasePreviewSurface(it->first);
                it = m_PreviewRequests.erase(it);
                continue;
            }
            const RenderViewRequest request{ .id = it->first, .targetGeneration = it->second.generation,
                .viewport = { .width = it->second.extent.width, .height = it->second.extent.height } };
            RenderFramePacket packet;
            auto status = scene->CommitPending();
            if (status.IsSuccess()) status = TransformSystem::Update(*scene);
            if (status.IsSuccess()) status = RenderExtractSystem::Extract(*scene, sequence, { &request, 1 }, packet);
            if (!status.IsSuccess()) {
                // 候选预览失败保持上次显示帧；不能为了一个编辑窗口终止整个引擎。
                if (error.empty()) error = status.message;
            } else {
                packet.time = m_FrameTime;
                RequestFrameAssets(packet);
                std::string currentError;
                if (!m_Renderer->SetPreviewFrame(it->first, std::move(packet), currentError) && error.empty())
                    error = std::move(currentError);
            }
            ++it;
        }
        return error.empty();
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
        if (m_Assets) m_Assets->Pump();
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
            // Extract替换整个包后才附加展示帧时间；所有权随快照交给Renderer。
            packet.time = m_FrameTime;
            RequestFrameAssets(packet);
            if (const auto applied = m_Renderer->SetSceneFrame(std::move(packet)); !applied.IsSuccess()) return { WorldError::InvalidArgument, applied.message };
            ++m_FrameSequence;
            std::string previewError;
            if (!UpdateEditorPreviews(m_FrameSequence, previewError)) {
                if (previewError != m_LastPreviewError) CAT_CORE_WARN("Editor preview retained its previous frame: {0}", previewError);
                m_LastPreviewError = std::move(previewError);
            } else m_LastPreviewError.clear();
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
