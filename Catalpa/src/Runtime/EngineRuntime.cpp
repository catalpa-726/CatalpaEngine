#include "catpch.h"
#include "Runtime/RuntimeRegistration.h"
#include "Shaders/ShaderLibrary.h"
#include "Assets/BuiltinAssets.h"
#include "EngineRuntime.h"

#include "Log/Log.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Audio/AudioConfig.h"
#include "UI/UIRuntimeConfig.h"
#include "Core/Window.h"

#include "Renderer/Renderer.h"

#include "RHI/RHIBackendFactory.h"
#include "RHI/RHISwapchain.h"

#include "Platform/Vulkan/VulkanBackend.h"
#include "Scene/Scene.h"
#include "Camera/CameraProjection.h"
#include "ECS/SystemContext.h"
#include "Runtime/RuntimeServices.h"
#include "Runtime/Systems/TransformSystem.h"
#include "Runtime/Systems/RenderExtractSystem.h"
#include "Runtime/Systems/ScriptSystem.h"

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

    bool EngineRuntime::SetPlayScene(SceneHandle scene, bool paused, std::string& error)
    {
        if (scene.HasValue() && !m_Scenes.GetScene(scene)) {
            error = "Play scene is no longer available";
            return false;
        }
        if (m_PlayScene != scene) {
            m_PendingScriptScene.reset();
            // 停止Play后撤下旧HUD；视口会继续显示作者Scene，不能保留游戏UI快照。
            if (m_Renderer && m_PlayScene.HasValue()) {
                for (const auto& [id, request] : m_PreviewRequests)
                    if (request.scene == m_PlayScene) {
                        std::string ignored;
                        (void)m_Renderer->SetPreviewGameUI(id, nullptr, ignored);
                    }
            }
            m_GameUISystem.Shutdown();
            m_AudioExtract.Stop(m_Audio);
            m_Navigation.Stop();
            m_Terrain.Stop();
            m_Particles.Stop();
            m_Physics.Stop();
            m_Animation.Stop();
            m_Scripts.Stop(m_Scenes.GetScene(m_PlayScene));
            m_Behaviors.Stop();
            m_LastScriptDiagnostic.clear();
            m_BehaviorDiagnostics.clear();
            m_PlayScene = scene;
            m_Clock.Reset();
            m_Input.ResetGameplayContext();
        }
        m_PlayPaused = paused;
        m_Clock.SetPaused(m_EditorViewportActive && (!scene.HasValue() || paused));
        error.clear();
        return true;
    }

    bool EngineRuntime::StepPlay(std::string& error)
    {
        if (!m_PlayScene.HasValue() || !m_PlayPaused) {
            error = "Single step requires a paused Play session";
            return false;
        }
        const auto status = m_Clock.RequestSingleStep();
        error = status.IsSuccess() ? "" : status.message;
        return status.IsSuccess();
    }

    bool EngineRuntime::StartPackage(const std::filesystem::path& path, std::string& error) {
        if (!m_Assets || m_SimulationStopped) {
            error = "Runtime is not ready";
            return false;
        }
        if (m_PackageSession.State() != PackageSessionState::Idle || m_PackageStopping) {
            error = "Busy: another package session is active or retiring";
            return false;
        }
        std::shared_ptr<const PackageReader> package;
        const auto budget = m_Assets->GetBudgetStats();
        PackageLimits limits;
        limits.maxPackageBytes = std::min(limits.maxPackageBytes, budget.limit - budget.used);
        limits.maxAssetBytes = 96 * 1024 * 1024;
        if (!PackageReader::Open(path, package, error, limits))
            return false;
        m_PrePackageScriptScene = m_StandaloneScriptScene;
        m_StandaloneScriptScene = {};
        if (m_PackageSession.Start(std::move(package), *m_Assets, m_Scenes, error))
            return true;
        if (m_PackageSession.State() == PackageSessionState::Idle) {
            m_StandaloneScriptScene = m_PrePackageScriptScene;
            m_PrePackageScriptScene = {};
        } else
            m_PackageStopping = true;
        return false;
    }

    bool EngineRuntime::StopPackage(std::string& error) {
        if (!m_Assets) {
            error = "Runtime assets are unavailable";
            return false;
        }
        if (m_PackageSession.State() == PackageSessionState::Idle && !m_PackageStopping) {
            error.clear();
            return true;
        }
        m_PackageStopping = true;
        m_PendingScriptScene.reset();
        auto* scene = m_Scenes.GetScene(m_StandaloneScriptScene.HasValue() ? m_StandaloneScriptScene
                                                                           : m_PackageSession.Scene());
        m_Scripts.Stop(scene);
        m_Behaviors.Stop();
        m_GameUISystem.Shutdown();
        m_Terrain.Stop();
        m_Navigation.Stop();
        m_Physics.Stop();
        m_Particles.Stop();
        m_Animation.Stop();
        if (m_Window)
            m_Window->ClearTextInputTarget(0x4341545549544558ull);
        for (const auto& acknowledgement : m_Audio.TakeAcknowledgements())
            if (!acknowledgement.success)
                CAT_CORE_WARN("Retiring audio: {0}", acknowledgement.error);
        const auto packageScene = m_PackageSession.Scene();
        for (auto it = m_PreviewRequests.begin(); it != m_PreviewRequests.end();) {
            if (it->second.scene == packageScene || it->second.scene == m_StandaloneScriptScene) {
                if (m_Renderer)
                    m_Renderer->ReleasePreviewSurface(it->first);
                it = m_PreviewRequests.erase(it);
            } else
                ++it;
        }
        for (auto handle : {packageScene, m_StandaloneScriptScene})
            if (auto* current = m_Scenes.GetScene(handle))
                m_TerrainPreviews.erase(current->GetWorld().GetIdentity());
        if (m_Renderer) {
            m_Renderer->SetGameUI(nullptr);
            m_Renderer->ClearSceneFrame();
        }
        // 队列满或流送仍在退役时不卸载World/解挂包；下个展示帧继续处理ack并重试。
        if (!m_AudioExtract.StopAndDrain(m_Audio, error))
            return false;
        if (m_StandaloneScriptScene.HasValue()) {
            const auto unloaded = m_Scenes.Unload(m_StandaloneScriptScene);
            if (!unloaded.IsSuccess()) {
                error = unloaded.message;
                return false;
            }
            m_StandaloneScriptScene = {};
        }
        if (!m_PackageSession.Stop(*m_Assets, m_Scenes, error))
            return false;
        m_StandaloneScriptScene = m_PrePackageScriptScene;
        m_PrePackageScriptScene = {};
        m_PackageStopping = false;
        m_Clock.Reset();
        m_Input.ResetGameplayContext();
        error.clear();
        return true;
    }

    EngineRuntime::~EngineRuntime() { Shutdown(); }

    RHIStatus EngineRuntime::Init(Window& window, const RHIBackendDesc& backendDesc)
    {
        // 重复初始化不得先重建资产服务或拆掉仍在使用的运行时。
        if (m_Device || m_Assets)
            return { .code = RHIErrorCode::InvalidState, .operation = "EngineRuntime::Init/AlreadyInitialized" };
        m_Window = &window;
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
        m_Renderer->SetJobs(&m_Jobs);
        if (m_EditorViewportActive)
            m_Renderer->SetMainScenePresentation(MainScenePresentation::OffscreenOnly);
        m_SimulationStopped = false;
        m_FrameSequence = 0;
        m_EditorPreviewSeconds = 0;
        m_InputSceneIdentity = 0;
        m_Clock.Reset();
        m_LastSwapchainDesc = desc;
        return {};
    }

    void EngineRuntime::Shutdown()
    {
        if (m_Window)
            m_Window->ClearTextInputTarget(0);
        StopSimulation();
        m_PreviewRequests.clear();
        m_LastPreviewError.clear();
        m_LastSceneCameraError.clear();
        m_EditorViewportActive = false;
        m_EnvironmentRequestErrors.clear();
        m_GameUISystem.Shutdown();
        m_AudioExtract.Stop(m_Audio);
        m_Audio.Shutdown();
        m_Navigation.Stop();
        m_Terrain.Stop();
        m_TerrainPreviews.clear();
        m_Particles.Stop();
        m_Physics.Stop();
        m_Animation.Stop();
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

        m_PackageSession.ResetAfterShutdown();
        m_PackageStopping = false;
        m_PrePackageScriptScene = {};
        m_StandaloneScriptScene = {};
        m_PendingScriptScene.reset();
        m_GameUIInputRouter = UIInputRouter{};
        m_UISeconds = 0;
        m_Renderer.reset();
        m_Swapchain.reset();
        m_Device.reset();
        m_Assets.reset();
        m_LastSwapchainDesc = {};
        m_SwapchainDirty = true;
        m_Window = nullptr;
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
        m_Scripts.BindJobs(m_Jobs);
        AudioServiceConfig audioConfig;
        UIRuntimeConfig uiConfig;
        VirtualFileSystem configs;
        if (!configs.MountReadOnly("runtime-config", m_ResourceRoot / "Config", 0, error))
            return false;
        const auto readConfig = [&](std::string_view name, std::string& text) {
            VirtualPath path;
            std::vector<std::byte> bytes;
            if (!VirtualPath::Parse("runtime-config:/" + std::string(name), path, error) ||
                !configs.ReadFile(path, 65536, bytes, error))
                return false;
            text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        };
        std::string configText;
        if (!readConfig("AudioDefaults.json", configText) ||
            !ReadAudioConfig(configText, audioConfig, error) ||
            !readConfig("PhysicsDefaults.json", configText) ||
            !m_Physics.ConfigureFromJson(configText, error) ||
            !readConfig("UIRuntimeDefaults.json", configText) ||
            !ReadUIRuntimeConfig(configText, uiConfig, error) || !m_GameUISystem.Configure(uiConfig, error))
            return false;
        m_Audio.BindJobs(m_Jobs);
        std::string audioDiagnostic;
        if (!m_Audio.Initialize(audioConfig, audioDiagnostic)) {
            error = "Cannot initialize audio service: " + audioDiagnostic;
            return false;
        }
        if (!audioDiagnostic.empty())
            CAT_CORE_WARN("Audio: {0}", audioDiagnostic);
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
        for (const auto name :
             {"Pbr.slang", "Unlit.slang", "Depth.slang", "Shadow.slang", "ImGui.slang", "Skybox.slang",
              "Tonemap.slang", "Present.slang", "Picking.slang", "DebugDraw.slang", "GameUI.slang",
              "Skinning.slang", "DepthPyramid.slang", "LightCulling.slang", "GpuCulling.slang"}) {
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
        return StorePreviewRequest(scene, viewId, extent, std::nullopt, error);
    }

    bool EngineRuntime::SetEditorCameraPreview(SceneHandle scene, uint64_t viewId, Extent2D extent,
        const Math::TransformTRS& pose, const CameraLens& lens, uint64_t historyRevision, std::string& error)
    {
        RenderView view;
        view.id = viewId;
        view.independentCamera = true;
        view.historyRevision = historyRevision;
        view.viewport = { .width = extent.width, .height = extent.height };
        view.lens = lens;
        Math::Mat4 world;
        if (const auto status = Math::ComposeTRS(pose, world); !status.IsSuccess()) {
            error = status.message;
            return false;
        }
        if (const auto status = BuildCameraMatrices(world, lens, view.viewport, view.matrices); !status.IsSuccess()) {
            error = status.message;
            return false;
        }
        if (const auto status = ValidateRenderView(view); !status.IsSuccess()) {
            error = status.message;
            return false;
        }
        return StorePreviewRequest(scene, viewId, extent, std::move(view), error);
    }

    void EngineRuntime::SetEditorViewportActive(bool active)
    {
        if (active == m_EditorViewportActive) return;
        if (active) m_StandalonePaused = m_Clock.IsPaused();
        else m_Clock.SetPaused(m_StandalonePaused);
        m_EditorViewportActive = active;
        m_Input.ResetGameplayContext();
        if (m_Renderer)
            m_Renderer->SetMainScenePresentation(active ? MainScenePresentation::OffscreenOnly : MainScenePresentation::MainWindow);
    }

    bool EngineRuntime::StorePreviewRequest(SceneHandle scene, uint64_t viewId, Extent2D extent,
        std::optional<RenderView> independentView, std::string& error)
    {
        if (!m_Renderer || !m_Scenes.GetScene(scene) || viewId == 0 || extent.IsEmpty()) {
            error = "Preview requires a live scene, nonzero view ID and nonempty extent";
            return false;
        }
        const auto& caps = m_Device->GetCapabilities();
        if (extent.width > 8192 || extent.height > 8192 ||
            extent.width > caps.maxRenderTargetExtent.width || extent.height > caps.maxRenderTargetExtent.height) {
            error = "Preview extent exceeds render-target limits";
            return false;
        }
        const auto previous = m_PreviewRequests.find(viewId);
        if (previous != m_PreviewRequests.end() && previous->second.scene == scene && previous->second.extent == extent
            && previous->second.independentView.has_value() == independentView.has_value()) {
            // 位姿改变只更新相机值，不重新分配 GPU 目标，也不让已显示帧的点击失去身份。
            if (independentView) independentView->targetGeneration = previous->second.generation;
            previous->second.independentView = std::move(independentView);
            error.clear();
            return true;
        }
        if (m_NextPreviewGeneration == std::numeric_limits<uint64_t>::max()) {
            error = "Preview generation exhausted";
            return false;
        }
        try {
            if (independentView) independentView->targetGeneration = m_NextPreviewGeneration;
            m_PreviewRequests.insert_or_assign(viewId, PreviewRequest{ scene, extent, m_NextPreviewGeneration, std::move(independentView) });
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
    bool EngineRuntime::GetManagedGpuPayloadBytes(uint64_t& used, uint64_t& budget) const {
        used = budget = 0;
        if (!m_Renderer)
            return false;
        const auto& stats = m_Renderer->GetStatistics();
        used = stats.managedPayloadBytes;
        budget = stats.managedPayloadBudget;
        return budget != 0;
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

    bool EngineRuntime::UpdateEditorPreviews(uint64_t sequence, bool primaryFrameFresh, std::string& error)
    {
        error.clear();
        std::set<uint64_t> animatedScenes;
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
            const auto& independent = it->second.independentView;
            const auto& primary = m_Renderer->GetSceneFrame();
            WorldStatus status;
            if (primaryFrameFresh && independent && scene == m_Scenes.GetActive() && primary.sequence == sequence
                && primary.sceneIdentity == scene->GetWorld().GetIdentity()) {
                // 复用同一安全点的场景快照；编辑镜头不会写回相机组件或重新运行玩法。
                packet = primary;
                std::erase_if(packet.instances, [](const auto& instance) { return instance.id.part != 0; });
                std::erase_if(packet.transforms,
                              [](const auto& transform) { return transform.id.part != 0; });
                packet.views.clear();
            } else {
                // 主帧可能因场景相机无效而沿用旧快照；独立镜头应重新提取当前Scene，
                // 不能把旧网格/变换与本轮的蒙皮、粒子和地形混合。
                // 独立预览先接收作者变更，再推进动画，最后用本帧世界矩阵发射粒子。
                // 新建 Scene 尚无有效 worldMatrix，不能让首次 Tick 失败阻断后续 Transform。
                status = scene->CommitPending();
                if (status.IsSuccess()) status = TransformSystem::Update(*scene);
                if (status.IsSuccess() && scene != m_Scenes.GetActive() &&
                    animatedScenes.insert(scene->GetWorld().GetIdentity()).second) {
                    std::string animationError;
                    if (!m_Animation.Tick(*scene, *m_Assets, std::min(m_FrameTime.realDeltaSeconds, 1.0),
                                          animationError)) {
                        if (error.empty())
                            error = animationError;
                        ++it;
                        continue;
                    }
                    // root motion 只排结构命令；粒子与提取必须消费提交后的同一姿态。
                    status = scene->CommitPending();
                    if (status.IsSuccess())
                        status = TransformSystem::Update(*scene);
                    if (status.IsSuccess() &&
                        !m_Particles.Tick(*scene, *m_Assets, std::min(m_FrameTime.realDeltaSeconds, .25),
                                          animationError)) {
                        if (error.empty())
                            error = animationError;
                        ++it;
                        continue;
                    }
                }
                const auto views = independent ? std::span<const RenderViewRequest>{}
                    : std::span<const RenderViewRequest>{ &request, 1 };
                if (status.IsSuccess()) status = RenderExtractSystem::Extract(*scene, sequence, views, packet);
            }
            if (!status.IsSuccess()) {
                // 候选预览失败保持上次显示帧；不能为了一个编辑窗口终止整个引擎。
                if (error.empty()) error = status.message;
            } else {
                if (independent) packet.views.push_back(*independent);
                packet.time = m_FrameTime;
                // 编辑/资产预览可播放材质 Time，而作者 World 仍不执行玩法。
                // Play 视口继续使用权威模拟时间，暂停和单步不能被墙钟绕过。
                if (m_EditorViewportActive && (!m_PlayScene.HasValue() || it->second.scene != m_PlayScene))
                    packet.time.simulationSeconds = m_EditorPreviewSeconds;
                AppendAnimationFrame(*scene, packet);
                std::string dynamicError;
                if (!AppendDynamicFrame(*scene, packet, scene != m_Scenes.GetActive(), dynamicError)) {
                    if (error.empty())
                        error = dynamicError;
                    ++it;
                    continue;
                }
                RequestFrameAssets(packet);
                std::string currentError;
                if (!m_Renderer->SetPreviewFrame(it->first, std::move(packet), currentError) && error.empty())
                    error = std::move(currentError);
                if (m_PlayScene.HasValue() && it->second.scene == m_PlayScene)
                    (void)m_Renderer->SetPreviewGameUI(it->first, m_GameUISystem.DrawData(), currentError);
            }
            ++it;
        }
        std::set<uint64_t> liveWorlds;
        if (const auto* active = m_Scenes.GetActive())
            liveWorlds.insert(active->GetWorld().GetIdentity());
        for (const auto& [id, request] : m_PreviewRequests)
            if (const auto* scene = m_Scenes.GetScene(request.scene))
                liveWorlds.insert(scene->GetWorld().GetIdentity());
        for (auto iterator = m_TerrainPreviews.begin(); iterator != m_TerrainPreviews.end();) {
            if (liveWorlds.contains(iterator->first)) {
                ++iterator;
                continue;
            }
            m_Animation.ReleaseWorld(iterator->first);
            m_Particles.ReleaseWorld(iterator->first);
            iterator = m_TerrainPreviews.erase(iterator);
        }
        return error.empty();
    }

    void EngineRuntime::StopSimulation() noexcept
    {
        m_Scripts.Stop(m_PlayScene.HasValue() ? m_Scenes.GetScene(m_PlayScene) : m_Scenes.GetActive());
        m_Behaviors.Stop();
        m_GameUISystem.Shutdown();
        m_AudioExtract.Stop(m_Audio);
        m_Navigation.Stop();
        m_Terrain.Stop();
        m_Particles.Stop();
        m_Physics.Stop();
        m_Animation.Stop();
        m_PlayScene = {};
        m_PendingScriptScene.reset();
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
                if (!services.extractSceneCamera)
                    return RenderExtractSystem::Extract(context.scene, services.renderFrame->sequence,
                        std::span<const RenderViewRequest>{}, *services.renderFrame);
                return RenderExtractSystem::Extract(context.scene, services.renderFrame->sequence,
                    services.targetWidth, services.targetHeight, *services.renderFrame);
            };
            if (const auto status = systems.Add(std::move(extract)); !status.IsSuccess()) return status;
        }
        return systems.Compile();
    }

    WorldStatus EngineRuntime::UpdateSimulation(double realSeconds, InputCapture capture, uint32_t width, uint32_t height)
    {
        if (m_Assets) {
            m_Assets->Pump();
            if (m_PackageStopping) {
                std::string stopError;
                if (!StopPackage(stopError)) {
                    if (!stopError.starts_with("Busy") && !stopError.empty())
                        CAT_CORE_ERROR("Package retirement: {0}", stopError);
                    return {}; // 已停止旧世界，不在Busy重试期间恢复其玩法。
                }
            }
            if (!m_EditorViewportActive) {
                SceneHandle candidate;
                std::string switchError;
                if (PollScriptSceneSwitch(candidate, switchError)) {
                    const auto previous = m_StandaloneScriptScene;
                    if (auto* old = m_Scenes.GetActive())
                        m_Scripts.Stop(old);
                    m_Terrain.Stop();
                    m_Navigation.Stop();
                    m_Physics.Stop();
                    m_Animation.Stop();
                    m_Particles.Stop();
                    m_AudioExtract.Stop(m_Audio);
                    m_GameUISystem.Shutdown();
                    const auto status = m_Scenes.SetActive(candidate);
                    if (!status.IsSuccess()) {
                        (void)m_Scenes.Unload(candidate);
                        return status;
                    }
                    m_StandaloneScriptScene = candidate;
                    if (previous.HasValue())
                        (void)m_Scenes.Unload(previous);
                    m_Clock.Reset();
                    m_Input.ResetGameplayContext();
                } else if (!switchError.empty())
                    CAT_CORE_ERROR("Script scene switch: {0}", switchError);
            }
            std::string packageError;
            if (m_PackageSession.State() == PackageSessionState::Loading &&
                m_Assets->GetStatus(m_PackageSession.EntryScene()) == AssetStatus::Ready) {
                if (auto* previous = m_Scenes.GetActive())
                    m_Scripts.Stop(previous);
                m_Behaviors.Stop();
                m_Terrain.Stop();
                m_Navigation.Stop();
                m_Physics.Stop();
                m_Animation.Stop();
                m_Particles.Stop();
                m_AudioExtract.Stop(m_Audio);
                m_GameUISystem.Shutdown();
                if (m_Renderer) {
                    m_Renderer->SetGameUI(nullptr);
                    m_Renderer->ClearSceneFrame();
                }
                m_Clock.Reset();
                m_Input.ResetGameplayContext();
            }
            if (!m_PackageSession.Pump(*m_Assets, m_Scenes, packageError)) {
                CAT_CORE_ERROR("Package session failed: {0}", packageError);
                return {WorldError::InvalidArgument, "Package session failed; see diagnostic"};
            }
        }
        if (m_SimulationStopped || !m_Renderer) return { WorldError::Busy, "Simulation is stopped or renderer is unavailable" };
        if (m_FrameSequence == std::numeric_limits<uint64_t>::max()) return { WorldError::CapacityExceeded, "Presentation frame sequence exhausted" };
        if (!std::isfinite(realSeconds) || realSeconds < 0.0) return { WorldError::InvalidArgument, "Real frame duration must be finite and nonnegative" };
        try {
            if (m_EditorViewportActive) {
                const double nextPreviewSeconds = m_EditorPreviewSeconds + realSeconds;
                if (!std::isfinite(nextPreviewSeconds) || nextPreviewSeconds > std::numeric_limits<float>::max())
                    return { WorldError::InvalidArgument, "Editor preview time exceeds the shader time range" };
                // 保留累计秒数，由统一帧打包拆成 epoch/phase，图 Time 不在 4096 秒处归零。
                m_EditorPreviewSeconds = nextPreviewSeconds;
            }
            const Scene* active = m_Scenes.GetActive();
            const uint64_t activeIdentity = active ? active->GetWorld().GetIdentity() : 0;
            if (m_PlayScene.HasValue() && m_Scenes.GetActiveHandle() != m_PlayScene) {
                std::string ignored;
                (void)SetPlayScene({}, false, ignored);
            }
            if (m_EditorViewportActive)
                m_Clock.SetPaused(!m_PlayScene.HasValue() || m_PlayPaused);
            if (activeIdentity != m_InputSceneIdentity) {
                m_Input.ResetGameplayContext();
                m_LastSceneCameraError.clear();
                m_LastScriptDiagnostic.clear();
                m_BehaviorDiagnostics.clear();
                m_InputSceneIdentity = activeIdentity;
            }
            capture.gameplayActive = capture.gameplayActive && activeIdentity != 0 &&
                (!m_EditorViewportActive || m_PlayScene.HasValue());
            if (!m_EditorViewportActive)
                capture.keyboardCaptured = capture.keyboardCaptured || m_GameUISystem.HasFocus();
            const auto& actions = m_Input.BeginFrame(capture);
            if (const auto* pause = actions.Find("simulation.pause"); pause && pause->pressed && !m_EditorViewportActive) {
                m_Clock.SetPaused(!m_Clock.IsPaused());
                CAT_CORE_INFO("[S03.2] Simulation paused = {0}", m_Clock.IsPaused());
            }
            if (const auto* step = actions.Find("simulation.step"); step && step->pressed && !m_EditorViewportActive) {
                if (const auto status = m_Clock.RequestSingleStep(); !status.IsSuccess()) CAT_CORE_WARN("[S03.2] Single step rejected: {0}", status.message);
            }
            if (const auto status = m_Clock.Advance(realSeconds, m_FrameTime); !status.IsSuccess()) return { WorldError::InvalidArgument, status.message };
            if (m_FrameTime.droppedSeconds > 0.0) CAT_CORE_WARN("[S03.2] Dropped {0} simulation seconds at catch-up limit", m_FrameTime.droppedSeconds);
            RenderFramePacket packet;
            packet.sequence = m_FrameSequence + 1;
            RuntimeServices services{ &actions, width, height, &packet, !m_EditorViewportActive };
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
                    RuntimeServices tickServices{ &tickInput, width, height, &packet, !m_EditorViewportActive };
                    SystemContext context{ scene, tick, tickServices };
                    if ((!m_EditorViewportActive || m_PlayScene.HasValue()) && m_Assets) {
                        ScriptSystem::Update(m_Scripts, *m_Assets, context);
                        m_Behaviors.Tick(scene, *m_Assets, tick.fixedDeltaSeconds);
                        std::string diagnostics;
                        for (const auto& diagnostic : m_Scripts.Diagnostics()) {
                            if (!diagnostics.empty()) diagnostics += '\n';
                            diagnostics += diagnostic;
                        }
                        if (!diagnostics.empty() && diagnostics != m_LastScriptDiagnostic)
                            CAT_CORE_ERROR("Lua instances: {0}", diagnostics);
                        if (diagnostics.find("requires controlled shutdown") != std::string::npos)
                            return {WorldError::CommandFailed,
                                    "Script transaction could not roll back; controlled shutdown required"};
                        m_LastScriptDiagnostic = std::move(diagnostics);
                        for(const auto& snapshot:m_Behaviors.Snapshot(false)) if(!snapshot.error.empty() && m_BehaviorDiagnostics[snapshot.entity]!=snapshot.error) {
                            CAT_CORE_ERROR("Behavior entity {0}, node {1}: {2}",snapshot.entity.ToString(),snapshot.node.ToString(),snapshot.error);
                            m_BehaviorDiagnostics[snapshot.entity]=snapshot.error;
                        }
                    }
                    // 一个 tick 完整收束后才能进入下个 tick，离散事件不能被展示帧覆盖。
                    for (const auto phase :
                         {SystemPhase::Gameplay, SystemPhase::Animation, SystemPhase::Kinematic,
                          SystemPhase::Physics, SystemPhase::PostPhysics, SystemPhase::Transform}) {
                        if (const auto executed = systems.RunPhase(phase, context); !executed.IsSuccess())
                            return executed;
                        std::string domainError;
                        if (phase == SystemPhase::Animation &&
                            !m_Animation.Tick(scene, *m_Assets, tick.fixedDeltaSeconds, domainError)) {
                            CAT_CORE_ERROR("Animation: {0}", domainError);
                            return {WorldError::InvalidArgument, "Animation update failed"};
                        }
                        if (phase == SystemPhase::Kinematic &&
                            !m_Navigation.Tick(scene, *m_Assets, m_Jobs, tick.fixedDeltaSeconds,
                                               domainError)) {
                            CAT_CORE_ERROR("Navigation: {0}", domainError);
                            return {WorldError::InvalidArgument, "Navigation update failed"};
                        }
                        if (phase == SystemPhase::Transform &&
                            !m_Particles.Tick(scene, *m_Assets, tick.fixedDeltaSeconds, domainError)) {
                            CAT_CORE_ERROR("Particles: {0}", domainError);
                            return {WorldError::InvalidArgument, "Particle update failed"};
                        }
                        if (phase == SystemPhase::Physics) {
                            if (const auto committed = scene.CommitPending(); !committed.IsSuccess())
                                return committed;
                            if (!m_Physics.Tick(scene, *m_Assets, tick.fixedDeltaSeconds, tick.firstTick,
                                                domainError)) {
                                CAT_CORE_ERROR("Physics: {0}", domainError);
                                return {WorldError::InvalidArgument, "Physics update failed"};
                            }
                        }
                    }
                }
                std::string dynamicError;
                if (m_EditorViewportActive && !m_PlayScene.HasValue())
                    if (!m_Animation.Tick(scene, *m_Assets, 0, dynamicError))
                        CAT_CORE_WARN("Author pose: {0}", dynamicError);
                m_UISeconds += std::min(realSeconds, 1.0);
                const bool running = !m_EditorViewportActive || m_PlayScene.HasValue();
                uint32_t uiWidth = width, uiHeight = height;
                if (m_EditorViewportActive)
                    for (const auto& [id, request] : m_PreviewRequests)
                        if (request.scene == m_PlayScene) {
                            uiWidth = request.extent.width;
                            uiHeight = request.extent.height;
                            break;
                        }
                if (!m_AudioExtract.Update(running ? m_Scenes.GetActiveHandle() : SceneHandle{},
                                           running ? &scene : nullptr, *m_Assets, m_Audio, dynamicError))
                    CAT_CORE_WARN("Audio extraction: {0}", dynamicError);
                m_Audio.Update(std::min(realSeconds, 1.0));
                if (!m_GameUISystem.Update(running ? m_Scenes.GetActiveHandle() : SceneHandle{},
                                           running ? &scene : nullptr, *m_Assets, uiWidth, uiHeight,
                                           m_UISeconds, dynamicError))
                    CAT_CORE_WARN("Game UI: {0}", dynamicError);
                m_Renderer->SetGameUI(m_EditorViewportActive ? nullptr : m_GameUISystem.DrawData());
                if (!m_EditorViewportActive && m_Window) {
                    constexpr uint64_t textOwner = 0x4341545549544558ull;
                    auto target = m_GameUISystem.TextInput();
                    if (target.active && width && height) {
                        const float sx = float(m_Window->GetWidth()) / width;
                        const float sy = float(m_Window->GetHeight()) / height;
                        target.x *= sx;
                        target.width *= sx;
                        target.y *= sy;
                        target.height *= sy;
                        std::string imeError;
                        if (!m_Window->SetTextInputTarget(
                                textOwner, target,
                                [this](const TextComposition& composition) {
                                    UIInputEvent event;
                                    event.type = UIInputEvent::Type::Composition;
                                    event.composition = composition;
                                    bool consumed = false;
                                    std::string error;
                                    if (!m_GameUISystem.Input(event, consumed, error) && !error.empty())
                                        CAT_CORE_WARN("Game UI composition: {0}", error);
                                },
                                imeError) &&
                            !imeError.empty())
                            CAT_CORE_WARN("Game UI IME: {0}", imeError);
                    } else
                        m_Window->ClearTextInputTarget(textOwner);
                }
                for (const auto phase : {SystemPhase::Camera, SystemPhase::AudioExtract, SystemPhase::UI}) {
                    if (const auto presented = systems.RunPhase(phase, presentation); !presented.IsSuccess())
                        return presented;
                }
                ForwardDynamicEvents(scene);
                return systems.RunPhase(SystemPhase::Extract, presentation);
            });
            if (!active) {
                // 无活动场景仍要处理编辑器音频预览及 Stop ack，不能让离散命令永久留在队列。
                std::string audioRetirement;
                (void)m_AudioExtract.StopAndDrain(m_Audio, audioRetirement);
                m_GameUISystem.Shutdown();
                m_Renderer->SetGameUI(nullptr);
                m_Audio.Update(std::min(realSeconds, 1.0));
                for (const auto& ack : m_Audio.TakeAcknowledgements())
                    if (!ack.success)
                        CAT_CORE_WARN("Audio preview command: {0}", ack.error);
                if (m_Window)
                    m_Window->ClearTextInputTarget(0x4341545549544558ull);
            }
            if (status.code == WorldError::InvalidPresentationView) {
                // 保留同一场景的最近有效画面；新场景没有有效帧时只绘制工作区。
                // 展示序号继续推进，下一轮输入/模拟可修正相机；其他系统错误仍由宿主处理。
                if (m_LastSceneCameraError != status.message) {
                    CAT_CORE_WARN("Scene camera frame rejected: {0}. Waiting for a valid pose or lens.", status.message);
                    m_LastSceneCameraError = status.message;
                }
                const auto& previous = m_Renderer->GetSceneFrame();
                packet = previous.sceneIdentity == activeIdentity ? previous : RenderFramePacket{};
                packet.sceneIdentity = activeIdentity;
                packet.sequence = m_FrameSequence + 1;
            } else {
                if (!status.IsSuccess()) return status;
                if (!m_LastSceneCameraError.empty()) CAT_CORE_INFO("Scene camera recovered; publishing the current view.");
                m_LastSceneCameraError.clear();
                packet.time = m_FrameTime;
            }
            if (status.IsSuccess()) {
                if (auto* activeScene = m_Scenes.GetActive()) {
                    AppendAnimationFrame(*activeScene, packet);
                    std::string dynamicError;
                    if (!AppendDynamicFrame(*activeScene, packet, false, dynamicError))
                        CAT_CORE_WARN("Dynamic render snapshot: {0}", dynamicError);
                }
            }
            // 相机拒绝时packet已是完整旧快照；不能再追加同身份的粒子/地形或混入新蒙皮。
            RequestFrameAssets(packet);
            if (const auto applied = m_Renderer->SetSceneFrame(std::move(packet)); !applied.IsSuccess()) return { WorldError::InvalidArgument, applied.message };
            ++m_FrameSequence;
            std::string previewError;
            if (!UpdateEditorPreviews(m_FrameSequence, status.IsSuccess(), previewError)) {
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

namespace Catalpa {
    bool EngineRuntime::SetEditorGameUI(uint64_t id, std::shared_ptr<const UIDrawData> data,
                                        std::string& error) {
        if (!m_Renderer) {
            error = "Renderer is not initialized";
            return false;
        }
        return m_Renderer->SetPreviewGameUI(id, std::move(data), error);
    }
} // namespace Catalpa

namespace Catalpa {
    void EngineRuntime::AppendAnimationFrame(const Scene& scene, RenderFramePacket& packet) {
        packet.skinning.clear();
        for (const auto& skin : m_Animation.Snapshot(scene.GetWorld().GetIdentity())) {
            const auto id = skin.entity.resolved;
            const RenderInstanceId renderId{id.world, id.index, id.generation};
            auto found = std::find_if(packet.instances.begin(), packet.instances.end(),
                                      [&](const auto& value) { return value.id == renderId; });
            if (found == packet.instances.end())
                continue;
            Aabb bounds;
            if (!TransformBounds(skin.bounds, found->worldMatrix, bounds).IsSuccess())
                continue;
            found->worldBounds = bounds;
            packet.skinning.push_back({renderId, skin.binding, skin.palette});
        }
    }
    bool EngineRuntime::OnInputEvent(const Event& event) {
        float scaleX = 1, scaleY = 1;
        if (m_Window) {
            const auto [width, height] = m_Window->GetFramebufferSize();
            if (m_Window->GetWidth())
                scaleX = float(width) / m_Window->GetWidth();
            if (m_Window->GetHeight())
                scaleY = float(height) / m_Window->GetHeight();
        }
        UIInputEvent input;
        const bool translated = m_GameUIInputRouter.Translate(event, 0, 0, scaleX, scaleY, input);
        // Editor 在图像局部坐标路由。此处仍观察原始物理源，展示帧再按 Play 捕获策略隔离。
        if (m_EditorViewportActive) {
            m_Input.OnEvent(event);
            return false;
        }
        bool consumed = false;
        std::string error;
        if (translated && !m_GameUISystem.Input(input, consumed, error)) {
            consumed = m_GameUISystem.HasFocus();
            if (!error.empty())
                CAT_CORE_WARN("Game UI input: {0}", error);
        }
        // 按下与位移归 UI，释放始终更新物理状态；物理快照不被筛选，只有动作被消费。
        if (consumed)
            m_Input.OnCapturedEvent(event, m_GameUISystem.HasFocus());
        else
            m_Input.OnEvent(event);
        return consumed;
    }
} // namespace Catalpa
