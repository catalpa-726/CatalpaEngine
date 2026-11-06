#include "catpch.h"
#include "Renderer/RendererInternal.h"
#include "Assets/AssetManager.h"
#include "Assets/BuiltinAssetIds.h"
#include "Shaders/ShaderLibrary.h"
#include "Materials/MaterialValidation.h"
#include "Log/Log.h"

namespace Catalpa {
    namespace {
        // 反射决定真实使用的set0布局；不会依赖优化器保留未使用的纹理或UBO。
        bool BuildPipelineLayout(const ShaderAsset& vertexShader, std::string_view vertexName,
            const ShaderAsset& fragmentShader, std::string_view fragmentName,
            std::vector<RHIBindingLayoutEntry>& bindings, RHIPushConstantDesc& push)
        {
            const auto* vertex = FindShaderEntry(vertexShader.metadata, vertexName, ShaderStage::Vertex);
            const auto* fragment = FindShaderEntry(fragmentShader.metadata, fragmentName, ShaderStage::Fragment);
            if (!vertex || !fragment) return false;
            std::map<uint32_t, RHIBindingLayoutEntry> merged;
            for (const auto* entry : { vertex, fragment }) {
                const auto visibility = entry == vertex ? RHIShaderVisibility::Vertex : RHIShaderVisibility::Fragment;
                for (const auto& source : entry->bindings) {
                    RHIBindingType type;
                    switch (source.kind) {
                    case ShaderResourceKind::UniformBuffer: type = RHIBindingType::UniformBuffer; break;
                    case ShaderResourceKind::SampledImage: type = RHIBindingType::SampledTexture; break;
                    case ShaderResourceKind::Sampler: type = RHIBindingType::Sampler; break;
                    default: return false;
                    }
                    if (source.set != 0 || source.count != 1) return false;
                    const auto viewType = source.imageDimension == ShaderImageDimension::Cube
                        ? RHITextureViewType::Cube : RHITextureViewType::Texture2D;
                    auto [found, inserted] = merged.emplace(source.binding, RHIBindingLayoutEntry{
                        source.binding, type, visibility, source.blockSize, viewType });
                    if (!inserted) {
                        if (found->second.type != type || found->second.minimumBufferBytes != source.blockSize
                            || found->second.imageViewType != viewType) return false;
                        if (found->second.visibility != visibility) found->second.visibility = RHIShaderVisibility::VertexFragment;
                    }
                }
            }
            bindings.clear();
            for (const auto& [index, binding] : merged) bindings.push_back(binding);
            push.size = std::max(vertex->pushConstantBytes, fragment->pushConstantBytes);
            push.visibility = vertex->pushConstantBytes && fragment->pushConstantBytes ? RHIShaderVisibility::VertexFragment
                : vertex->pushConstantBytes ? RHIShaderVisibility::Vertex
                : fragment->pushConstantBytes ? RHIShaderVisibility::Fragment : RHIShaderVisibility::None;
            return true;
        }

        bool BuildPipelineLayout(const ShaderAsset& shader, std::string_view vertexName, std::string_view fragmentName,
            std::vector<RHIBindingLayoutEntry>& bindings, RHIPushConstantDesc& push)
        {
            return BuildPipelineLayout(shader, vertexName, shader, fragmentName, bindings, push);
        }

        std::vector<VertexAttribute> FilterVertexLayout(const ShaderAsset& shader, std::string_view entry, VertexLayout source)
        {
            std::vector<VertexAttribute> output;
            const auto* metadata = FindShaderEntry(shader.metadata, entry, ShaderStage::Vertex);
            if (!metadata) return output;
            for (const auto& input : metadata->inputs)
                for (const auto& attribute : source.attributes)
                    if (attribute.location == input.location) output.push_back(attribute);
            return output;
        }
    }

    RHIStatus Renderer::AcquireBuiltinPipeline(std::string_view file, PixelFormat color, PixelFormat depth,
        RHIDepthState depthState, VertexLayout layout, std::shared_ptr<PipelineResource>& output,
        RHIBlendMode blend, RHITopology topology)
    {
        AssetId id;
        AssetVersionRef<ShaderAsset> shader;
        bool fallback = false;
        std::string error;
        if (!ShaderLibrary::BuiltinId(file, id, error) || !m_Assets.Acquire(id, shader, fallback, error) || fallback)
            return { .code = RHIErrorCode::InvalidState, .operation = "Renderer/cooked builtin shader" };
        std::vector<RHIBindingLayoutEntry> bindings;
        RHIPushConstantDesc push;
        if (!BuildPipelineLayout(*shader.Get(), "vertexMain", "fragmentMain", bindings, push))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/builtin reflected layout" };
        const auto attributes = FilterVertexLayout(*shader.Get(), "vertexMain", layout);
        RHIGraphicsPipelineDesc desc;
        desc.vertex = { .asset = shader, .stage = RHIShaderStage::Vertex, .entryPoint = "vertexMain" };
        desc.fragment = { .asset = shader, .stage = RHIShaderStage::Fragment, .entryPoint = "fragmentMain" };
        desc.colorFormat = color;
        desc.depthFormat = depth;
        desc.depthState = depthState;
        desc.blendMode = blend;
        desc.topology = topology;
        desc.vertexLayout = { layout.stride, attributes };
        desc.bindingLayout.entries = bindings;
        desc.pushConstants = push;
        return m_Pipelines.Acquire(desc, output);
    }

    RHIStatus Renderer::EnsureDefaultTextures()
    {
        std::string error;
        bool fallback = false;
        for (auto [id, destination] : { std::pair{ BuiltinAssetIds::WhiteImage, &m_White },
            std::pair{ BuiltinAssetIds::FlatNormalImage, &m_FlatNormal } }) {
            if (*destination) continue;
            AssetVersionRef<ImageAsset> image;
            if (!m_Assets.Acquire(id, image, fallback, error) || fallback)
                return { .code = RHIErrorCode::InvalidState, .operation = "Renderer/default image" };
            if (const auto status = m_Textures.Acquire(image, *destination); !status.IsSuccess()) return status;
        }
        if (!m_BlackCube) {
            // Shader仍声明Cube descriptor时也提供真实Cube默认值，不用2D图冒充维度。
            const auto id = BuiltinAssetIds::BlackEnvironmentCubeImage;
            AssetVersionRef<ImageAsset> version;
            bool usedFallback = false;
            if (!m_Assets.Acquire(id, version, usedFallback, error) || usedFallback) {
                ImageAsset image;
                image.sourceUri = "builtin:black-environment-cube";
                image.image.dimension = ImageDimension::Cube;
                image.image.width = image.image.height = 1;
                image.image.format = PixelFormat::RGBA8_UNorm;
                for (uint32_t face = 0; face < 6; ++face) {
                    image.image.subresources.push_back({ 0, 0, face, 1, 1, 1, uint64_t(face) * 4, 4, 4, 4 });
                    image.image.pixels.insert(image.image.pixels.end(), { std::byte{ 0 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 } });
                }
                const size_t bytes = sizeof(ImageAsset) + image.sourceUri.capacity() + 1 + image.image.pixels.capacity()
                    + image.image.subresources.capacity() * sizeof(ImageSubresource);
                if (!m_Assets.PublishBuiltin<ImageAsset>(id, 1, std::make_shared<const ImageAsset>(std::move(image)), bytes, error)
                    || !m_Assets.Acquire(id, version, usedFallback, error) || usedFallback)
                    return { .code = RHIErrorCode::InvalidState, .operation = "Renderer/default cube publication" };
            }
            if (const auto status = m_Textures.Acquire(version, m_BlackCube); !status.IsSuccess()) return status;
        }
        if (!m_EnvironmentSampler.HasValue()) {
            const RHISamplerDesc desc{ .minFilter = RHIFilter::Linear, .magFilter = RHIFilter::Linear,
                .addressV = RHISamplerAddressMode::ClampToEdge, .maxLod = 32 };
            if (const auto status = m_Device.CreateSampler(desc, m_EnvironmentSampler); !status.IsSuccess()) return status;
        }
        if (!m_NearestEnvironmentSampler.HasValue()) {
            const RHISamplerDesc desc{ .mipFilter = RHIFilter::Nearest,
                .addressV = RHISamplerAddressMode::ClampToEdge, .maxLod = 32 };
            if (const auto status = m_Device.CreateSampler(desc, m_NearestEnvironmentSampler); !status.IsSuccess()) return status;
        }
        if (!m_ShadowSampler.HasValue()) {
            const auto filter = m_Device.GetFormatSupport(m_Profile.depthFormat).linearFiltering ? RHIFilter::Linear : RHIFilter::Nearest;
            const RHISamplerDesc desc{ .minFilter = filter, .magFilter = filter, .mipFilter = filter,
                .addressU = RHISamplerAddressMode::ClampToEdge, .addressV = RHISamplerAddressMode::ClampToEdge,
                .compareEnabled = true, .compareOp = RHICompareOp::LessEqual };
            if (const auto status = m_Device.CreateSampler(desc, m_ShadowSampler); !status.IsSuccess()) return status;
            if (filter == RHIFilter::Nearest)
                CAT_CORE_INFO("Depth format lacks linear filtering; shadows use nearest comparison samples with the explicit 3x3 PCF kernel.");
        }
        return {};
    }

    RHIStatus Renderer::AcquireMaterialSampler(uint32_t flags, PixelFormat format, SamplerHandle& output)
    {
        if (flags > 255 || (flags & 3) > 2 || ((flags >> 2) & 3) > 2)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/source sampler flags" };
        uint32_t effective = flags;
        if (!m_Device.GetFormatSupport(format).linearFiltering) effective &= ~(16u | 32u | 64u);
        if (const auto found = m_MaterialSamplers.find(effective); found != m_MaterialSamplers.end()) {
            output = found->second;
            return {};
        }
        const auto address = [](uint32_t mode) {
            return mode == 1 ? RHISamplerAddressMode::ClampToEdge
                : mode == 2 ? RHISamplerAddressMode::MirroredRepeat : RHISamplerAddressMode::Repeat;
        };
        const RHISamplerDesc desc{
            .minFilter = effective & 16 ? RHIFilter::Linear : RHIFilter::Nearest,
            .magFilter = effective & 32 ? RHIFilter::Linear : RHIFilter::Nearest,
            .mipFilter = effective & 64 ? RHIFilter::Linear : RHIFilter::Nearest,
            .addressU = address(effective & 3), .addressV = address((effective >> 2) & 3),
            .maxLod = effective & 128 ? 32.0f : 0.0f
        };
        auto [found, inserted] = m_MaterialSamplers.emplace(effective, SamplerHandle{});
        const auto status = m_Device.CreateSampler(desc, found->second);
        if (!status.IsSuccess()) {
            m_Device.ReleaseSampler(found->second);
            m_MaterialSamplers.erase(found);
            return status;
        }
        output = found->second;
        return {};
    }

    RHIStatus Renderer::EnsurePipelines(const GeometryGpuResource& geometry, DrawResources& resources, bool reversedZ, bool mirrored)
    {
        constexpr const char* operation = "Renderer::EnsurePipelines";
        const auto& instance = *resources.material->asset.Get();
        const auto& definition = *instance.definition.Get();
        const auto& mesh = geometry.asset.Get()->mesh->GetData();
        if (definition.shadingModel == MaterialShadingModel::Pbr
            && (!geometry.hasSourceNormal || geometry.maximumVertexColor > 1))
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/PBR requires source normals and vertex reflectance in [0,1]" };
        const auto& shader = definition.shaderVersion;
        if (!shader.HasValue() || shader.GetId() != definition.shader.id)
            return { .code = RHIErrorCode::InvalidState, .operation = operation };
        bool hasUv = false, hasTangent = false;
        for (const auto& attribute : mesh.attributes) {
            hasUv |= attribute.semantic == VertexSemantic::TexCoord0;
            hasTangent |= attribute.semantic == VertexSemantic::Tangent;
        }
        for (const auto& texture : resources.material->surface.textures)
            if (texture.HasValue() && !hasUv)
                return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/material requires source UV" };
        if (resources.material->surface.textures[1].HasValue() && !hasTangent)
            return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/normal map requires source tangent" };

        const auto canonical = GeometryGpuResource::GetVertexLayout();
        const auto attributes = FilterVertexLayout(*shader.Get(), definition.vertexEntry, canonical);
        const VertexLayout layout{ canonical.stride, attributes };
        std::string error;
        if (!ValidateMaterialShaderInterface(definition, shader.Get()->metadata, layout, error)
            || !ValidateFrameShaderInterface(shader.Get()->metadata, definition.vertexEntry, definition.fragmentEntry, error)) {
            CAT_CORE_ERROR("Material/Frame/View/Draw shader ABI rejected: {0}", error);
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/material shader ABI" };
        }
        const bool transparent = definition.alphaMode == MaterialAlphaMode::Blend;
        if (!instance.renderState.depthTest || (transparent && instance.renderState.depthWrite)
            || (!transparent && !instance.renderState.depthWrite)
            || (transparent && instance.renderState.blend != MaterialBlendMode::Alpha
                && instance.renderState.blend != MaterialBlendMode::Additive)
            || (!transparent && instance.renderState.blend != MaterialBlendMode::Opaque))
            return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/surface depth and alpha policy" };
        std::vector<RHIBindingLayoutEntry> bindings;
        RHIPushConstantDesc push;
        if (!BuildPipelineLayout(*shader.Get(), definition.vertexEntry, definition.fragmentEntry, bindings, push))
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        // 通用资产反射仍可描述旧/自定义布局；普通表面GPU适配器只接受自己能完整绑定的ABI。
        // 在创建/声明draw之前拒绝，才能走材质回退，而不是到共享图录制时拖垮整个窗口。
        const auto hasBinding = [&](uint32_t index, RHIBindingType type) {
            return std::any_of(bindings.begin(), bindings.end(), [&](const auto& binding) {
                return binding.binding == index && binding.type == type;
            });
        };
        if (push.size != 0)
            return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/surface push constants are unsupported" };
        for (const auto& binding : bindings) {
            if (binding.type == RHIBindingType::UniformBuffer) {
                uint32_t expected = 0;
                switch (binding.binding) {
                case 0: expected = sizeof(FrameGpuParameters); break;
                case 1: expected = sizeof(ViewGpuParameters); break;
                case 2: expected = sizeof(DrawGpuParameters); break;
                case 3: expected = sizeof(MaterialGpuParameters); break;
                case 10: expected = sizeof(PackedLightingData); break;
                default: break;
                }
                if (expected == 0 || binding.minimumBufferBytes != expected)
                    return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/unsupported surface uniform ABI" };
            } else if (binding.type == RHIBindingType::SampledTexture) {
                uint32_t sampler = UINT32_MAX;
                auto viewType = RHITextureViewType::Texture2D;
                if (binding.binding >= 4 && binding.binding <= 8) sampler = binding.binding + 14;
                else if (binding.binding == 11) sampler = 12;
                else if (binding.binding >= 13 && binding.binding <= 15) {
                    sampler = 16;
                    if (binding.binding != 15) viewType = RHITextureViewType::Cube;
                } else if (binding.binding == 17) { sampler = 16; viewType = RHITextureViewType::Cube; }
                if (sampler == UINT32_MAX || binding.imageViewType != viewType
                    || binding.visibility != RHIShaderVisibility::Fragment || !hasBinding(sampler, RHIBindingType::Sampler))
                    return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/unsupported surface texture-sampler ABI" };
            } else {
                const bool paired = binding.binding >= 18 && binding.binding <= 22
                    ? hasBinding(binding.binding - 14, RHIBindingType::SampledTexture)
                    : binding.binding == 12 ? hasBinding(11, RHIBindingType::SampledTexture)
                    : binding.binding == 16 && (hasBinding(13, RHIBindingType::SampledTexture)
                        || hasBinding(14, RHIBindingType::SampledTexture) || hasBinding(15, RHIBindingType::SampledTexture)
                        || hasBinding(17, RHIBindingType::SampledTexture));
                if (!paired || binding.visibility != RHIShaderVisibility::Fragment)
                    return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/unsupported surface sampler ABI" };
            }
        }
        RHIGraphicsPipelineDesc desc;
        desc.vertex = { .asset = shader, .stage = RHIShaderStage::Vertex, .entryPoint = definition.vertexEntry.c_str() };
        desc.fragment = { .asset = shader, .stage = RHIShaderStage::Fragment, .entryPoint = definition.fragmentEntry.c_str() };
        desc.colorFormat = PixelFormat::RGBA32_Float;
        desc.depthFormat = m_Profile.depthFormat;
        const auto compare = reversedZ ? RHICompareOp::GreaterEqual : RHICompareOp::LessEqual;
        desc.depthState = { true, !transparent && !m_Profile.depthPrepass,
            !transparent && m_Profile.depthPrepass ? RHICompareOp::Equal : compare };
        desc.blendMode = !transparent ? RHIBlendMode::Opaque
            : instance.renderState.blend == MaterialBlendMode::Additive ? RHIBlendMode::Additive : RHIBlendMode::StraightAlpha;
        desc.vertexLayout = layout;
        desc.bindingLayout.entries = bindings;
        desc.pushConstants = push;
        desc.cullMode = instance.renderState.cull == MaterialCullMode::Back ? RHICullMode::Back
            : instance.renderState.cull == MaterialCullMode::Front ? RHICullMode::Front : RHICullMode::None;
        desc.frontFace = mirrored ? RHIFrontFace::Clockwise : RHIFrontFace::CounterClockwise;
        resources.reversedZ = reversedZ;
        resources.mirrored = mirrored;
        if (const auto status = m_Pipelines.Acquire(desc, resources.forward); !status.IsSuccess()) return status;

        // 深度/阴影/拾取都使用同一Mesh和Material裁切；不复用opaque无alpha片元造成漏影。
        const auto acquireAuxiliary = [&](std::string_view file, PixelFormat color, RHICompareOp comparison,
            std::shared_ptr<PipelineResource>& output) -> RHIStatus {
            // 同一双面规则也传给辅助通道；重新按完整key创建后不会覆盖其他材质的管线。
            AssetId id;
            AssetVersionRef<ShaderAsset> auxiliary;
            bool fallback = false;
            const bool graphSurface = definition.family == "catalpa.graph";
            if (graphSurface) auxiliary = shader;
            else if (!ShaderLibrary::BuiltinId(file, id, error) || !m_Assets.Acquire(id, auxiliary, fallback, error) || fallback)
                return { .code = RHIErrorCode::InvalidState, .operation = operation };
            // Equal预通道精确复用forward的VS版本/入口/输入，不能靠不同模块的Invariant
            // 声明来推定其算术相同。阴影与Picking独立使用LessEqual/GreaterEqual，无此替换。
            const bool prepass = file == "Depth.slang";
            const auto& vertexAsset = prepass ? resources.forward->vertex : auxiliary;
            const char* vertexEntry = prepass ? definition.vertexEntry.c_str() : graphSurface && file == "Shadow.slang" ? "graphShadowVertex" : "vertexMain";
            const char* fragmentEntry = graphSurface ? file == "Picking.slang" ? "graphPickingMain" : "graphDepthMain" : "fragmentMain";
            const auto auxiliaryAttributes = prepass ? attributes : FilterVertexLayout(*vertexAsset.Get(), vertexEntry, canonical);
            std::vector<RHIBindingLayoutEntry> auxiliaryBindings;
            RHIPushConstantDesc auxiliaryPush;
            if (!BuildPipelineLayout(*vertexAsset.Get(), vertexEntry, *auxiliary.Get(), fragmentEntry, auxiliaryBindings, auxiliaryPush))
                return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
            RHIGraphicsPipelineDesc auxiliaryDesc;
            auxiliaryDesc.vertex = { .asset = vertexAsset, .stage = RHIShaderStage::Vertex, .entryPoint = vertexEntry };
            auxiliaryDesc.fragment = { .asset = auxiliary, .stage = RHIShaderStage::Fragment, .entryPoint = fragmentEntry };
            auxiliaryDesc.colorFormat = color;
            auxiliaryDesc.depthFormat = m_Profile.depthFormat;
            auxiliaryDesc.depthState = { true, true, comparison };
            auxiliaryDesc.cullMode = desc.cullMode;
            auxiliaryDesc.frontFace = desc.frontFace;
            auxiliaryDesc.vertexLayout = { prepass ? layout.stride : canonical.stride, auxiliaryAttributes };
            auxiliaryDesc.bindingLayout.entries = auxiliaryBindings;
            auxiliaryDesc.pushConstants = auxiliaryPush;
            return m_Pipelines.Acquire(auxiliaryDesc, output);
        };
        if (!transparent && m_Profile.depthPrepass)
            if (const auto status = acquireAuxiliary("Depth.slang", PixelFormat::Unknown, compare, resources.depth); !status.IsSuccess()) return status;
        if (!transparent)
            if (const auto status = acquireAuxiliary("Shadow.slang", PixelFormat::Unknown, RHICompareOp::LessEqual, resources.shadow); !status.IsSuccess()) return status;
        return acquireAuxiliary("Picking.slang", PixelFormat::R32_UInt, compare, resources.picking);
    }

    RHIStatus Renderer::EnsureSurfaceResources(Surface& surface, const RenderFramePacket& packet,
        Extent2D extent, uint64_t generation, bool preview)
    {
        constexpr const char* operation = "Renderer::EnsureSurfaceResources";
        if (packet.views.size() != 1 || extent.IsEmpty() || extent.width > 8192 || extent.height > 8192 || generation == 0)
            return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
        if (!surface.targets || surface.targets->extent != extent || surface.targets->generation != generation) {
            const auto hdrSupport = m_Device.GetFormatSupport(PixelFormat::RGBA32_Float);
            if (!hdrSupport.colorAttachment || !hdrSupport.colorAttachmentBlend || !hdrSupport.sampledImage)
                return { .code = RHIErrorCode::Unsupported,
                    .operation = "Renderer/RGBA32F HDR color attachment, blending and sampling are required" };
            auto targets = std::make_shared<Targets>();
            targets->device = &m_Device;
            targets->extent = extent;
            targets->generation = generation;
            const auto create = [&](Targets::Image& image, Extent2D size, PixelFormat format, RHITextureUsage usage) -> RHIStatus {
                const auto support = m_Device.GetFormatSupport(format);
                if ((HasTextureUsage(usage, RHITextureUsage::Sampled) && !support.sampledImage)
                    || (HasTextureUsage(usage, RHITextureUsage::ColorAttachment) && !support.colorAttachment)
                    || (HasTextureUsage(usage, RHITextureUsage::DepthAttachment) && !support.depthAttachment))
                    return { .code = RHIErrorCode::Unsupported, .operation = "Renderer/target format support" };
                const RHITextureDesc desc{ .extent = size, .format = format, .usage = usage };
                auto status = m_Device.CreateTexture(desc, image.texture);
                RHITextureViewDesc viewDesc;
                viewDesc.format = format;
                if (GetPixelFormatInfo(format).isDepth) viewDesc.range.aspect = RHITextureAspect::Depth;
                if (status.IsSuccess()) status = m_Device.CreateTextureView(image.texture, viewDesc, image.view);
                return status;
            };
            const auto sampledColor = RHITextureUsage::Sampled | RHITextureUsage::ColorAttachment;
            if (const auto status = create(targets->hdr, extent, PixelFormat::RGBA32_Float, sampledColor); !status.IsSuccess()) return status;
            if (const auto status = create(targets->display, extent, PixelFormat::RGBA8_SRGB, sampledColor); !status.IsSuccess()) return status;
            if (const auto status = create(targets->depth, extent, m_Profile.depthFormat, RHITextureUsage::DepthAttachment); !status.IsSuccess()) return status;
            if (const auto status = create(targets->shadow, { 1024, 1024 }, m_Profile.depthFormat,
                RHITextureUsage::DepthAttachment | RHITextureUsage::Sampled); !status.IsSuccess()) return status;
            if (preview) {
                if (const auto status = create(targets->picking, extent, PixelFormat::R32_UInt,
                    RHITextureUsage::ColorAttachment | RHITextureUsage::TransferSource); !status.IsSuccess()) return status;
                if (const auto status = create(targets->pickingDepth, extent, m_Profile.depthFormat, RHITextureUsage::DepthAttachment); !status.IsSuccess()) return status;
                uint64_t token = 0;
                if (const auto status = m_UI.RegisterTexture(targets->display.view, targets, token); !status.IsSuccess()) return status;
                // 已发布token仍可能在隐藏面板恢复时被重新引用，必须活到新目标真实提交。
                // 这里只退休上一份从未发布的候选，避免连续resize堆积注册项。
                if (surface.token != surface.published.textureToken) m_UI.ReleaseTexture(surface.token);
                surface.token = token;
            }
            surface.targets = std::move(targets);
        }

        std::vector<InstanceGpu> instances;
        instances.reserve(packet.instances.size());
        for (size_t index = 0; index < packet.instances.size(); ++index) {
            const auto& instance = packet.instances[index];
            const bool mirrored = glm::determinant(glm::dmat3{ Math::Mat3{ instance.worldMatrix } }) < 0;
            InstanceGpu gpu;
            if (instance.mesh->GetData().vertexCount == 0) { instances.push_back(std::move(gpu)); continue; }
            if (!instance.meshVersion.HasValue() || instance.materials.size() != instance.meshVersion.Get()->materialSlots.size())
                return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/fixed asset snapshots required" };
            if (const auto status = m_Geometry.Acquire(instance.meshVersion, gpu.geometry); !status.IsSuccess()) return status;
            for (size_t slot = 0; slot < instance.materials.size(); ++slot) {
                const auto& material = instance.materials[slot];
                DrawResources draw;
                auto status = m_Materials.Acquire(material, draw.material);
                if (status.IsSuccess()) status = EnsurePipelines(*gpu.geometry, draw, packet.views[0].lens.reversedZ, mirrored);
                if (!status.IsSuccess()) {
                    if (status.code == RHIErrorCode::DeviceLost || status.code == RHIErrorCode::OutOfMemory || status.code == RHIErrorCode::BackendFailure) return status;
                    if (m_ReportedMaterialFailures.size() < 256
                        && m_ReportedMaterialFailures.emplace(material.GetId(), material.GetVersion()).second)
                        CAT_CORE_WARN("Material {0} version {1} rejected at {2}; retain the usable version or use the explicit error material.",
                            material.GetId().ToString(), material.GetVersion(), status.operation);
                    if (index < surface.instances.size() && surface.instances[index].geometry
                        && surface.instances[index].geometry->asset.Get() == gpu.geometry->asset.Get()
                        && slot < surface.instances[index].materials.size()
                        && surface.instances[index].materials[slot].reversedZ == packet.views[0].lens.reversedZ
                        && surface.instances[index].materials[slot].mirrored == mirrored
                        && surface.instances[index].materials[slot].material->asset.Get()->renderState.blend == material.Get()->renderState.blend
                        && surface.instances[index].materials[slot].material->asset.GetId() == material.GetId()) {
                        gpu.materials.push_back(surface.instances[index].materials[slot]);
                        continue;
                    }
                    AssetVersionRef<MaterialInstance> errorMaterial;
                    bool fallback = false;
                    std::string error;
                    if (!m_Assets.Acquire(BuiltinAssetIds::ErrorInstance, errorMaterial, fallback, error) || fallback) return status;
                    if (const auto result = m_Materials.Acquire(errorMaterial, draw.material); !result.IsSuccess()) return result;
                    if (const auto result = EnsurePipelines(*gpu.geometry, draw, packet.views[0].lens.reversedZ, mirrored); !result.IsSuccess()) return result;
                }
                for (size_t texture = 0; texture < draw.textures.size(); ++texture) {
                    draw.textures[texture] = texture == 1 ? m_FlatNormal : m_White;
                    const auto handle = draw.material->surface.textures[texture];
                    if (!handle.HasValue()) continue;
                    const auto& image = draw.material->asset.Get()->textureVersions[texture];
                    if (!image.HasValue() || image.GetId() != handle.id)
                        return { .code = RHIErrorCode::InvalidState, .operation = "Renderer/material requires fixed image dependency" };
                    if (const auto result = m_Textures.Acquire(image, draw.textures[texture]); !result.IsSuccess()) return result;
                }
                for (size_t textureIndex = 0; textureIndex < draw.textures.size(); ++textureIndex) {
                    const auto& texture = draw.textures[textureIndex];
                    if (const auto result = AcquireMaterialSampler(draw.material->surface.samplerFlags[textureIndex],
                        texture->asset.Get()->image.format, draw.samplers[textureIndex]); !result.IsSuccess()) return result;
                    if (!m_Device.GetFormatSupport(texture->asset.Get()->image.format).linearFiltering) {
                        if (m_ReportedMaterialFailures.size() < 256
                            && m_ReportedMaterialFailures.emplace(texture->asset.GetId(), texture->asset.GetVersion()).second)
                            CAT_CORE_WARN("Image {0} format does not support linear filtering; using explicit nearest sampling without changing its pixels.",
                                texture->asset.GetId().ToString());
                    }
                }
                gpu.materials.push_back(std::move(draw));
            }
            instances.push_back(std::move(gpu));
        }
        surface.instances.swap(instances);
        surface.environment.fill(m_BlackCube);
        surface.environment[3] = m_White;
        surface.environmentVersion = {};
        surface.environmentSampler = m_EnvironmentSampler;
        if (packet.environment && packet.environment->version.HasValue()) {
            std::string error;
            const auto& environment = packet.environment->version;
            const auto* asset = environment.Get();
            const std::array images{ &asset->radiance, &asset->irradiance, &asset->specular, &asset->brdfLut };
            for (size_t image = 0; image < images.size(); ++image) {
                AssetVersionRef<ImageAsset> version;
                if (!AssetVersionRef<ImageAsset>::TryCreate(environment.GetId(), environment.GetVersion(),
                    std::shared_ptr<const ImageAsset>(environment.GetShared(), images[image]), version, error))
                    return { .code = RHIErrorCode::InvalidDescription, .operation = operation };
                if (const auto status = m_Textures.AcquireEmbedded(version,
                    static_cast<uint32_t>(image + 1), surface.environment[image]); !status.IsSuccess()) return status;
            }
            surface.environmentVersion = environment;
        }
        for (const auto& image : surface.environment)
            if (!m_Device.GetFormatSupport(image->asset.Get()->image.format).linearFiltering) {
                surface.environmentSampler = m_NearestEnvironmentSampler;
                const auto id = surface.environmentVersion.HasValue() ? surface.environmentVersion.GetId() : image->asset.GetId();
                const auto version = surface.environmentVersion.HasValue() ? surface.environmentVersion.GetVersion() : image->asset.GetVersion();
                if (m_ReportedMaterialFailures.size() < 256 && m_ReportedMaterialFailures.emplace(id, version).second)
                    CAT_CORE_WARN("Environment {0} format does not support linear filtering; using explicit nearest sampling and preserving HDR radiance.", id.ToString());
            }
        if (const auto status = AcquireBuiltinPipeline("Tonemap.slang", PixelFormat::RGBA8_SRGB, PixelFormat::Unknown,
            {}, {}, surface.tonemap); !status.IsSuccess()) return status;
        if (!preview)
            if (const auto status = AcquireBuiltinPipeline("Present.slang", m_Swapchain.GetInfo().format, PixelFormat::Unknown,
                {}, {}, surface.present); !status.IsSuccess()) return status;
        if (surface.environmentVersion.HasValue() && packet.environment && packet.environment->settings.drawBackground)
            if (const auto status = AcquireBuiltinPipeline("Skybox.slang", PixelFormat::RGBA32_Float, PixelFormat::Unknown,
                {}, {}, surface.skybox); !status.IsSuccess()) return status;
        if (!BuildDirectionalShadowView(packet, 1024, surface.shadow).IsSuccess())
            return { .code = RHIErrorCode::InvalidDescription, .operation = "Renderer/shadow projection" };
        for (size_t light = 0; light < packet.lights.size(); ++light)
            if (packet.lights[light].parameters.castShadows && (!surface.shadow.enabled || surface.shadow.lightIndex != light)
                && m_ReportedShadowRequests.size() < 256 && m_ReportedShadowRequests.insert(packet.lights[light].id).second)
                CAT_CORE_WARN("Light index {0} requests a shadow outside the one-directional-light budget; its direct illumination remains enabled.", light);
        return {};
    }
}
