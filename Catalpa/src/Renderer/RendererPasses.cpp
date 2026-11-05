#include "catpch.h"
#include "Renderer/RendererInternal.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/Passes/DepthPrepass.h"
#include "Renderer/Passes/ForwardPass.h"
#include "Renderer/Passes/TransparentPass.h"
#include "Renderer/Passes/ShadowPass.h"
#include "Renderer/Passes/SkyboxPass.h"
#include "Renderer/Passes/TonemapPass.h"
#include "Renderer/Passes/PickingPass.h"
#include "Renderer/Passes/DebugDrawPass.h"

namespace Catalpa {

    RHIStatus Renderer::PrepareSurface(Surface& surface, const RenderFramePacket& packet, Extent2D extent)
    {
        // 提交前完成所有可能分配；队列接受后身份表只swap，不能半发布一份GPU显示帧。
        std::vector<RenderInstanceId> identities;
        identities.reserve(packet.instances.size());
        for (const auto& instance : packet.instances) identities.push_back(instance.id);
        if (const auto status = surface.uniforms.Prepare(m_FrameSlot, packet, m_Profile, extent); !status.IsSuccess()) return status;
        auto& frame = surface.frames[m_FrameSlot];
        PackedLightingData lighting;
        const uint32_t mipLevels = surface.environmentVersion.HasValue()
            ? surface.environmentVersion.Get()->specular.image.mipLevels : 1;
        if (!BuildLightingData(packet, surface.shadow, 1024, mipLevels, lighting).IsSuccess())
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/lighting packing" };
        if (!surface.environmentVersion.HasValue()) lighting.countEnvironment[1] = 0;
        const auto writeUniform = [&](BufferHandle& buffer, std::span<const std::byte> bytes) -> RHIStatus {
            if (!buffer.HasValue()) {
                const RHIBufferDesc desc{ .size = bytes.size(), .usage = RHIBufferUsage::Uniform, .memory = RHIMemoryDomain::Upload };
                if (const auto status = m_Device.CreateBuffer(desc, buffer); !status.IsSuccess()) return status;
            }
            return m_Device.WriteBuffer(buffer, 0, bytes);
        };
        if (const auto status = writeUniform(frame.lights, std::as_bytes(std::span{ &lighting, 1 })); !status.IsSuccess()) return status;
        ViewGpuParameters shadowView;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                shadowView.viewProjectionColumns[column][row] = surface.shadow.viewProjection[column][row];
        if (const auto status = writeUniform(frame.shadowView, std::as_bytes(std::span{ &shadowView, 1 })); !status.IsSuccess()) return status;

        const bool drawDebug = m_DebugSurface == 0 ? &surface == m_Main.get()
            : &surface != m_Main.get() && packet.views.front().id == m_DebugSurface;
        if (drawDebug && !m_Debug.GetVertices().empty()) {
            const auto bytes = std::as_bytes(m_Debug.GetVertices());
            if (frame.debugCapacity < bytes.size()) {
                BufferHandle replacement;
                const RHIBufferDesc desc{ .size = bytes.size(), .usage = RHIBufferUsage::Vertex, .memory = RHIMemoryDomain::Upload };
                if (const auto status = m_Device.CreateBuffer(desc, replacement); !status.IsSuccess()) return status;
                m_Device.ReleaseBuffer(std::exchange(frame.debug, replacement));
                frame.debugCapacity = bytes.size();
            }
            if (const auto status = m_Device.WriteBuffer(frame.debug, 0, bytes); !status.IsSuccess()) return status;
            constexpr std::array<VertexAttribute, 2> attributes{{
                { 0, VertexFormat::Float3, 0 }, { 1, VertexFormat::Float4, 12 }
            }};
            const bool reversedZ = packet.views.front().lens.reversedZ;
            if (const auto status = AcquireBuiltinPipeline("DebugDraw.slang", PixelFormat::RGBA32_Float, m_Profile.depthFormat,
                { true, false, reversedZ ? RHICompareOp::GreaterEqual : RHICompareOp::LessEqual },
                { sizeof(DebugVertex), attributes }, surface.debug, RHIBlendMode::StraightAlpha, RHITopology::LineList); !status.IsSuccess()) return status;
        }
        surface.pendingIdentities.swap(identities);
        return {};
    }

    RHIStatus Renderer::FillDrawBindings(RenderGraph& graph, const Surface& surface, const RenderFramePacket& packet,
        const DrawResources& resources, size_t instanceIndex, const PipelineResource& pipeline, bool shadow, RGIndexedDrawDesc& draw)
    {
        constexpr const char* operation = "Renderer::FillDrawBindings";
        const auto* packing = surface.uniforms.GetPacking(m_FrameSlot);
        if (!packing || packing->viewOffsets.size() != 1 || instanceIndex >= packing->drawOffsets.size())
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        const auto& frame = surface.frames[m_FrameSlot];
        for (const auto& binding : pipeline.bindings) {
            if (binding.type == RHIBindingType::Sampler) continue;
            if (binding.type == RHIBindingType::UniformBuffer) {
                BufferHandle buffer = surface.uniforms.GetBuffer(m_FrameSlot);
                uint64_t offset = 0, bytes = 0;
                switch (binding.binding) {
                case 0: bytes = sizeof(FrameGpuParameters); break;
                case 1:
                    bytes = sizeof(ViewGpuParameters);
                    if (shadow) buffer = frame.shadowView;
                    else offset = packing->viewOffsets.front();
                    break;
                case 2: offset = packing->drawOffsets[instanceIndex]; bytes = sizeof(DrawGpuParameters); break;
                case 3: buffer = resources.material->parameters; bytes = sizeof(MaterialGpuParameters); break;
                case 10: buffer = frame.lights; bytes = sizeof(PackedLightingData); break;
                default: return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/unknown uniform binding" };
                }
                if (bytes != binding.minimumBufferBytes)
                    return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/reflected UBO range mismatch" };
                RGBuffer imported;
                if (const auto status = graph.ImportBuffer(buffer, imported); !status.IsSuccess()) return status;
                draw.uniformBuffers.push_back({ imported, binding.binding, offset, bytes, binding.visibility });
            } else {
                TextureViewHandle texture;
                SamplerHandle sampler{};
                uint32_t samplerBinding = 0;
                if (binding.binding >= 4 && binding.binding <= 8) {
                    texture = resources.textures[binding.binding - 4]->view;
                    sampler = resources.samplers[binding.binding - 4];
                    samplerBinding = binding.binding + 14;
                } else if (binding.binding == 11) {
                    texture = surface.targets->shadow.view;
                    sampler = m_ShadowSampler;
                    samplerBinding = 12;
                } else if (binding.binding >= 13 && binding.binding <= 15) {
                    texture = surface.environment[binding.binding - 12]->view;
                    sampler = surface.environmentSampler;
                    samplerBinding = 16;
                } else if (binding.binding == 17) {
                    texture = surface.environment[0]->view;
                    sampler = surface.environmentSampler;
                    samplerBinding = 16;
                } else return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/unknown texture binding" };
                RGTexture imported;
                if (const auto status = graph.ImportTexture(texture, imported); !status.IsSuccess()) return status;
                draw.sampledTextures.push_back({ imported, sampler, binding.binding, samplerBinding });
            }
        }
        return {};
    }

    RHIStatus Renderer::AddSurfacePasses(RenderGraph& graph, Surface& surface, const RenderFramePacket& packet, bool picking)
    {
        constexpr const char* operation = "Renderer::AddSurfacePasses";
        const auto* view = surface.uniforms.GetView(m_FrameSlot);
        const auto* visibility = surface.uniforms.GetVisibility(m_FrameSlot);
        const auto* packing = surface.uniforms.GetPacking(m_FrameSlot);
        if (!view || !visibility || !packing || !surface.targets)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        const auto extent = surface.targets->extent;
        const float clearDepth = view->lens.reversedZ ? 0.0f : 1.0f;
        RGTexture color, depth, display, shadowDepth, pickColor, pickDepth;
        if (const auto status = graph.ImportTexture(surface.targets->hdr.view, color); !status.IsSuccess()) return status;
        if (const auto status = graph.ImportTexture(surface.targets->depth.view, depth); !status.IsSuccess()) return status;
        if (const auto status = graph.ImportTexture(surface.targets->display.view, display); !status.IsSuccess()) return status;
        if (const auto status = graph.ImportTexture(surface.targets->shadow.view, shadowDepth); !status.IsSuccess()) return status;
        auto background = m_Profile.clearColor;
        if (packet.environment && packet.environment->settings.fogEnabled && packet.environment->settings.fogDensityPerMeter > 0) {
            const auto& fog = packet.environment->settings.fogColor;
            background = { fog.x, fog.y, fog.z, 1.0f };
        }
        if (const auto status = graph.AddClearPass("HDR scene clear", color, background); !status.IsSuccess()) return status;
        if (const auto status = graph.AddDepthClearPass("Scene depth clear", depth, clearDepth); !status.IsSuccess()) return status;
        if (const auto status = graph.AddDepthClearPass("Shadow depth clear", shadowDepth, 1.0f); !status.IsSuccess()) return status;
        if (picking) {
            if (const auto status = graph.ImportTexture(surface.targets->picking.view, pickColor); !status.IsSuccess()) return status;
            if (const auto status = graph.ImportTexture(surface.targets->pickingDepth.view, pickDepth); !status.IsSuccess()) return status;
            if (const auto status = graph.AddDepthClearPass("Picking depth clear", pickDepth, clearDepth); !status.IsSuccess()) return status;
        }

        if (surface.skybox && surface.environmentVersion.HasValue() && packet.environment && packet.environment->settings.drawBackground) {
            RGBuffer uniforms, lights;
            RGTexture radiance;
            if (const auto status = graph.ImportBuffer(surface.uniforms.GetBuffer(m_FrameSlot), uniforms); !status.IsSuccess()) return status;
            if (const auto status = graph.ImportBuffer(surface.frames[m_FrameSlot].lights, lights); !status.IsSuccess()) return status;
            if (const auto status = graph.ImportTexture(surface.environment[0]->view, radiance); !status.IsSuccess()) return status;
            RGIndexedDrawDesc sky;
            sky.color = color;
            sky.pipeline = surface.skybox->pipeline;
            sky.viewport = { .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
            sky.scissor.extent = extent;
            // 精确可见性以最终反射为准，VS全屏可能不消费View，FS才读取inverseVP。
            for (const auto& binding : surface.skybox->bindings)
                if (binding.type == RHIBindingType::UniformBuffer)
                    sky.uniformBuffers.push_back(binding.binding == 1
                        ? RGUniformBufferBinding{ uniforms, 1, packing->viewOffsets.front(), sizeof(ViewGpuParameters), binding.visibility }
                        : RGUniformBufferBinding{ lights, 10, 0, sizeof(PackedLightingData), binding.visibility });
            sky.sampledTextures.push_back({ radiance, surface.environmentSampler, 17, 16 });
            if (const auto status = SkyboxPass::Add(graph, std::move(sky)); !status.IsSuccess()) return status;
        }

        const auto makeDraw = [&](size_t instanceIndex, uint32_t submeshIndex, const PipelineResource& pipeline,
            bool shadow, RGIndexedDrawDesc& output) -> RHIStatus {
            if (instanceIndex >= surface.instances.size() || !surface.instances[instanceIndex].geometry)
                return { .code = RHIErrorCode::InvalidState, .operation = "Renderer/draw geometry lease" };
            const auto& gpu = surface.instances[instanceIndex];
            const auto& mesh = gpu.geometry->asset.Get()->mesh->GetData();
            if (submeshIndex >= mesh.submeshes.size()) return { .code = RHIErrorCode::InvalidState, .operation = operation };
            const auto& submesh = mesh.submeshes[submeshIndex];
            if (submesh.materialSlot >= gpu.materials.size()) return { .code = RHIErrorCode::InvalidState, .operation = operation };
            RGIndexedDrawDesc draw;
            if (const auto status = graph.ImportBuffer(gpu.geometry->vertices, draw.vertices); !status.IsSuccess()) return status;
            if (const auto status = graph.ImportBuffer(gpu.geometry->indices, draw.indices); !status.IsSuccess()) return status;
            draw.color = shadow ? RGTexture{} : color;
            draw.depth = shadow ? shadowDepth : depth;
            draw.pipeline = pipeline.pipeline;
            draw.indexType = mesh.indexType == MeshIndexType::UInt16 ? RHIIndexType::UInt16 : RHIIndexType::UInt32;
            draw.indexCount = submesh.indexCount;
            draw.firstIndex = submesh.firstIndex;
            draw.baseVertex = submesh.baseVertex;
            const Extent2D drawExtent = shadow ? Extent2D{ 1024, 1024 } : extent;
            draw.viewport = { .width = static_cast<float>(drawExtent.width), .height = static_cast<float>(drawExtent.height) };
            draw.scissor.extent = drawExtent;
            draw.clearDepth = shadow ? 1.0f : clearDepth;
            draw.loadOp = RHILoadOp::Load;
            draw.depthLoadOp = RHILoadOp::Load;
            if (const auto status = FillDrawBindings(graph, surface, packet, gpu.materials[submesh.materialSlot],
                instanceIndex, pipeline, shadow, draw); !status.IsSuccess()) return status;
            output = std::move(draw);
            return {};
        };

        std::vector<RGIndexedDrawDesc> shadowDraws, depthDraws, forwardDraws, transparentDraws, pickDraws;
        if (surface.shadow.enabled)
            for (size_t instance = 0; instance < surface.instances.size(); ++instance) {
                const auto& gpu = surface.instances[instance];
                if (!gpu.geometry) continue;
                const auto& submeshes = gpu.geometry->asset.Get()->mesh->GetData().submeshes;
                for (size_t submesh = 0; submesh < submeshes.size(); ++submesh) {
                    const auto& resources = gpu.materials[submeshes[submesh].materialSlot];
                    if (!resources.shadow) continue;
                    RGIndexedDrawDesc draw;
                    if (const auto status = makeDraw(instance, static_cast<uint32_t>(submesh), *resources.shadow, true, draw); !status.IsSuccess()) return status;
                    shadowDraws.push_back(std::move(draw));
                }
            }
        if (const auto status = ShadowPass::Add(graph, shadowDraws); !status.IsSuccess()) return status;

        std::vector<VisibleSubmesh> visible = visibility->opaque;
        visible.insert(visible.end(), visibility->masked.begin(), visibility->masked.end());
        visible.insert(visible.end(), visibility->transparent.begin(), visibility->transparent.end());
        // 错误材质/旧可用版本可能改变分类；根据真正绑定的版本确定深度与混合策略。
        std::stable_sort(visible.begin(), visible.end(), [&](const auto& left, const auto& right) {
            const auto alphaMode = [&](const auto& entry) {
                const auto& gpu = surface.instances[entry.instanceIndex];
                const auto slot = gpu.geometry->asset.Get()->mesh->GetData().submeshes[entry.submeshIndex].materialSlot;
                return gpu.materials[slot].material->asset.Get()->definition.Get()->alphaMode;
            };
            const bool leftBlend = alphaMode(left) == MaterialAlphaMode::Blend;
            const bool rightBlend = alphaMode(right) == MaterialAlphaMode::Blend;
            if (leftBlend != rightBlend) return !leftBlend;
            if (leftBlend && left.cameraDepth != right.cameraDepth) return left.cameraDepth > right.cameraDepth;
            if (packet.instances[left.instanceIndex].id != packet.instances[right.instanceIndex].id)
                return packet.instances[left.instanceIndex].id < packet.instances[right.instanceIndex].id;
            return left.submeshIndex < right.submeshIndex;
        });
        for (const auto& entry : visible) {
            const auto& gpu = surface.instances[entry.instanceIndex];
            const auto slot = gpu.geometry->asset.Get()->mesh->GetData().submeshes[entry.submeshIndex].materialSlot;
            const auto& resources = gpu.materials[slot];
            const bool blend = resources.material->asset.Get()->definition.Get()->alphaMode == MaterialAlphaMode::Blend;
            RGIndexedDrawDesc draw;
            if (const auto status = makeDraw(entry.instanceIndex, entry.submeshIndex, *resources.forward, false, draw); !status.IsSuccess()) return status;
            (blend ? transparentDraws : forwardDraws).push_back(std::move(draw));
            if (!blend && m_Profile.depthPrepass) {
                if (const auto status = makeDraw(entry.instanceIndex, entry.submeshIndex, *resources.depth, false, draw); !status.IsSuccess()) return status;
                draw.color = {};
                depthDraws.push_back(std::move(draw));
            }
            if (picking) {
                if (const auto status = makeDraw(entry.instanceIndex, entry.submeshIndex, *resources.picking, false, draw); !status.IsSuccess()) return status;
                draw.color = pickColor;
                draw.depth = pickDepth;
                pickDraws.push_back(std::move(draw));
            }
        }
        if (m_Profile.depthPrepass)
            if (const auto status = DepthPrepass::Add(graph, depthDraws, clearDepth); !status.IsSuccess()) return status;
        if (const auto status = ForwardPass::Add(graph, forwardDraws, m_Profile.depthPrepass); !status.IsSuccess()) return status;
        if (const auto status = TransparentPass::Add(graph, transparentDraws); !status.IsSuccess()) return status;
        if (picking) {
            if (const auto status = PickingPass::Add(graph, pickColor, pickDraws, clearDepth); !status.IsSuccess()) return status;
            if (const auto status = graph.ExportReadbackTexture(pickColor); !status.IsSuccess()) return status;
        }

        const bool drawDebug = m_DebugSurface == 0 ? !picking : picking && packet.views.front().id == m_DebugSurface;
        if (drawDebug && !m_Debug.GetVertices().empty() && surface.debug) {
            RGIndexedDrawDesc debug;
            debug.color = color;
            debug.depth = depth;
            debug.pipeline = surface.debug->pipeline;
            debug.vertexCount = static_cast<uint32_t>(m_Debug.GetVertices().size());
            debug.viewport = { .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
            debug.scissor.extent = extent;
            RGBuffer uniforms;
            if (const auto status = graph.ImportBuffer(surface.frames[m_FrameSlot].debug, debug.vertices); !status.IsSuccess()) return status;
            if (const auto status = graph.ImportBuffer(surface.uniforms.GetBuffer(m_FrameSlot), uniforms); !status.IsSuccess()) return status;
            debug.uniformBuffers.push_back({ uniforms, 1, packing->viewOffsets.front(), sizeof(ViewGpuParameters), RHIShaderVisibility::Vertex });
            if (const auto status = DebugDrawPass::Add(graph, std::move(debug)); !status.IsSuccess()) return status;
        }

        RGIndexedDrawDesc tonemap;
        tonemap.color = display;
        tonemap.pipeline = surface.tonemap->pipeline;
        tonemap.viewport = { .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height) };
        tonemap.scissor.extent = extent;
        const auto hdrSampler = m_Device.GetFormatSupport(PixelFormat::RGBA32_Float).linearFiltering
            ? m_EnvironmentSampler : m_NearestEnvironmentSampler;
        tonemap.sampledTextures.push_back({ color, hdrSampler, 0, 1 });
        const float exposure = packet.environment ? packet.environment->settings.exposureEV : 0;
        if (const auto status = TonemapPass::Add(graph, std::move(tonemap), exposure); !status.IsSuccess()) return status;
        return graph.ExportTexture(display);
    }
}
