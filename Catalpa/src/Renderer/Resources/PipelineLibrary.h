#pragma once
#include "RHI/RHIPipelineDesc.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
namespace Catalpa {
    class RHIDevice;
    struct PipelineResource {
        RHIDevice* device = nullptr;
        GraphicsPipelineHandle pipeline{};
        AssetVersionRef<ShaderAsset> vertex, fragment;
        std::vector<RHIBindingLayoutEntry> bindings;
        ~PipelineResource();
    };
    // 一个device的唯一管线owner缓存，键含完整shader版本/入口/布局/附件和状态。
    class CATALPA_API PipelineLibrary {
    public:
        explicit PipelineLibrary(RHIDevice& device) : m_Device(device) {}
        [[nodiscard]] RHIStatus Acquire(const RHIGraphicsPipelineDesc&, std::shared_ptr<PipelineResource>& output);
        void Prune();
        void Clear() noexcept { m_Entries.clear(); }
        [[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }
    private:
        RHIDevice& m_Device;
        std::map<std::string, std::shared_ptr<PipelineResource>> m_Entries;
    };
}
