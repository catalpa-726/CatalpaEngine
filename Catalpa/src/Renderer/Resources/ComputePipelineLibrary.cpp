#include "catpch.h"

#include "ComputePipelineLibrary.h"

#include "RHI/RHIDevice.h"
#include "Assets/AssetManager.h"
#include "Shaders/ShaderLibrary.h"
namespace Catalpa {
	void ComputePipelineLibrary::Prune() {
		for (auto it = m_Entries.begin(); it != m_Entries.end();)
			if (it->second.use_count() == 1)
				it = m_Entries.erase(it);
			else
				++it;
	}

	ComputePipelineResource::~ComputePipelineResource() {
		if (device)
			device->ReleaseComputePipeline(pipeline);
	}

	RHIStatus ComputePipelineLibrary::Acquire(std::string_view file, std::shared_ptr<ComputePipelineResource>& output) {
		AssetId id;
		AssetVersionRef<ShaderAsset> shader;
		bool fallback = false;
		std::string error;
		if (!ShaderLibrary::BuiltinId(file, id, error) || !m_Assets.Acquire(id, shader, fallback, error) || fallback)
			return {RHIErrorCode::InvalidState, 0, "Compute/cooked shader"};
		const auto key = std::make_pair(id, shader.GetVersion());
		if (const auto found = m_Entries.find(key); found != m_Entries.end()) {
			if (found->second->shader.Get() != shader.Get())
				return {RHIErrorCode::InvalidState, 0, "Compute/version collision"};
			output = found->second;
			return {};
		}
		const auto* entry = FindShaderEntry(shader.Get()->metadata, "computeMain", ShaderStage::Compute);
		if (!entry)
			return {RHIErrorCode::InvalidDescription, 0, "Compute/entry"};
		auto resource = std::make_shared<ComputePipelineResource>();
		resource->device = &m_Device;
		resource->shader = shader;
		resource->pushBytes = entry->pushConstantBytes;
		for (const auto& source : entry->bindings) {
			if (source.set != 0 || source.count != 1)
				return {RHIErrorCode::Unsupported, 0, "Compute/set or descriptor-array domain"};
			RHIBindingType type;
			switch (source.kind) {
			case ShaderResourceKind::StorageBuffer:
				type = RHIBindingType::StorageBuffer;
				break;
			case ShaderResourceKind::UniformBuffer:
				type = RHIBindingType::UniformBuffer;
				break;
			case ShaderResourceKind::SampledImage:
				type = RHIBindingType::SampledTexture;
				break;
			case ShaderResourceKind::Sampler:
				type = RHIBindingType::Sampler;
				break;
			default:
				return {RHIErrorCode::Unsupported, 0, "Compute/resource kind"};
			}
			resource->bindings.push_back({source.binding, type, RHIShaderVisibility::Compute, source.blockSize});
		}
		const RHIComputePipelineDesc desc{{shader, RHIShaderStage::Compute, "computeMain"},
		    {entry->pushConstantBytes,
		        entry->pushConstantBytes ? RHIShaderVisibility::Compute : RHIShaderVisibility::None},
		    {resource->bindings}};
		if (const auto status = m_Device.CreateComputePipeline(desc, resource->pipeline); !status.IsSuccess())
			return status;
		m_Entries.emplace(key, resource);
		output = std::move(resource);
		return {};
	}
} /* 命名空间 Catalpa */
