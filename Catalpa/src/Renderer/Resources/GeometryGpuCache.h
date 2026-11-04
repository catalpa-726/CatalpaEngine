#pragma once
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RHIDevice; class UploadScheduler;
    // 唯一buffer owner；固定CPU版本保活，后端命令引用继续负责GPU退休。
    struct GeometryGpuResource {
        RHIDevice* device = nullptr;
        AssetVersionRef<MeshAsset> asset;
        BufferHandle vertices{}, indices{};
        bool uploadPending = true;
        ~GeometryGpuResource();
    };
    class CATALPA_API GeometryGpuCache {
    public:
        GeometryGpuCache(RHIDevice& device, UploadScheduler& uploads) : m_Device(device), m_Uploads(uploads) {}
        [[nodiscard]] RHIStatus Acquire(const AssetVersionRef<MeshAsset>&, std::shared_ptr<GeometryGpuResource>& output);
        void OnSubmitted() noexcept;
        void Prune();
        void Clear() noexcept { m_Entries.clear(); } // caller先关闭pending上传
        [[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }
    private:
        RHIDevice& m_Device; UploadScheduler& m_Uploads;
        std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<GeometryGpuResource>> m_Entries;
    };
}
