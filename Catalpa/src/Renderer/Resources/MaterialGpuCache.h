#pragma once
#include "Materials/MaterialInstance.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RHIDevice;
    struct MaterialGpuResource {
        RHIDevice* device = nullptr;
        AssetVersionRef<MaterialInstance> asset;
        BufferHandle parameters{};
        ~MaterialGpuResource();
    };
    // 当前Unlit的16字节Material UBO；Frame/View/Draw继续由RendererFrameContext拥有。
    class CATALPA_API MaterialGpuCache {
    public:
        explicit MaterialGpuCache(RHIDevice& device) : m_Device(device) {}
        [[nodiscard]] RHIStatus Acquire(const AssetVersionRef<MaterialInstance>&, std::shared_ptr<MaterialGpuResource>& output);
        void Prune();
        void Clear() noexcept { m_Entries.clear(); }
    private:
        RHIDevice& m_Device;
        std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<MaterialGpuResource>> m_Entries;
    };
}
