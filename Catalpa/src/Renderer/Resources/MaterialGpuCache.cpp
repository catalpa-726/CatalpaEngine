#include "catpch.h"
#include "Renderer/Resources/MaterialGpuCache.h"
#include "Materials/MaterialValidation.h"
#include "RHI/RHIDevice.h"
namespace Catalpa {
    MaterialGpuResource::~MaterialGpuResource() { if (device) device->ReleaseBuffer(parameters); }
    RHIStatus MaterialGpuCache::Acquire(const AssetVersionRef<MaterialInstance>& asset, std::shared_ptr<MaterialGpuResource>& output)
    {
        constexpr const char* op = "MaterialGpuCache::Acquire";
        std::string error;
        if (!asset.HasValue() || !ValidateMaterialInstance(*asset.Get(), error))
            return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto& value = *asset.Get();
        const auto& definition = *value.definition.Get();
        if (definition.shadingModel != MaterialShadingModel::Unlit || definition.alphaMode != MaterialAlphaMode::Opaque
            || value.renderState.blend != MaterialBlendMode::Opaque || !value.renderState.depthTest || !value.renderState.depthWrite
            || definition.parameters.size() != 1 || definition.parameters.front().id != "base_color")
            return { .code = RHIErrorCode::Unsupported, .operation = op };
        const auto* parameter = FindMaterialParameter(value, "base_color");
        const auto* color = parameter ? std::get_if<std::array<float, 4>>(parameter) : nullptr;
        if (!color) return { .code = RHIErrorCode::InvalidDescription, .operation = op };
        const auto key = std::make_pair(asset.GetId(), asset.GetVersion());
        if (const auto found = m_Entries.find(key); found != m_Entries.end()) {
            if (found->second->asset.Get() != asset.Get()) return { .code = RHIErrorCode::InvalidState, .operation = "MaterialGpuCache/identity-version collision" };
            output = found->second; return {};
        }
        auto resource = std::make_shared<MaterialGpuResource>();
        resource->device = &m_Device; resource->asset = asset;
        const RHIBufferDesc desc{ .size = sizeof(*color), .usage = RHIBufferUsage::Uniform, .memory = RHIMemoryDomain::Upload };
        if (const auto status = m_Device.CreateBuffer(desc, resource->parameters); !status.IsSuccess()) return status;
        if (const auto status = m_Device.WriteBuffer(resource->parameters, 0, std::as_bytes(std::span{color,1})); !status.IsSuccess()) return status;
        m_Entries.emplace(key, resource); output = std::move(resource); return {};
    }
    void MaterialGpuCache::Prune() {
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
            if (it->second.use_count() == 1) it = m_Entries.erase(it); else ++it;
    }
}
