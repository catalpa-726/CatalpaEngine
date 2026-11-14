#include "catpch.h"

#include "Renderer/Resources/MaterialGpuCache.h"

#include "Materials/MaterialValidation.h"
#include "RHI/RHIDevice.h"
namespace Catalpa {
	MaterialGpuResource::~MaterialGpuResource() {
		if (device)
			device->ReleaseBuffer(parameters);
	}

	RHIStatus MaterialGpuCache::Acquire(
	    const AssetVersionRef<MaterialInstance>& asset, std::shared_ptr<MaterialGpuResource>& output) {
		constexpr const char* op = "MaterialGpuCache::Acquire";
		std::string error;
		if (!asset.HasValue())
			return {.code = RHIErrorCode::InvalidDescription, .operation = op};
		const auto key = std::make_pair(asset.GetId(), asset.GetVersion());
		if (const auto found = m_Entries.find(key); found != m_Entries.end()) {
			if (found->second->asset.Get() != asset.Get())
				return {.code = RHIErrorCode::InvalidState, .operation = "MaterialGpuCache/identity-version collision"};
			output = found->second;
			return {};
		}
		if (!ValidateMaterialInstance(*asset.Get(), error))
			return {.code = RHIErrorCode::InvalidDescription, .operation = op};
		const auto& value = *asset.Get();
		MaterialSurfaceParameters surface;
		if (!ResolveMaterialSurface(value, surface, error))
			return {.code = RHIErrorCode::InvalidDescription, .operation = op};
		auto resource = std::make_shared<MaterialGpuResource>();
		resource->device = &m_Device;
		resource->asset = asset;
		resource->surface = surface;
		const auto& definition = *value.definition.Get();
		uint32_t flags = 0;
		for (size_t index = 0; index < surface.textures.size(); ++index)
			if (surface.textures[index].HasValue())
				flags |= 1u << index;
		if (definition.alphaMode == MaterialAlphaMode::Mask)
			flags |= 1u << 5;
		if (definition.alphaMode == MaterialAlphaMode::Blend)
			flags |= 1u << 6;
		if (surface.flipV)
			flags |= 1u << 7;
		if (definition.shadingModel == MaterialShadingModel::Unlit)
			flags |= 1u << 8;
		MaterialGpuParameters parameters;
		parameters.baseColor = surface.baseColor;
		parameters.emissiveMetallic = {surface.emissive[0], surface.emissive[1], surface.emissive[2], surface.metallic};
		parameters.roughnessNormalOcclusionCutoff = {
		    surface.roughness, surface.normalScale, surface.occlusionStrength, surface.alphaCutoff};
		parameters.uvScaleOffset = surface.uvScaleOffset;
		parameters.timeEffect = {surface.uvCycles[0], surface.uvCycles[1], static_cast<float>(surface.pulseCycles),
		    static_cast<float>(flags)};
		const RHIBufferDesc desc{
		    .size = sizeof(parameters), .usage = RHIBufferUsage::Uniform, .memory = RHIMemoryDomain::Upload};
		if (const auto status = m_Device.CreateBuffer(desc, resource->parameters); !status.IsSuccess())
			return status;
		if (const auto status = m_Device.WriteBuffer(resource->parameters, 0, std::as_bytes(std::span{&parameters, 1}));
		    !status.IsSuccess())
			return status;
		m_Entries.emplace(key, resource);
		output = std::move(resource);
		return {};
	}

	void MaterialGpuCache::Prune() {
		for (auto it = m_Entries.begin(); it != m_Entries.end();)
			if (it->second.use_count() == 1)
				it = m_Entries.erase(it);
			else
				++it;
	}
}
