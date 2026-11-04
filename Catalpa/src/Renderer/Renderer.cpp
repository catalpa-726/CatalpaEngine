#include "catpch.h"
#include "Renderer.h"
#include "Assets/AssetManager.h"
#include "Assets/BuiltinAssetIds.h"
#include "Shaders/ShaderLibrary.h"
#include "Materials/MaterialValidation.h"
#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "RHI/RHITextureDesc.h"
#include "RHI/RHIBindingDesc.h"
#include "RHI/RHISamplerDesc.h"
#include "Geometry/VertexLayout.h"
#include "RenderGraph/RenderGraph.h"
#include "FrameScheduler.h"
#include "Passes/DepthPrepass.h"
#include "Passes/ForwardPass.h"

namespace Catalpa {

    Renderer::Renderer(RHIDevice& device, RHISwapchain& swapchain, AssetManager& assets, const RendererProfile& profile)
        : m_Device(device), m_Swapchain(swapchain), m_Assets(assets), m_Uploads(device),
          m_Geometry(device,m_Uploads), m_Textures(device,m_Uploads), m_Materials(device), m_Pipelines(device),
          m_UI(device,assets,m_Textures,m_Pipelines), m_FrameUniforms(device), m_Profile(profile) {}

    Renderer::~Renderer()
    {
        // 先放弃pending staging引用，再释放cache owner；后端独立命令引用仍由Runtime先等待完成。
        m_Uploads.Shutdown();
        m_Instances.clear();
        m_Geometry.Clear();
        m_Materials.Clear();
        m_Device.ReleaseTextureView(m_DepthView);
        m_Device.ReleaseTexture(m_DepthTexture);
    }

    RHIStatus Renderer::InitUI(std::span<const std::byte> pixels, Extent2D extent)
    {
        std::string error;
        if (extent.IsEmpty() || uint64_t(extent.width)*extent.height > SIZE_MAX/4
            || pixels.size() != uint64_t(extent.width)*extent.height*4)
            return { .code=RHIErrorCode::InvalidDescription,.operation="Renderer::InitUI" };
        ImageAsset image;
        image.image.width=extent.width; image.image.height=extent.height; image.image.format=PixelFormat::RGBA8_UNorm;
        image.image.alphaMode=ImageAlphaMode::Straight; image.image.pixels.assign(pixels.begin(),pixels.end());
        image.image.subresources.push_back({0,0,0,extent.width,extent.height,1,0,uint64_t(extent.width)*4,pixels.size(),pixels.size()});
        image.sourceUri="generated:imgui-font-atlas";
        const size_t resident=sizeof(ImageAsset)+image.sourceUri.capacity()+1+image.image.pixels.capacity()
            +image.image.subresources.capacity()*sizeof(ImageSubresource);
        const auto id=BuiltinAssetIds::FontAtlasImage;
        if (!m_Assets.PublishBuiltin<ImageAsset>(id,1,std::make_shared<const ImageAsset>(std::move(image)),resident,error))
            return { .code=RHIErrorCode::InvalidState,.operation="Renderer::InitUI/publication" };
        AssetVersionRef<ImageAsset> version;
        bool fallback=false;
        if (!m_Assets.Acquire(id,version,fallback,error))
            return { .code=RHIErrorCode::InvalidState,.operation="Renderer::InitUI/acquire" };
        return m_UI.InitFont(version);
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
                if(instance.mesh->GetData().vertexCount==0) continue;
                if (i >= m_Instances.size() || !m_Instances[i].geometry)
                    return { .code = RHIErrorCode::InvalidState, .operation = "Renderer::BuildFrameGraph geometry" };
                const auto& gpu = m_Instances[i];
                const auto& geometry = *gpu.geometry;
                RGBuffer vertices, indices;
                if (const auto status = graph.ImportBuffer(geometry.vertices, vertices); !status.IsSuccess()) return status;
                if (const auto status = graph.ImportBuffer(geometry.indices, indices); !status.IsSuccess()) return status;
                for (const auto& submesh : geometry.asset.Get()->mesh->GetData().submeshes) {
                    RGIndexedDrawDesc draw;
                    draw.vertices = vertices;
                    draw.indices = indices;
                    draw.color = color;
                    const auto slot = submesh.materialSlot;
                    if (slot >= gpu.forward.size() || slot >= gpu.materials.size())
                        return { .code=RHIErrorCode::InvalidState,.operation="Renderer::BuildFrameGraph material slot" };
                    RGBuffer material;
                    if (const auto status=graph.ImportBuffer(gpu.materials[slot]->parameters,material);!status.IsSuccess()) return status;
                    draw.pipeline = gpu.forward[slot]->pipeline;
                    draw.indexType = geometry.asset.Get()->mesh->GetData().indexType == MeshIndexType::UInt16 ? RHIIndexType::UInt16 : RHIIndexType::UInt32;
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
                        { uniforms, 2, packing->drawOffsets[i], sizeof(DrawGpuParameters), RHIShaderVisibility::VertexFragment },
                        { material, 3, 0, 16, RHIShaderVisibility::Fragment }
                    };
                    forwardDraws.push_back(draw);
                    if (m_Profile.depthPrepass) {
                        draw.pipeline = gpu.depth[slot]->pipeline;
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
            m_Geometry.OnSubmitted();
            m_Textures.OnSubmitted();
            m_FrameSlot = (m_FrameSlot + 1) % m_Device.GetFrameCount();
            RHIPresentOutcome outcome{};
            return m_Swapchain.Present(acquired, outcome);
        } catch (const std::bad_alloc&) {
            const RHIStatus failure{ .code = RHIErrorCode::OutOfMemory, .operation = "Renderer::Render" };
            if (!submitted) m_FrameUniforms.OnCancelled();
            return acquired.HasImage() && !submitted ? CancelFrame(acquired, failure) : failure;
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

    RHIStatus Renderer::EnsureScenePipelines(const MeshData& mesh, const MaterialGpuResource& material, bool reversedZ,
        std::shared_ptr<PipelineResource>& forward, std::shared_ptr<PipelineResource>& depth)
    {
        constexpr const char* op="Renderer::EnsureScenePipelines";
        const auto& definition=*material.asset.Get()->definition.Get();
        // 材质定义已经固定shader依赖；旧帧/定义不得按逻辑ID自动追逐当前版本。
        const auto& shader=definition.shaderVersion;
        AssetVersionRef<ShaderAsset> depthShader;
        bool fallback=false; std::string error; AssetId depthId;
        if (!shader.HasValue() || shader.GetId()!=definition.shader.id
            || !ShaderLibrary::BuiltinId("Depth.slang",depthId,error)
            || !m_Assets.Acquire(depthId,depthShader,fallback,error) || fallback)
            return { .code=RHIErrorCode::InvalidState,.operation=op };
        std::vector<VertexAttribute> inputs;
        for(const auto& attribute:mesh.attributes) if(attribute.location<2) inputs.push_back(attribute);
        const VertexLayout layout{mesh.vertexStride,inputs};
        if (!ValidateMaterialShaderInterface(definition,shader.Get()->metadata,layout,error))
            return { .code=RHIErrorCode::InvalidDescription,.operation="Renderer/material shader ABI" };
        // 与CPU回归共享同一布局校验，保留Slang真实array wrapper与每个字段的严格边界。
        if(!ValidateFrameShaderInterface(shader.Get()->metadata,definition.vertexEntry,definition.fragmentEntry,error)) {
            CAT_CORE_ERROR("Frame/View/Draw shader ABI rejected: {0}",error);
            return { .code=RHIErrorCode::InvalidDescription,.operation="Renderer/FrameViewDraw shader ABI" };
        }
        const std::array<RHIBindingLayoutEntry,4> bindings{{
            {0,RHIBindingType::UniformBuffer,RHIShaderVisibility::Fragment,32},
            {1,RHIBindingType::UniformBuffer,RHIShaderVisibility::Vertex,80},
            {2,RHIBindingType::UniformBuffer,RHIShaderVisibility::VertexFragment,128},
            {3,RHIBindingType::UniformBuffer,RHIShaderVisibility::Fragment,16}
        }};
        const std::array<RHIBindingLayoutEntry,2> depthBindings{{
            {1,RHIBindingType::UniformBuffer,RHIShaderVisibility::Vertex,80},
            {2,RHIBindingType::UniformBuffer,RHIShaderVisibility::Vertex,128}
        }};
        RHIGraphicsPipelineDesc desc;
        desc.vertex={.asset=shader,.stage=RHIShaderStage::Vertex,.entryPoint=definition.vertexEntry.c_str()};
        desc.fragment={.asset=shader,.stage=RHIShaderStage::Fragment,.entryPoint=definition.fragmentEntry.c_str()};
        desc.colorFormat=m_Swapchain.GetInfo().format; desc.vertexLayout=layout; desc.bindingLayout={bindings};
        desc.depthFormat=m_Profile.depthFormat;
        desc.depthState={true,!m_Profile.depthPrepass,m_Profile.depthPrepass?RHICompareOp::Equal:(reversedZ?RHICompareOp::GreaterEqual:RHICompareOp::LessEqual)};
        switch(material.asset.Get()->renderState.cull) {
        case MaterialCullMode::None: desc.cullMode=RHICullMode::None;break;
        case MaterialCullMode::Back: desc.cullMode=RHICullMode::Back;break;
        case MaterialCullMode::Front: desc.cullMode=RHICullMode::Front;break;
        default:return {.code=RHIErrorCode::InvalidDescription,.operation=op};
        }
        if(const auto status=m_Pipelines.Acquire(desc,forward);!status.IsSuccess()) return status;
        if(m_Profile.depthPrepass) {
            // 共用同一最终Invariant vertex bytecode，创建后不得再patch而破坏hash/metadata关联。
            desc.fragment={.asset=depthShader,.stage=RHIShaderStage::Fragment,.entryPoint="fragmentMain"};
            desc.colorFormat=PixelFormat::Unknown; desc.bindingLayout={depthBindings};
            desc.depthState={true,true,reversedZ?RHICompareOp::GreaterEqual:RHICompareOp::LessEqual};
            if(const auto status=m_Pipelines.Acquire(desc,depth);!status.IsSuccess()) return status;
        }
        return {};
    }
    RHIStatus Renderer::EnsureSceneResources()
    {
        const auto& packet=m_Scene.GetFrame();
        if(packet.views.empty()||packet.instances.empty()) { m_Instances.clear(); m_Geometry.Prune(); m_Materials.Prune(); m_Pipelines.Prune(); m_Textures.Prune(); return {}; }
        if(packet.views.size()!=1||m_Profile.depthFormat!=PixelFormat::D32_Float)
            return {.code=RHIErrorCode::Unsupported,.operation="Renderer::EnsureSceneResources"};
        if(const auto status=EnsureDepthTarget(m_Swapchain.GetInfo().extent);!status.IsSuccess()) return status;
        std::vector<InstanceGpu> candidate;
        candidate.reserve(packet.instances.size());
        for(const auto& instance:packet.instances) {
            if(!instance.meshVersion.HasValue()||instance.materials.size()!=instance.meshVersion.Get()->materialSlots.size())
                return {.code=RHIErrorCode::InvalidDescription,.operation="Renderer/fixed asset snapshots required"};
            InstanceGpu gpu;
            if(instance.mesh->GetData().vertexCount==0) {candidate.push_back(std::move(gpu));continue;}
            if(const auto status=m_Geometry.Acquire(instance.meshVersion,gpu.geometry);!status.IsSuccess()) return status;
            const auto& mesh=instance.mesh->GetData();
            for(const auto& material:instance.materials) {
                std::shared_ptr<MaterialGpuResource> value;
                auto status=m_Materials.Acquire(material,value);
                std::shared_ptr<PipelineResource> forward,depth;
                if(status.IsSuccess()) status=EnsureScenePipelines(mesh,*value,packet.views[0].lens.reversedZ,forward,depth);
                if(!status.IsSuccess()) {
                    if(status.code==RHIErrorCode::DeviceLost||status.code==RHIErrorCode::OutOfMemory||status.code==RHIErrorCode::BackendFailure) return status;
                    const auto key=std::make_pair(material.GetId(),material.GetVersion());
                    if(m_ReportedMaterialFailures.size()<256&&m_ReportedMaterialFailures.insert(key).second)
                        CAT_CORE_WARN("Material {0} version {1} unavailable at {2}; using the explicit magenta error material.",
                            material.GetId().ToString(),material.GetVersion(),status.operation);
                    AssetVersionRef<MaterialInstance> fallbackMaterial;bool usedFallback=false;std::string error;
                    if(!m_Assets.Acquire(BuiltinAssetIds::ErrorInstance,fallbackMaterial,usedFallback,error)||usedFallback)
                        return status;
                    if(const auto fallbackStatus=m_Materials.Acquire(fallbackMaterial,value);!fallbackStatus.IsSuccess()) return fallbackStatus;
                    if(const auto fallbackStatus=EnsureScenePipelines(mesh,*value,packet.views[0].lens.reversedZ,forward,depth);!fallbackStatus.IsSuccess()) return fallbackStatus;
                }
                gpu.materials.push_back(std::move(value)); gpu.forward.push_back(std::move(forward)); gpu.depth.push_back(std::move(depth));
            }
            candidate.push_back(std::move(gpu));
        }
        m_Instances.swap(candidate); candidate.clear();
        m_Geometry.Prune(); m_Materials.Prune(); m_Pipelines.Prune(); m_Textures.Prune();
        return {};
    }
}
