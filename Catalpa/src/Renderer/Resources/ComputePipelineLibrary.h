#pragma once
#include "RHI/RHIPipelineDesc.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RHIDevice;
    class AssetManager;
    struct ComputePipelineResource {
        RHIDevice* device = nullptr;
        ComputePipelineHandle pipeline{};
        AssetVersionRef<ShaderAsset> shader;
        std::vector<RHIBindingLayoutEntry> bindings;
        uint32_t pushBytes = 0;
        ~ComputePipelineResource();
    };
    class ComputePipelineLibrary {
      public:
        ComputePipelineLibrary(RHIDevice& device, AssetManager& assets)
            : m_Device(device), m_Assets(assets) {}
        [[nodiscard]] RHIStatus Acquire(std::string_view file, std::shared_ptr<ComputePipelineResource>&);
        void Prune();
        void Clear() noexcept {
            m_Entries.clear();
        }

      private:
        RHIDevice& m_Device;
        AssetManager& m_Assets;
        std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<ComputePipelineResource>> m_Entries;
    };
} // namespace Catalpa
