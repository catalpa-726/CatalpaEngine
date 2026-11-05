#pragma once
#include "Materials/MaterialInstance.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "Materials/MaterialSurface.h"
namespace Catalpa {
    // 与Common/Surface.slang按字段对应，不上传variant/领域结构内存。
    struct alignas(16) MaterialGpuParameters {
        std::array<float, 4> baseColor{};
        std::array<float, 4> emissiveMetallic{};
        std::array<float, 4> roughnessNormalOcclusionCutoff{};
        std::array<float, 4> uvScaleOffset{};
        std::array<float, 4> timeEffect{};
    };
    static_assert(sizeof(MaterialGpuParameters) == 80 && offsetof(MaterialGpuParameters, emissiveMetallic) == 16
        && offsetof(MaterialGpuParameters, roughnessNormalOcclusionCutoff) == 32
        && offsetof(MaterialGpuParameters, uvScaleOffset) == 48 && offsetof(MaterialGpuParameters, timeEffect) == 64);
    class RHIDevice;
    struct MaterialGpuResource {
        RHIDevice* device = nullptr;
        AssetVersionRef<MaterialInstance> asset;
        BufferHandle parameters{};
        MaterialSurfaceParameters surface;
        ~MaterialGpuResource();
    };
    // 普通PBR/Unlit共用80字节Material UBO；Frame/View/Draw由帧上下文拥有。
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
