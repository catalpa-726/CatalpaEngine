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
        constexpr const char* op = "TextureGpuCache::Acquire";
        std::string error;
        if (!asset.HasValue() || !ValidateImageAsset(*asset.Get(), error))
            return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto& image = asset.Get()->image;
        const auto format = GetPixelFormatInfo(image.format);
        // 现有UploadScheduler仅支持紧密2D/单mip/单层；领域格式丰富不等于后端能力已经实现。
        if (image.dimension != ImageDimension::Texture2D || image.depth != 1 || image.arrayLayers != 1 || image.mipLevels != 1
            || image.subresources.size() != 1 || format.isCompressed || format.isDepth
            || !m_Device.GetFormatSupport(image.format).sampledImage)
            return { .code = RHIErrorCode::Unsupported, .operation = op };
        const auto& sub = image.subresources.front();
        if (sub.offset != 0 || sub.rowPitch != uint64_t(image.width) * format.bytesPerBlock
            || sub.slicePitch != sub.rowPitch * image.height || sub.byteLength != image.pixels.size())
            return { .code = RHIErrorCode::Unsupported, .operation = op };
        const auto key = std::make_pair(asset.GetId(), asset.GetVersion());
        auto found = m_Entries.find(key);
        if (found != m_Entries.end() && found->second->asset.Get() != asset.Get())
            return { .code = RHIErrorCode::InvalidState, .operation = "TextureGpuCache/identity-version collision" };
        if (found == m_Entries.end()) {
            auto resource = std::make_shared<TextureGpuResource>();
            resource->device = &m_Device; resource->asset = asset;
            found = m_Entries.emplace(key, std::move(resource)).first;
        }
        auto& resource = *found->second;
        if (!resource.texture.HasValue()) {
            const RHITextureDesc desc{ .extent = {image.width,image.height}, .format = image.format, .usage = RHITextureUsage::Sampled };
            if (const auto status = m_Uploads.CreateTexture(image.pixels, desc, resource.texture, resource.view); !status.IsSuccess()) return status;
        }
        output = found->second; return {};
    }
    void TextureGpuCache::OnSubmitted() noexcept { for (auto& [key, value] : m_Entries) value->uploadPending = false; }
    void TextureGpuCache::Prune() {
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
            if (!it->second->uploadPending && it->second.use_count() == 1) it = m_Entries.erase(it); else ++it;
    }
}
