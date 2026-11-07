#pragma once
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "RHI/RHICompletionPoint.h"
namespace Catalpa {
    class RHIDevice;
    class UploadScheduler;
    class ResourceResidencyManager;
    // 唯一buffer owner；固定CPU版本保活，后端命令引用继续负责GPU退休。
    struct GeometryGpuResource {
        RHIDevice* device = nullptr;
        AssetVersionRef<MeshAsset> asset;
        BufferHandle vertices{}, indices{};
        bool uploadPending = true;
        bool acquired = false;
        bool stale = false;
        bool hasSourceNormal = false;
        float maximumVertexColor = 1.0f;
        ~GeometryGpuResource();
        [[nodiscard]] static CATALPA_API VertexLayout GetVertexLayout() noexcept;
    };
    class CATALPA_API GeometryGpuCache {
    public:
      GeometryGpuCache(RHIDevice& device, UploadScheduler& uploads,
                       ResourceResidencyManager* residency = nullptr)
          : m_Device(device), m_Uploads(uploads), m_Residency(residency) {}
      [[nodiscard]] RHIStatus Acquire(const AssetVersionRef<MeshAsset>&,
                                      std::shared_ptr<GeometryGpuResource>& output);
      void OnSubmitted(std::span<const RHICompletionPoint> points = {}) noexcept;
      void Prune();
      void Invalidate(AssetId id, uint64_t currentVersion) noexcept;
      void MarkUses(std::span<const RHICompletionPoint>) noexcept;
      void Clear() noexcept; // caller先关闭pending上传
      [[nodiscard]] size_t Size() const noexcept {
          return m_Entries.size();
      }

    private:
        RHIDevice& m_Device; UploadScheduler& m_Uploads;
        ResourceResidencyManager* m_Residency = nullptr;
        std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<GeometryGpuResource>> m_Entries;
    };
}
