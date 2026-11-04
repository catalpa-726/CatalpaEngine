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
        void OnSubmitted() noexcept;
        void Prune();
        void Clear() noexcept { m_Entries.clear(); }
        [[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }
    private:
        RHIDevice& m_Device; UploadScheduler& m_Uploads;
        std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<TextureGpuResource>> m_Entries;
    };
}
