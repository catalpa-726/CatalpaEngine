#include "catpch.h"
#include "RHI/RHIPipelineDesc.h"

namespace Catalpa {
    bool ValidateRHIComputeShaderInterface(const RHIComputePipelineDesc& desc, std::string& error) {
        if (desc.shader.stage != RHIShaderStage::Compute || !ValidateRHIShader(desc.shader, error))
            return false;
        const auto* entry = FindShaderEntry(desc.shader.asset.Get()->metadata, desc.shader.entryPoint, ShaderStage::Compute);
        if (!entry || entry->pushConstantBytes != desc.pushConstants.size ||
            (entry->pushConstantBytes && desc.pushConstants.visibility != RHIShaderVisibility::Compute) ||
            entry->bindings.size() != desc.bindingLayout.entries.size()) {
            error = "Compute pipeline push constant or binding count mismatch";
            return false;
        }
        std::set<uint32_t> bindings;
        for (const auto& layout : desc.bindingLayout.entries) {
            const auto reflected =
                std::find_if(entry->bindings.begin(), entry->bindings.end(), [&](const auto& b) {
                    return b.set == 0 && b.binding == layout.binding && b.count == 1;
                });
            if (!bindings.insert(layout.binding).second ||
                layout.visibility != RHIShaderVisibility::Compute || reflected == entry->bindings.end()) {
                error = "Compute layout requires unique reflected set0 bindings";
                return false;
            }
            const auto expected =
                reflected->kind == ShaderResourceKind::UniformBuffer   ? RHIBindingType::UniformBuffer
                : reflected->kind == ShaderResourceKind::StorageBuffer ? RHIBindingType::StorageBuffer
                : reflected->kind == ShaderResourceKind::SampledImage  ? RHIBindingType::SampledTexture
                                                                       : RHIBindingType::Sampler;
            if ((reflected->kind != ShaderResourceKind::UniformBuffer &&
                 reflected->kind != ShaderResourceKind::StorageBuffer &&
                 reflected->kind != ShaderResourceKind::SampledImage &&
                 reflected->kind != ShaderResourceKind::Sampler) ||
                expected != layout.type || layout.minimumBufferBytes != reflected->blockSize) {
                error = "Compute resource kind or buffer layout differs from final SPIR-V";
                return false;
            }
        }
        error.clear();
        return true;
    }
} // namespace Catalpa
