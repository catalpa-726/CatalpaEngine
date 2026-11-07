#include "catpch.h"
#include <bit>
#include "GameUIPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
namespace Catalpa {
    namespace {
        uint64_t NextTextureIdentity() {
            static uint64_t next = 0;
            if (next == UINT64_MAX)
                throw std::overflow_error("Game UI texture identities exhausted");
            return ++next;
        }
    } // namespace
    GameUIPass::~GameUIPass() {
        m_Device.ReleaseSampler(m_Sampler);
    }
    RHIStatus GameUIPass::Prepare(uint32_t slot, const UIDrawData& data,
                                  std::shared_ptr<PipelineResource> pipeline) {
        UIDrawData normalized = data;
        static const auto white = [] {
            auto texture = std::make_shared<UITextureData>();
            texture->width = texture->height = 1;
            texture->generation = 1;
            texture->srgb = false;
            texture->rgba8.assign(4, std::byte{255});
            return std::shared_ptr<const UITextureData>(std::move(texture));
        }();
        for (auto& batch : normalized.batches)
            if (!batch.texture)
                batch.texture = white;
        if (slot >= m_Frames.size() || normalized.vertices.size() > 262144 ||
            normalized.indices.size() > 1048576 || normalized.batches.size() > 4096)
            return {RHIErrorCode::InvalidDescription, 0, "GameUI/draw budget"};
        auto& frame = m_Frames[slot];
        frame.data = {};
        frame.textures.clear();
        frame.pipeline = std::move(pipeline);
        std::erase_if(m_Cache, [](const auto& entry) {
            return entry.source.use_count() == 1 && entry.gpu.use_count() <= 2;
        });
        if (normalized.batches.empty())
            return {};
        if (!frame.pipeline || !normalized.width || !normalized.height || normalized.vertices.empty() ||
            normalized.indices.empty())
            return {RHIErrorCode::InvalidDescription, 0, "GameUI/frame"};
        for (const auto& vertex : normalized.vertices)
            for (float value : std::bit_cast<std::array<float, 8>>(vertex))
                if (!std::isfinite(value))
                    return {RHIErrorCode::InvalidDescription, 0, "GameUI/nonfinite vertex"};
        for (const auto& batch : normalized.batches) {
            if (batch.firstIndex > normalized.indices.size() ||
                batch.indexCount > normalized.indices.size() - batch.firstIndex || batch.indexCount % 3)
                return {RHIErrorCode::InvalidDescription, 0, "GameUI/index range"};
            for (uint32_t index = batch.firstIndex; index < batch.firstIndex + batch.indexCount; ++index) {
                const int64_t vertex = int64_t(normalized.indices[index]) + batch.baseVertex;
                if (vertex < 0 || uint64_t(vertex) >= normalized.vertices.size())
                    return {RHIErrorCode::InvalidDescription, 0, "GameUI/base vertex"};
            }
            const auto& source = batch.texture;
            if (!source || !source->width || !source->height ||
                source->rgba8.size() != uint64_t(source->width) * source->height * 4 ||
                source->rgba8.size() > 64 * 1024 * 1024)
                return {RHIErrorCode::InvalidDescription, 0, "GameUI/texture"};
            auto found = std::find_if(m_Cache.begin(), m_Cache.end(),
                                      [&](const auto& entry) { return entry.source == source; });
            if (found == m_Cache.end()) {
                if (m_Cache.size() >= 128 || m_NextTexture == UINT64_MAX)
                    return {RHIErrorCode::OutOfMemory, 0, "GameUI/texture budget"};
                ImageAsset image;
                image.sourceUri = "runtime:game-ui";
                image.image.width = source->width;
                image.image.height = source->height;
                image.image.format = PixelFormat::RGBA8_UNorm;
                image.image.pixels = source->rgba8;
                image.image.subresources.push_back({0, 0, 0, source->width, source->height, 1, 0,
                                                    uint64_t(source->width) * 4,
                                                    uint64_t(source->width) * source->height * 4,
                                                    uint64_t(source->width) * source->height * 4});
                AssetVersionRef<ImageAsset> version;
                std::string error;
                if (!AssetVersionRef<ImageAsset>::TryCreate(
                        AssetId::FromWords(0x4341545549544558ULL, NextTextureIdentity()), 1,
                        std::make_shared<const ImageAsset>(std::move(image)), version, error))
                    return {RHIErrorCode::InvalidDescription, 0, "GameUI/texture version"};
                TextureEntry entry{source};
                if (const auto status = m_Textures.Acquire(version, entry.gpu); !status.IsSuccess())
                    return status;
                m_Cache.push_back(std::move(entry));
                found = std::prev(m_Cache.end());
            }
            frame.textures.push_back(found->gpu);
        }
        if (!m_Sampler.HasValue()) {
            const RHISamplerDesc desc{.minFilter = RHIFilter::Linear,
                                      .magFilter = RHIFilter::Linear,
                                      .addressU = RHISamplerAddressMode::ClampToEdge,
                                      .addressV = RHISamplerAddressMode::ClampToEdge};
            if (const auto status = m_Device.CreateSampler(desc, m_Sampler); !status.IsSuccess())
                return status;
        }
        if (const auto status =
                frame.vertices.Ensure(m_Device, normalized.vertices.size() * sizeof(UIDrawVertex),
                                      RHIBufferUsage::Vertex, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status = frame.indices.Ensure(m_Device, normalized.indices.size() * 4,
                                                     RHIBufferUsage::Index, RHIMemoryDomain::Upload);
            !status.IsSuccess())
            return status;
        if (const auto status =
                m_Device.WriteBuffer(frame.vertices.buffer, 0, std::as_bytes(std::span{normalized.vertices}));
            !status.IsSuccess())
            return status;
        if (const auto status =
                m_Device.WriteBuffer(frame.indices.buffer, 0, std::as_bytes(std::span{normalized.indices}));
            !status.IsSuccess())
            return status;
        frame.data = std::move(normalized);
        return {};
    }
    RHIStatus GameUIPass::Add(RenderGraph& graph, RGTexture target, Extent2D extent, uint32_t slot) {
        auto& frame = m_Frames.at(slot);
        for (size_t index = 0; index < frame.data.batches.size(); ++index) {
            const auto& batch = frame.data.batches[index];
            // UIContext输出目标像素尺寸。resize本帧尚未同步时只跳旧UI，不能越界裁剪。
            if (frame.data.width != extent.width || frame.data.height != extent.height)
                return {};
            const int64_t x = std::clamp<int64_t>(batch.scissor[0], 0, extent.width),
                          y = std::clamp<int64_t>(batch.scissor[1], 0, extent.height);
            const int64_t right = std::clamp<int64_t>(
                int64_t(batch.scissor[0]) + std::max(0, batch.scissor[2]), x, extent.width);
            const int64_t bottom = std::clamp<int64_t>(
                int64_t(batch.scissor[1]) + std::max(0, batch.scissor[3]), y, extent.height);
            if (right == x || bottom == y || !batch.indexCount)
                continue;
            RGIndexedDrawDesc draw;
            draw.color = target;
            draw.pipeline = frame.pipeline->pipeline;
            draw.indexCount = batch.indexCount;
            draw.firstIndex = batch.firstIndex;
            draw.baseVertex = batch.baseVertex;
            draw.loadOp = RHILoadOp::Load;
            draw.viewport = {.width = float(extent.width), .height = float(extent.height)};
            draw.scissor = {{int32_t(x), int32_t(y)}, {uint32_t(right - x), uint32_t(bottom - y)}};
            if (const auto status = graph.ImportBuffer(frame.vertices.buffer, draw.vertices);
                !status.IsSuccess())
                return status;
            if (const auto status = graph.ImportBuffer(frame.indices.buffer, draw.indices);
                !status.IsSuccess())
                return status;
            RGTexture texture;
            if (const auto status = graph.ImportTexture(frame.textures[index]->view, texture);
                !status.IsSuccess())
                return status;
            draw.sampledTextures.push_back({texture, m_Sampler, 0, 1});
            const std::array<float, 8> parameters{2.0f / extent.width,
                                                  2.0f / extent.height,
                                                  -1,
                                                  -1,
                                                  frame.data.batches[index].texture->srgb ? 1.0f : 0.0f,
                                                  0,
                                                  0,
                                                  0};
            const auto bytes = std::as_bytes(std::span{parameters});
            draw.pushConstants.assign(bytes.begin(), bytes.end());
            if (const auto status = graph.AddDrawPass("Game UI", draw); !status.IsSuccess())
                return status;
        }
        return {};
    }
} // namespace Catalpa
