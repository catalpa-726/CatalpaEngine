#include "catpch.h"
#include "Renderer/RendererInternal.h"
#include "Renderer/UI/ImGuiFrameData.h"
#include "Renderer/FrameScheduler.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Passes/PresentPass.h"
#include "Assets/AssetManager.h"
#include "Assets/BuiltinAssetIds.h"
#include "Log/Log.h"

namespace Catalpa {

    Renderer::Renderer(RHIDevice& device, RHISwapchain& swapchain, AssetManager& assets,
                       const RendererProfile& profile)
        : m_Device(device), m_Swapchain(swapchain), m_Assets(assets), m_Uploads(device),
          m_Geometry(device, m_Uploads, &m_Residency), m_Textures(device, m_Uploads, &m_Residency),
          m_Materials(device), m_Pipelines(device), m_Compute(device, assets),
          m_UI(device, assets, m_Textures, m_Pipelines), m_GameUI(device, m_Textures), m_Readbacks(device),
          m_Profile(profile), m_Main(std::make_unique<Surface>(device, m_Textures)),
          m_Queries(device.GetFrameCount()) {}

    Renderer::~Renderer()
    {
        // Runtime已停止提交并等待完成；先撤销未提交的上传，再归还各逻辑owner。
        m_Uploads.Shutdown();
        m_Previews.clear();
        m_Main.reset();
        m_Device.ReleaseSampler(m_EnvironmentSampler);
        m_Device.ReleaseSampler(m_ShadowSampler);
        m_Device.ReleaseSampler(m_NearestEnvironmentSampler);
        for (const auto& [flags, sampler] : m_MaterialSamplers) m_Device.ReleaseSampler(sampler);
    }

    Math::MathStatus Renderer::ResolveEnvironmentSnapshot(RenderFramePacket& packet, const RenderFramePacket& previous)
    {
        if (!packet.environment || !packet.environment->asset.HasValue()) return {};
        auto& environment = *packet.environment;
        if (environment.version.HasValue()) {
            if (environment.version.GetId() != environment.asset.id)
                return { Math::MathError::OutOfRange, "Environment fixed version disagrees with its logical identity" };
            return {};
        }
        std::string error;
        bool fallback = false;
        if (m_Assets.Acquire(environment.asset.id, environment.version, fallback, error) && !fallback) return {};
        environment.version = {};
        if (previous.environment && previous.environment->asset.id == environment.asset.id)
            environment.version = previous.environment->version;
        if (m_ReportedMaterialFailures.size() < 256 && m_ReportedMaterialFailures.emplace(environment.asset.id, 0).second)
            CAT_CORE_WARN("Environment {0} is not ready; retain its previous fixed version or use the disabled default environment.",
                environment.asset.id.ToString());
        return {};
    }

    Math::MathStatus Renderer::SetSceneFrame(RenderFramePacket packet)
    {
        if (const auto status = ResolveEnvironmentSnapshot(packet, m_Scene.GetFrame()); !status.IsSuccess()) return status;
        return m_Scene.Apply(std::move(packet));
    }

    void Renderer::ClearSceneFrame() noexcept
    {
        m_Scene.Clear();
        ClearMainSurface();
    }

    void Renderer::SetMainScenePresentation(MainScenePresentation presentation) noexcept
    {
        if (m_MainScenePresentation == presentation) return;
        m_MainScenePresentation = presentation;
        // 主快照继续供宿主读取；退休主窗口表现资源，不影响离屏显示帧、Picking或相机历史。
        // 切回主窗口时从最新快照重建，不能把此前隐藏的主相机当作连续展示历史。
        ClearMainSurface();
    }

    void Renderer::ClearMainSurface() noexcept
    {
        m_Main->uniforms.ClearHistory();
        m_Main->instances.clear();
        m_Main->environment.fill({});
        m_Main->environmentVersion = {};
        m_Main->targets.reset();
        m_Main->published = {};
        m_Main->skybox.reset();
        m_Main->tonemap.reset();
        m_Main->present.reset();
        m_Main->debug.reset();
    }

    RHIStatus Renderer::InitUI(std::span<const std::byte> pixels, Extent2D extent)
    {
        std::string error;
        if (extent.IsEmpty() || uint64_t(extent.width) * extent.height > SIZE_MAX / 4
            || pixels.size() != uint64_t(extent.width) * extent.height * 4)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer::InitUI" };
        ImageAsset image;
        image.image.width = extent.width;
        image.image.height = extent.height;
        image.image.format = PixelFormat::RGBA8_UNorm;
        image.image.alphaMode = ImageAlphaMode::Straight;
        image.image.pixels.assign(pixels.begin(), pixels.end());
        image.image.subresources.push_back({ 0, 0, 0, extent.width, extent.height, 1, 0,
            uint64_t(extent.width) * 4, pixels.size(), pixels.size() });
        image.sourceUri = "generated:imgui-font-atlas";
        const size_t resident = sizeof(ImageAsset) + image.sourceUri.capacity() + 1 + image.image.pixels.capacity()
            + image.image.subresources.capacity() * sizeof(ImageSubresource);
        if (!m_Assets.PublishBuiltin<ImageAsset>(BuiltinAssetIds::FontAtlasImage, 1,
            std::make_shared<const ImageAsset>(std::move(image)), resident, error))
            return { .code = RHIErrorCode::InvalidState, .operation = "Renderer::InitUI/publication" };
        AssetVersionRef<ImageAsset> version;
        bool fallback = false;
        if (!m_Assets.Acquire(BuiltinAssetIds::FontAtlasImage, version, fallback, error))
            return { .code = RHIErrorCode::InvalidState, .operation = "Renderer::InitUI/acquire" };
        return m_UI.InitFont(version);
    }

    void Renderer::OnCancelled() noexcept
    {
        m_Uploads.OnCancelled();
        m_Readbacks.OnCancelled();
        m_Main->uniforms.OnCancelled();
        for (auto& [id, surface] : m_Previews) surface->uniforms.OnCancelled();
    }

    bool Renderer::IsolatePreviewFailure(uint64_t surfaceId, Surface& surface, RHIStatus status)
    {
        surface.prepared = false;
        surface.uniforms.OnCancelled();
        if (status.code != RHIErrorCode::InvalidDescription && status.code != RHIErrorCode::InvalidState
            && status.code != RHIErrorCode::Unsupported) return false;

        const char* previous = surface.lastFailure.operation ? surface.lastFailure.operation : "";
        const char* operation = status.operation ? status.operation : "";
        // 只隔离Renderer明确报告的候选状态；后端/缓存所有权及Graph的InvalidState仍是全帧错误。
        if (status.code == RHIErrorCode::InvalidState && !std::string_view(operation).starts_with("Renderer")) return false;
        if (surface.lastFailure.code != status.code || surface.lastFailure.nativeCode != status.nativeCode
            || std::strcmp(previous, operation) != 0)
            CAT_CORE_WARN("Preview surface {0} preparation failed at {1} (RHI code {2}); retaining its last submitted image.",
                surfaceId, operation, static_cast<int>(status.code));
        surface.lastFailure = status;
        return true;
    }

    void Renderer::OnSubmitted(RHICompletionPoint completion,
                               std::span<const RHICompletionPoint> points) noexcept {
        m_Uploads.OnSubmitted(completion);
        m_Readbacks.OnAccepted(points);
        m_Geometry.OnSubmitted(points);
        m_Textures.OnSubmitted(points);
        if (m_MainScenePresentation == MainScenePresentation::MainWindow)
            m_Main->uniforms.OnSubmitted();
        for (auto& [id, surface] : m_Previews) {
            if (surface->imageOnly || !surface->prepared || !surface->targets) continue;
            surface->uniforms.OnSubmitted();
            const auto& packet = surface->scene.GetFrame();
            const auto* view = surface->uniforms.GetView(m_FrameSlot);
            if (!view) continue;
            if (surface->published.textureToken != surface->token)
                m_UI.ReleaseTexture(surface->published.textureToken);
            surface->published = { id, surface->targets->generation, packet.sceneIdentity, packet.sequence,
                surface->targets->extent, surface->token, *view };
            surface->publishedTargets = surface->targets;
            surface->publishedIdentities.swap(surface->pendingIdentities);
        }
        auto& query = m_Queries[m_FrameSlot];
        if (query.pool) query.completion = completion;
        m_Statistics.submittedSequence = m_Scene.GetFrame().sequence;
        m_Statistics.previewCount = static_cast<uint32_t>(m_Previews.size());
    }

    RHIStatus Renderer::BuildFrameGraph(RenderGraph& graph, const RHIAcquireResult& acquired, const ImGuiFrameData* uiFrame)
    {
        // 先复制上次显示帧的Picking像素，再重绘同一目标。WAR边界由图保证，身份表来自显示帧。
        if (const auto status = m_Readbacks.AddPasses(graph); !status.IsSuccess()) return status;
        if (const auto status = m_Uploads.AddPasses(graph); !status.IsSuccess()) return status;
        RGTexture color;
        if (const auto status = graph.ImportTexture(acquired.view, color); !status.IsSuccess()) return status;
        const auto extent = m_Swapchain.GetInfo().extent;
        if (const auto status = graph.AddClearPass("Main window clear", color, m_Profile.clearColor); !status.IsSuccess()) return status;

        const auto& packet = m_Scene.GetFrame();
        if (m_MainScenePresentation == MainScenePresentation::MainWindow && !packet.views.empty()) {
            if (const auto status = AddSurfacePasses(graph, *m_Main, packet, false); !status.IsSuccess()) return status;
            RGTexture display;
            if (const auto status = graph.ImportTexture(m_Main->targets->display.view, display); !status.IsSuccess()) return status;
            RGIndexedDrawDesc draw;
            draw.color = color;
            draw.pipeline = m_Main->present->pipeline;
            draw.viewport = { .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
            draw.scissor.extent = extent;
            draw.sampledTextures.push_back({ display, m_EnvironmentSampler, 0, 1 });
            if (const auto status = PresentPass::Add(graph, std::move(draw)); !status.IsSuccess()) return status;
        }
        for (auto& [id, surface] : m_Previews)
            if (!surface->imageOnly && surface->prepared && !surface->scene.GetFrame().views.empty())
                if (const auto status = AddSurfacePasses(graph, *surface, surface->scene.GetFrame(), true); !status.IsSuccess()) return status;

        if (const auto status = m_GameUI.Add(graph, color, extent, m_FrameSlot); !status.IsSuccess())
            return status;
        if (uiFrame)
            if (const auto status = m_UI.AddPasses(graph, color, extent, m_FrameSlot, *uiFrame); !status.IsSuccess()) return status;
        if (m_Queries[m_FrameSlot].pool)
            if (const auto status = graph.SetTimestampQueries(m_Queries[m_FrameSlot].pool); !status.IsSuccess()) return status;
        return graph.ExportPresent(color);
    }

    RHIStatus Renderer::CancelFrame(RHIAcquireResult& acquired, RHIStatus failure)
    {
        OnCancelled();
        if (failure.code == RHIErrorCode::DeviceLost) return failure;
        RHICompletionPoint completion;
        const auto status = m_Swapchain.CancelAcquire(acquired, completion);
        if (!status.IsSuccess()) {
            if (status.code == RHIErrorCode::DeviceLost) return status;
            CAT_CORE_ASSERT(false, "Cannot safely finish an acquired frame after cancellation failed!");
            std::abort();
        }
        m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
        return failure;
    }

    RHIStatus Renderer::Render(const ImGuiFrameData* uiFrame)
    {
        struct DebugFrameReset {
            DebugDraw& draw;
            ~DebugFrameReset() { draw.Clear(); }
        } debugFrameReset{ m_Debug };
        OnCancelled();
        if (const auto changes = RefreshAssetChanges(); !changes.IsSuccess())
            return changes;
        if (m_Swapchain.GetInfo().state != RHISwapchainState::Ready) return {};
        RHIAcquireResult acquired;
        bool submitted = false;
        try {
            constexpr uint64_t timeoutNanoseconds = 16'000'000;
            RHIFrameResources frame;
            bool ready = false;
            auto status = m_Device.PrepareFrame(m_FrameSlot, timeoutNanoseconds, frame, ready);
            if (!status.IsSuccess() || !ready) return status;

            // 只有可提交的帧槽才创建resize候选，GPU繁忙时不持续积累离屏附件。
            const auto& packet = m_Scene.GetFrame();
            const bool renderMain = m_MainScenePresentation == MainScenePresentation::MainWindow;
            const bool hasSceneSurface = (renderMain && !packet.views.empty())
                || std::any_of(m_Previews.begin(), m_Previews.end(), [](const auto& entry) {
                    return !entry.second->imageOnly && !entry.second->scene.GetFrame().views.empty();
                });
            // UI与纹理预览无需场景默认图、材质或HDR附件，仍通过下方上传/交换链图提交。
            if (hasSceneSurface)
                if (const auto status = EnsureDefaultTextures(); !status.IsSuccess()) return status;
            if (renderMain && !packet.views.empty())
                if (const auto status = EnsureSurfaceResources(*m_Main, packet, m_Swapchain.GetInfo().extent,
                    packet.views.front().targetGeneration, false); !status.IsSuccess()) return status;
            for (auto& [id, surface] : m_Previews) {
                surface->prepared = false;
                const auto& preview = surface->scene.GetFrame();
                if (surface->imageOnly || preview.views.empty()) continue;
                const auto& view = preview.views.front();
                if (const auto status = EnsureSurfaceResources(*surface, preview,
                    { view.viewport.width, view.viewport.height }, view.targetGeneration, true); !status.IsSuccess()) {
                    if (!IsolatePreviewFailure(id, *surface, status)) return status;
                    continue;
                }
                surface->prepared = true;
            }

            // 帧槽已完成才读取旧query/覆写对应UBO；available与提交完成分别核对。
            auto& query = m_Queries[m_FrameSlot];
            if (!m_QuerySupportChecked) {
                status = m_Device.CreateQueryPool({}, query.pool);
                m_QuerySupportChecked = true;
                m_Statistics.timestampSupported = status.IsSuccess();
                if (status.code == RHIErrorCode::Unsupported)
                    CAT_CORE_INFO("GPU timestamp queries are unavailable on the active graphics queue; frame GPU timing is disabled.");
                if (!status.IsSuccess() && status.code != RHIErrorCode::Unsupported) return status;
            } else if (m_Statistics.timestampSupported && !query.pool) {
                if (const auto queryStatus = m_Device.CreateQueryPool({}, query.pool); !queryStatus.IsSuccess()) return queryStatus;
            }
            if (query.pool && query.completion.HasValue()) {
                bool complete = false;
                if (const auto queryStatus = m_Device.GetGraphicsQueue().IsComplete(query.completion, complete); !queryStatus.IsSuccess()) return queryStatus;
                std::array<RHIQueryResult, 2> values{};
                if (complete) {
                    if (const auto queryStatus = query.pool->GetResults(0, values); !queryStatus.IsSuccess()) return queryStatus;
                }
                if (complete && values[0].available && values[1].available) {
                    const uint32_t bits = query.pool->GetTimestampValidBits();
                    const float period = query.pool->GetTimestampPeriodNanoseconds();
                    if (bits == 0 || bits > 64 || !std::isfinite(period) || period <= 0)
                        return { .code = RHIErrorCode::InvalidState, .operation = "Renderer/invalid timestamp capability" };
                    const uint64_t mask = bits >= 64 ? UINT64_MAX : (uint64_t{ 1 } << bits) - 1;
                    m_Statistics.gpuMilliseconds = double((values[1].value - values[0].value) & mask)
                        * period * 1.0e-6;
                    m_Statistics.gpuDurationAvailable = true;
                }
            }
            if (renderMain && !packet.views.empty())
                if (const auto prepare = PrepareSurface(*m_Main, packet, m_Swapchain.GetInfo().extent); !prepare.IsSuccess()) return prepare;
            if (renderMain && packet.views.empty() && packet.sequence != 0)
                if (const auto prepare = m_Main->uniforms.Prepare(m_FrameSlot, packet, m_Profile,
                    m_Swapchain.GetInfo().extent); !prepare.IsSuccess()) return prepare;
            for (auto& [id, surface] : m_Previews) {
                if (surface->imageOnly || !surface->prepared || surface->scene.GetFrame().views.empty()) continue;
                if (const auto prepare = PrepareSurface(*surface, surface->scene.GetFrame(), surface->targets->extent); !prepare.IsSuccess()) {
                    if (!IsolatePreviewFailure(id, *surface, prepare)) return prepare;
                    continue;
                }
                if (!surface->lastFailure.IsSuccess())
                    CAT_CORE_INFO("Preview surface {0} preparation recovered.", id);
                surface->lastFailure = {};
            }
            if ((m_GameUIDraw && !m_GameUIDraw->batches.empty()) ||
                std::any_of(m_Previews.begin(), m_Previews.end(), [](const auto& entry) {
                    return entry.second->gameUIDraw && !entry.second->gameUIDraw->batches.empty();
                })) {
                constexpr std::array<VertexAttribute, 3> attributes{{{0, VertexFormat::Float2, 0},
                                                                     {1, VertexFormat::Float2, 8},
                                                                     {2, VertexFormat::Float4, 16}}};
                if (const auto result = AcquireBuiltinPipeline(
                        "GameUI.slang", m_Swapchain.GetInfo().format, PixelFormat::Unknown, {},
                        {sizeof(UIDrawVertex), attributes}, m_GameUIPipeline, RHIBlendMode::Premultiplied);
                    !result.IsSuccess())
                    return result;
            }
            if (const auto result = m_GameUI.Prepare(m_FrameSlot, m_GameUIDraw ? *m_GameUIDraw : UIDrawData{},
                                                     m_GameUIPipeline);
                !result.IsSuccess())
                return result;
            for (auto& [id, surface] : m_Previews)
                if (surface->prepared) {
                    if (!surface->gameUIDraw || surface->gameUIDraw->batches.empty()) {
                        if (const auto result = surface->gameUI.Prepare(m_FrameSlot, UIDrawData{}, nullptr);
                            !result.IsSuccess())
                            return result;
                        continue;
                    }
                    constexpr std::array<VertexAttribute, 3> attributes{{{0, VertexFormat::Float2, 0},
                                                                         {1, VertexFormat::Float2, 8},
                                                                         {2, VertexFormat::Float4, 16}}};
                    std::shared_ptr<PipelineResource> pipeline;
                    if (const auto result = AcquireBuiltinPipeline(
                            "GameUI.slang", PixelFormat::RGBA8_SRGB, PixelFormat::Unknown, {},
                            {sizeof(UIDrawVertex), attributes}, pipeline, RHIBlendMode::Premultiplied);
                        !result.IsSuccess())
                        return result;
                    if (const auto result =
                            surface->gameUI.Prepare(m_FrameSlot, *surface->gameUIDraw, pipeline);
                        !result.IsSuccess())
                        return result;
                }
            if (uiFrame)
                if (const auto prepare = m_UI.Prepare(m_FrameSlot, *uiFrame, m_Swapchain.GetInfo().format); !prepare.IsSuccess()) return prepare;
            if (!uiFrame)
                if (const auto prepare = m_UI.Prepare(m_FrameSlot, ImGuiFrameData{}, m_Swapchain.GetInfo().format); !prepare.IsSuccess()) return prepare;
            // 所有surface与本帧UI lease均已确定；不再使用的版本归还owner，旧GPU命令继续保活。
            m_Geometry.Prune();
            m_Materials.Prune();
            m_Textures.Prune();
            m_Pipelines.Prune();
            m_Compute.Prune();
            status = m_Swapchain.Acquire(frame.token, timeoutNanoseconds, acquired);
            if (!status.IsSuccess() || !acquired.HasImage()) { OnCancelled(); return status; }

            RenderGraph graph(m_Device);
            RenderGraphPlan plan;
            status = BuildFrameGraph(graph, acquired, uiFrame);
            if (!status.IsSuccess()) return CancelFrame(acquired, status);
            status = graph.Compile(plan);
            if (!status.IsSuccess()) return CancelFrame(acquired, status);
            const RHISubmission submission{ .frame = frame.token, .commands = frame.commands,
                .wait = acquired.acquireReady, .signal = acquired.renderFinished };
            FrameSubmissionResult execution;
            status = FrameScheduler::Execute(plan, m_Device.GetGraphicsQueue(), submission, execution, m_Jobs,
                                             m_Profile.asyncQueues);
            if (execution.serializedForBudget) {
                static bool reportedBudgetFallback = false;
                if (!reportedBudgetFallback) {
                    CAT_CORE_WARN("Render graph queue switches exceed the parallel scheduling budget; this "
                                  "frame uses ordered graphics recording.");
                    reportedBudgetFallback = true;
                }
            }
            submitted = execution.accepted;
            if (execution.anyAccepted) {
                const auto points = execution.completions.GetPoints();
                m_Geometry.MarkUses(points);
                m_Textures.MarkUses(points);
                m_Readbacks.OnAccepted(points);
                if (!execution.accepted)
                    m_Uploads.OnPartiallySubmitted(points);
            }
            if (!submitted) return CancelFrame(acquired, status);
            if (!status.IsSuccess()) return status;
            OnSubmitted(execution.completion, execution.completions.GetPoints());
            m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
            RHIPresentOutcome outcome;
            return m_Swapchain.Present(acquired, outcome);
        } catch (const std::bad_alloc&) {
            const RHIStatus failure{ .code = RHIErrorCode::OutOfMemory, .operation = "Renderer::Render" };
            if (!submitted) OnCancelled();
            return acquired.HasImage() && !submitted ? CancelFrame(acquired, failure) : failure;
        }
    }
}

namespace Catalpa {
    bool Renderer::SetPreviewGameUI(uint64_t id, std::shared_ptr<const UIDrawData> data, std::string& error) {
        const auto found = m_Previews.find(id);
        if (found == m_Previews.end()) {
            error = "Create a preview Scene/surface before attaching Game UI";
            return false;
        }
        found->second->gameUIDraw = std::move(data);
        error.clear();
        return true;
    }
} // namespace Catalpa
