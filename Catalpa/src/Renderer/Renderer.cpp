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
#include "FrameScheduler.h"
#include "Passes/DepthPrepass.h"
#include "Passes/ForwardPass.h"
#include "UnlitVertexSpirv.h"
#include "UnlitFragmentSpirv.h"
#include "DepthFragmentSpirv.h"

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
        for (auto& [key, geometry] : m_Geometry) ReleaseGeometry(geometry);
        m_Device.ReleaseTextureView(m_DepthView);
        m_Device.ReleaseTexture(m_DepthTexture);
        m_Device.ReleaseSampler(m_TriangleSampler);
        m_Device.ReleaseTextureView(m_TriangleTextureView);
        m_Device.ReleaseTexture(m_TriangleTexture);
        m_Device.ReleaseBuffer(m_TriangleIndices);
        m_Device.ReleaseBuffer(m_TriangleVertices);
        m_Device.ReleaseGraphicsPipeline(m_TrianglePipeline);
    }

    RHIStatus Renderer::BuildFrameGraph(RenderGraph& graph, const RHIAcquireResult& acquired, const ImGuiFrameData* uiFrame)
    {
        if (const auto status = m_Uploads.AddPasses(graph); !status.IsSuccess()) return status;
        RGTexture color;
        if (const auto status = graph.ImportTexture(acquired.view, color); !status.IsSuccess()) return status;
        const auto extent = m_Swapchain.GetInfo().extent;
        // 无相机/无实例也需要真实清屏，不能依靠旧Triangle或伪draw初始化present输出。
        if (const auto status = graph.AddClearPass("Scene Clear", color, m_Profile.clearColor); !status.IsSuccess()) return status;
        const auto& packet = m_Scene.GetFrame();
        if (!packet.views.empty() && !packet.instances.empty()) {
            const auto* packing = m_FrameUniforms.GetPacking(m_FrameSlot);
            const auto* view = m_FrameUniforms.GetView(m_FrameSlot);
            const auto* visibility = m_FrameUniforms.GetVisibility(m_FrameSlot);
            if (!packing || !view || !visibility || packing->viewOffsets.size() != 1
                || packing->drawOffsets.size() != packet.instances.size())
                return { .code = RHIErrorCode::InvalidState, .operation = "Renderer::BuildFrameGraph uniforms" };
            RGTexture depth;
            RGBuffer uniforms;
            if (const auto status = graph.ImportTexture(m_DepthView, depth); !status.IsSuccess()) return status;
            // Prepare已完成本帧WriteBuffer；现在才能读取新的状态revision。
            if (const auto status = graph.ImportBuffer(m_FrameUniforms.GetBuffer(m_FrameSlot), uniforms); !status.IsSuccess()) return status;
            std::vector<RGIndexedDrawDesc> forwardDraws, depthDraws;
            const float clearDepth = view->lens.reversedZ ? 0.0f : 1.0f;
            // 只裁剪绘制清单，保持原实例顺序及UBO索引，不改写CPU网格或帧快照。
            for (const size_t i : visibility->instanceIndices) {
                const auto& instance = packet.instances[i];
                const auto found = m_Geometry.find(instance.mesh.get());
                if (found == m_Geometry.end()) {
                    if (instance.mesh->GetData().vertexCount == 0) continue;
                    return { .code = RHIErrorCode::InvalidState, .operation = "Renderer::BuildFrameGraph geometry" };
                }
                const auto& geometry = found->second;
                RGBuffer vertices, indices;
                if (const auto status = graph.ImportBuffer(geometry.vertices, vertices); !status.IsSuccess()) return status;
                if (const auto status = graph.ImportBuffer(geometry.indices, indices); !status.IsSuccess()) return status;
                for (const auto& submesh : geometry.mesh->GetData().submeshes) {
                    RGIndexedDrawDesc draw;
                    draw.vertices = vertices;
                    draw.indices = indices;
                    draw.color = color;
                    draw.pipeline = geometry.forward;
                    draw.indexType = geometry.mesh->GetData().indexType == MeshIndexType::UInt16 ? RHIIndexType::UInt16 : RHIIndexType::UInt32;
                    draw.indexCount = submesh.indexCount;
                    draw.firstIndex = submesh.firstIndex;
                    draw.baseVertex = submesh.baseVertex;
                    draw.loadOp = RHILoadOp::Load;
                    draw.storeOp = RHIStoreOp::Store;
                    draw.viewport = { .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
                    draw.scissor = { .extent = extent };
                    draw.depth = depth;
                    draw.clearDepth = clearDepth;
                    draw.uniformBuffers = {
                        { uniforms, 0, 0, sizeof(FrameGpuParameters), RHIShaderVisibility::Fragment },
                        { uniforms, 1, packing->viewOffsets[0], sizeof(ViewGpuParameters), RHIShaderVisibility::Vertex },
                        { uniforms, 2, packing->drawOffsets[i], sizeof(DrawGpuParameters), RHIShaderVisibility::VertexFragment }
                    };
                    forwardDraws.push_back(draw);
                    if (m_Profile.depthPrepass) {
                        draw.pipeline = geometry.depth;
                        draw.color = {};
                        // Depth vertex只使用View/Draw；不假定优化器保留未使用的Frame descriptor。
                        draw.uniformBuffers = {
                            { uniforms, 1, packing->viewOffsets[0], sizeof(ViewGpuParameters), RHIShaderVisibility::Vertex },
                            { uniforms, 2, packing->drawOffsets[i], sizeof(DrawGpuParameters), RHIShaderVisibility::Vertex }
                        };
                        depthDraws.push_back(std::move(draw));
                    }
                }
            }
            if (m_Profile.depthPrepass)
                if (const auto status = DepthPrepass::Add(graph, depthDraws, clearDepth); !status.IsSuccess()) return status;
            if (const auto status = ForwardPass::Add(graph, forwardDraws, m_Profile.depthPrepass); !status.IsSuccess()) return status;
        }
        if (uiFrame)
            if (const auto status = m_UI.AddPasses(graph, color, extent, m_FrameSlot, *uiFrame); !status.IsSuccess()) return status;
        return graph.ExportPresent(color);
    }

    RHIStatus Renderer::CancelFrame(RHIAcquireResult& acquired, RHIStatus failure)
    {
        // 取消路径不执行原来的复制命令，上传请求仍然待提交。
        m_Uploads.OnCancelled();
        m_FrameUniforms.OnCancelled();

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

    RHIStatus Renderer::Render(const ImGuiFrameData* uiFrame)
    {
        // 上轮可能在Acquire超时前就完成Prepare；未提交的视图不能成为下一帧历史。
        m_FrameUniforms.OnCancelled();
        if (m_Swapchain.GetInfo().state != RHISwapchainState::Ready) return {};
        RHIAcquireResult acquired{};
        bool submitted = false;
        try {
            if (const auto status = EnsureSceneResources(); !status.IsSuccess()) return status;
            constexpr uint64_t timeoutNanoseconds = 16'000'000;
            RHIFrameResources frame{};
            bool ready = false;
            auto status = m_Device.PrepareFrame(m_FrameSlot, timeoutNanoseconds, frame, ready);
            if (!status.IsSuccess() || !ready) return status;
            const auto& packet = m_Scene.GetFrame();
            if (packet.sequence != 0) {
                status = m_FrameUniforms.Prepare(m_FrameSlot, packet, m_Profile, m_Swapchain.GetInfo().extent);
                if (!status.IsSuccess()) return status;
            }
            if (uiFrame) {
                status = m_UI.Prepare(m_FrameSlot, *uiFrame, m_Swapchain.GetInfo().format);
                if (!status.IsSuccess()) { m_FrameUniforms.OnCancelled(); return status; }
            }
            status = m_Swapchain.Acquire(frame.token, timeoutNanoseconds, acquired);
            if (!status.IsSuccess() || !acquired.HasImage()) { m_FrameUniforms.OnCancelled(); return status; }
            RenderGraph graph(m_Device);
            RenderGraphPlan plan;
            status = BuildFrameGraph(graph, acquired, uiFrame);
            if (!status.IsSuccess()) return CancelFrame(acquired, status);
            status = graph.Compile(plan);
            if (!status.IsSuccess()) return CancelFrame(acquired, status);
            const RHISubmission submission{
                .frame = frame.token, .commands = frame.commands, .wait = acquired.acquireReady, .signal = acquired.renderFinished
            };
            FrameSubmissionResult execution;
            status = FrameScheduler::Execute(plan, m_Device.GetGraphicsQueue(), submission, execution);
            submitted = execution.accepted;
            if (!submitted) return CancelFrame(acquired, status);
            // 后端若违反完成身份契约，保留已接受事实并交运行时失败关闭，不再消费Acquire。
            if (!status.IsSuccess()) return status;
            m_FrameUniforms.OnSubmitted();
            m_Uploads.OnSubmitted(execution.completion);
            // 本次AddPasses冻结了所有已排队静态请求；现在其目标owner才不再受pending约束。
            for (auto& [key, geometry] : m_Geometry) geometry.uploadPending = false;
            m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
            RHIPresentOutcome outcome{};
            return m_Swapchain.Present(acquired, outcome);
        } catch (const std::bad_alloc&) {
            const RHIStatus failure{ .code = RHIErrorCode::OutOfMemory, .operation = "Renderer::Render" };
            if (!submitted) m_FrameUniforms.OnCancelled();
            return acquired.HasImage() && !submitted ? CancelFrame(acquired, failure) : failure;
        }
    }

    void Renderer::ReleaseGeometry(GeometryGpu& geometry) noexcept
    {
        m_Device.ReleaseGraphicsPipeline(geometry.forward);
        m_Device.ReleaseGraphicsPipeline(geometry.depth);
        m_Device.ReleaseBuffer(geometry.indices);
        m_Device.ReleaseBuffer(geometry.vertices);
    }

    void Renderer::PruneGeometry() noexcept
    {
        const auto& instances = m_Scene.GetFrame().instances;
        for (auto it = m_Geometry.begin(); it != m_Geometry.end();) {
            const bool used = std::any_of(instances.begin(), instances.end(),
                [&](const RenderMeshInstance& instance) { return instance.mesh.get() == it->first; });
            if (!used && !it->second.uploadPending) {
                ReleaseGeometry(it->second);
                it = m_Geometry.erase(it);
            } else ++it;
        }
    }

    RHIStatus Renderer::EnsureDepthTarget(Extent2D extent)
    {
        if (m_DepthView.HasValue() && m_DepthExtent == extent) return {};
        constexpr const char* operation = "Renderer::EnsureDepthTarget";
        if (!m_Device.GetFormatSupport(m_Profile.depthFormat).depthAttachment)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        TextureHandle texture;
        TextureViewHandle view;
        try {
        const RHITextureDesc desc{ .extent = extent, .format = m_Profile.depthFormat, .usage = RHITextureUsage::DepthAttachment };
        if (const auto status = m_Device.CreateTexture(desc, texture); !status.IsSuccess()) return status;
        const RHITextureViewDesc viewDesc{ .format = m_Profile.depthFormat, .range = { .aspect = RHITextureAspect::Depth } };
        if (const auto status = m_Device.CreateTextureView(texture, viewDesc, view); !status.IsSuccess()) {
            m_Device.ReleaseTexture(std::exchange(texture, TextureHandle{}));
            return status;
        }
        // 旧附件的命令引用继续保活；尺寸变化不按“等几帧”直接销毁原生image。
        const auto previousView = std::exchange(m_DepthView, std::exchange(view, TextureViewHandle{}));
        const auto previousTexture = std::exchange(m_DepthTexture, std::exchange(texture, TextureHandle{}));
        m_DepthExtent = extent;
        m_Device.ReleaseTextureView(previousView);
        m_Device.ReleaseTexture(previousTexture);
        return {};
        } catch (const std::bad_alloc&) {
            // 创建View的C++分配失败同样必须归还已成功创建的候选图像。
            m_Device.ReleaseTextureView(view);
            m_Device.ReleaseTexture(texture);
            return { .code = RHIErrorCode::OutOfMemory, .operation = operation };
        }
    }

    RHIStatus Renderer::EnsureScenePipelines(GeometryGpu& geometry, bool reversedZ)
    {
        const auto colorFormat = m_Swapchain.GetInfo().format;
        if (geometry.forward.HasValue() && geometry.colorFormat == colorFormat && geometry.reversedZ == reversedZ
            && geometry.prepass == m_Profile.depthPrepass && (!m_Profile.depthPrepass || geometry.depth.HasValue())) return {};
        constexpr std::array<RHIBindingLayoutEntry, 3> forwardBindings{{
            { 0, RHIBindingType::UniformBuffer, RHIShaderVisibility::Fragment },
            { 1, RHIBindingType::UniformBuffer, RHIShaderVisibility::Vertex },
            { 2, RHIBindingType::UniformBuffer, RHIShaderVisibility::VertexFragment }
        }};
        constexpr std::array<RHIBindingLayoutEntry, 2> depthBindings{{
            { 1, RHIBindingType::UniformBuffer, RHIShaderVisibility::Vertex },
            { 2, RHIBindingType::UniformBuffer, RHIShaderVisibility::Vertex }
        }};
        std::vector<uint32_t> invariantVertex;
        std::span<const uint32_t> vertexCode = UnlitVertexSpirv;
        if (m_Profile.depthPrepass) {
            if (const auto status = DepthPrepass::PrepareInvariantVertexShader(vertexCode, invariantVertex); !status.IsSuccess()) return status;
            vertexCode = invariantVertex;
        }
        RHIGraphicsPipelineDesc desc;
        desc.vertex = { .bytecode = vertexCode, .stage = RHIShaderStage::Vertex, .entryPoint = "vertexMain" };
        desc.fragment = { .bytecode = UnlitFragmentSpirv, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain" };
        desc.colorFormat = colorFormat;
        // Shader只消费position/normal；保留原stride/offset，不为未使用UV声明多余输入。
        std::array<VertexAttribute, 2> inputs;
        for (const auto& attribute : geometry.mesh->GetData().attributes)
            if (attribute.location < inputs.size()) inputs[attribute.location] = attribute;
        desc.vertexLayout = { geometry.mesh->GetData().vertexStride, inputs };
        desc.bindingLayout = { .entries = forwardBindings };
        desc.depthFormat = m_Profile.depthFormat;
        desc.depthState = { .testEnabled = true, .writeEnabled = !m_Profile.depthPrepass,
            .compareOp = m_Profile.depthPrepass ? RHICompareOp::Equal : (reversedZ ? RHICompareOp::GreaterEqual : RHICompareOp::LessEqual) };
        // 默认Cull=None，使负缩放实例保持正确显示；完整双面材质策略在材质阶段接入。
        GraphicsPipelineHandle forward, depth;
        try {
        if (const auto status = m_Device.CreateGraphicsPipeline(desc, forward); !status.IsSuccess()) return status;
        if (m_Profile.depthPrepass) {
            desc.colorFormat = PixelFormat::Unknown;
            desc.fragment = { .bytecode = DepthFragmentSpirv, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain" };
            desc.bindingLayout = { .entries = depthBindings };
            desc.depthState = { .testEnabled = true, .writeEnabled = true,
                .compareOp = reversedZ ? RHICompareOp::GreaterEqual : RHICompareOp::LessEqual };
            // 两pass使用同一份带Invariant Position的派生VS与相同UBO/viewport输入，落实Equal的跨pipeline前提。
            // Depth.slang仍生成完整VS/FS供专用路径扩展；本步实际使用其无颜色输出FS。
            if (const auto status = m_Device.CreateGraphicsPipeline(desc, depth); !status.IsSuccess()) {
                m_Device.ReleaseGraphicsPipeline(std::exchange(forward, GraphicsPipelineHandle{}));
                return status;
            }
        }
        const auto previousForward = std::exchange(geometry.forward, std::exchange(forward, GraphicsPipelineHandle{}));
        const auto previousDepth = std::exchange(geometry.depth, std::exchange(depth, GraphicsPipelineHandle{}));
        m_Device.ReleaseGraphicsPipeline(previousForward);
        m_Device.ReleaseGraphicsPipeline(previousDepth);
        geometry.colorFormat = colorFormat;
        geometry.reversedZ = reversedZ;
        geometry.prepass = m_Profile.depthPrepass;
        return {};
        } catch (const std::bad_alloc&) {
            // 两条管线全部创建后才发布；第二条创建异常时不能遗失第一条候选owner。
            m_Device.ReleaseGraphicsPipeline(depth);
            m_Device.ReleaseGraphicsPipeline(forward);
            return { .code = RHIErrorCode::OutOfMemory, .operation = "Renderer::EnsureScenePipelines" };
        }
    }

    RHIStatus Renderer::EnsureSceneResources()
    {
        constexpr const char* operation = "Renderer::EnsureSceneResources";
        PruneGeometry();
        const auto& packet = m_Scene.GetFrame();
        if (packet.views.empty() || packet.instances.empty()) return {};
        if (packet.views.size() != 1 || m_Profile.depthFormat != PixelFormat::D32_Float)
            return { .code = RHIErrorCode::Unsupported, .operation = operation };
        if (const auto status = EnsureDepthTarget(m_Swapchain.GetInfo().extent); !status.IsSuccess()) return status;
        for (const auto& instance : packet.instances) {
            const auto& mesh = instance.mesh->GetData();
            if (mesh.vertexCount == 0) continue;
            // 本步shader要求position/normal在0/1，缺失不能伪装为已正确绘制；UV可由现有layout提供。
            const auto hasAttribute = [&](VertexSemantic semantic, uint32_t location, VertexFormat format) {
                return std::any_of(mesh.attributes.begin(), mesh.attributes.end(), [&](const VertexAttribute& a) {
                    return a.semantic == semantic && a.location == location && a.format == format;
                });
            };
            if (!hasAttribute(VertexSemantic::Position, 0, VertexFormat::Float3) || !hasAttribute(VertexSemantic::Normal, 1, VertexFormat::Float3))
                return { .code = RHIErrorCode::Unsupported, .operation = operation };
            auto [it, inserted] = m_Geometry.try_emplace(instance.mesh.get());
            auto& geometry = it->second;
            if (inserted) geometry.mesh = instance.mesh;
            if (!geometry.vertices.HasValue())
                if (const auto status = m_Uploads.CreateBuffer(mesh.vertexData, RHIBufferUsage::Vertex, geometry.vertices); !status.IsSuccess()) return status;
            if (!geometry.indices.HasValue())
                if (const auto status = m_Uploads.CreateBuffer(mesh.indexData, RHIBufferUsage::Index, geometry.indices); !status.IsSuccess()) return status;
            if (const auto status = EnsureScenePipelines(geometry, packet.views[0].lens.reversedZ); !status.IsSuccess()) return status;
        }
        return {};
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
