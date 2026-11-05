#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"
#include "Images/ImageAsset.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RHIDevice; class UploadScheduler;
    struct TextureGpuResource {
        RHIDevice* device = nullptr;
        AssetVersionRef<ImageAsset> asset;
        TextureHandle texture{}; TextureViewHandle view{};
        bool uploadPending = true;
        ~TextureGpuResource();
    };
    // 图片身份/版本复用纹理与view；sampler策略独立，CPU布局不被静默重排。
    class CATALPA_API TextureGpuCache {
    public:
        TextureGpuCache(RHIDevice& device, UploadScheduler& uploads) : m_Device(device), m_Uploads(uploads) {}
        [[nodiscard]] RHIStatus Acquire(const AssetVersionRef<ImageAsset>&, std::shared_ptr<TextureGpuResource>& output);
        // 环境载荷内嵌图像使用独立slot命名空间；alias保活同一父载荷，不伪造全局AssetId。
        [[nodiscard]] RHIStatus AcquireEmbedded(const AssetVersionRef<ImageAsset>&, uint32_t slot,
            std::shared_ptr<TextureGpuResource>& output);
        void OnSubmitted() noexcept;
        void Prune();
        void Clear() noexcept { m_Entries.clear(); }
        [[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }
    private:
        RHIDevice& m_Device; UploadScheduler& m_Uploads;
        [[nodiscard]] RHIStatus AcquireInternal(const AssetVersionRef<ImageAsset>&, uint32_t slot,
            std::shared_ptr<TextureGpuResource>& output);
        std::map<std::tuple<AssetId, uint64_t, uint32_t>, std::shared_ptr<TextureGpuResource>> m_Entries;
    };
}
