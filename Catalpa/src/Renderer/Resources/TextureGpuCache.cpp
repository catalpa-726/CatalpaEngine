#include "catpch.h"
#include "Renderer/Resources/TextureGpuCache.h"
#include "Renderer/Uploads/UploadScheduler.h"
#include "Images/ImageValidation.h"
#include "RHI/RHIDevice.h"
namespace Catalpa {
    TextureGpuResource::~TextureGpuResource() {
        if (device) { device->ReleaseTextureView(view); device->ReleaseTexture(texture); }
    }
    RHIStatus TextureGpuCache::Acquire(const AssetVersionRef<ImageAsset>& asset, std::shared_ptr<TextureGpuResource>& output)
    {
        return AcquireInternal(asset, 0, output);
    }
    RHIStatus TextureGpuCache::AcquireEmbedded(const AssetVersionRef<ImageAsset>& asset, uint32_t slot,
        std::shared_ptr<TextureGpuResource>& output)
    {
        if (slot == 0 || slot > 4)
            return { .code = RHIErrorCode::InvalidDescription, .operation = "TextureGpuCache/embedded slot" };
        return AcquireInternal(asset, slot, output);
    }
    RHIStatus TextureGpuCache::AcquireInternal(const AssetVersionRef<ImageAsset>& asset, uint32_t slot,
        std::shared_ptr<TextureGpuResource>& output)
    {
        constexpr const char* op = "TextureGpuCache::Acquire";
        std::string error;
        if (!asset.HasValue())
            return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto key = std::make_tuple(asset.GetId(), asset.GetVersion(), slot);
        auto found = m_Entries.find(key);
        if (found != m_Entries.end()) {
            if (found->second->asset.Get() != asset.Get())
                return { .code = RHIErrorCode::InvalidState, .operation = "TextureGpuCache/identity-version collision" };
            if (found->second->view.HasValue()) { output = found->second; return {}; }
        }
        if (!ValidateImageAsset(*asset.Get(), error)) return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto& image = asset.Get()->image;
        const auto format = GetPixelFormatInfo(image.format);
        const bool cube = image.dimension == ImageDimension::Cube;
        if ((!cube && image.dimension != ImageDimension::Texture2D) || image.depth != 1 || image.arrayLayers != 1
            || format.isCompressed || format.isDepth || format.isInteger
            || !m_Device.GetFormatSupport(image.format).sampledImage)
            return { .code = RHIErrorCode::Unsupported, .operation = op };
        std::vector<RHIBufferTextureCopy> copies;
        for (const auto& sub : image.subresources) {
            if (sub.rowPitch != uint64_t(sub.width) * format.bytesPerBlock
                || sub.slicePitch != sub.rowPitch * sub.height || sub.byteLength != sub.slicePitch)
                return { .code = RHIErrorCode::Unsupported, .operation = op };
            copies.push_back({ .bufferOffset = sub.offset, .range = { .baseMipLevel = sub.mip,
                .baseArrayLayer = cube ? sub.layer * 6 + sub.face : sub.layer } });
        }
        if (found == m_Entries.end()) {
            auto resource = std::make_shared<TextureGpuResource>();
            resource->device = &m_Device; resource->asset = asset;
            found = m_Entries.emplace(key, std::move(resource)).first;
        }
        auto& resource = *found->second;
        if (!resource.texture.HasValue()) {
            const RHITextureDesc desc{ .extent = {image.width,image.height}, .format = image.format,
                .usage = RHITextureUsage::Sampled, .mipLevels = image.mipLevels,
                .arrayLayers = cube ? image.arrayLayers * 6 : image.arrayLayers, .cubeCompatible = cube };
            if (const auto status = m_Uploads.CreateTexture(image.pixels, desc, copies, resource.texture, resource.view); !status.IsSuccess()) return status;
        }
        output = found->second; return {};
    }
    void TextureGpuCache::OnSubmitted() noexcept { for (auto& [key, value] : m_Entries) value->uploadPending = false; }
    void TextureGpuCache::Prune() {
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
            if (!it->second->uploadPending && it->second.use_count() == 1) it = m_Entries.erase(it); else ++it;
    }
}
