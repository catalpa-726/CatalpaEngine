#include "catpch.h"

#include "VulkanBinding.h"

#include "VulkanConversions.h"

namespace Catalpa {
	RHIStatus BuildVulkanBindingLayout(std::span<const RHIBindingLayoutEntry> entries,
	    const VkPhysicalDeviceLimits& limits, uint32_t colorAttachmentCount,
	    std::span<VkDescriptorSetLayoutBinding> output) noexcept {
		constexpr const char* operation = "BuildVulkanBindingLayout";
		if (entries.size() > RHIMaxBindingsPerSet || output.size() < entries.size())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		struct Counts {
			uint32_t images = 0, samplers = 0, uniforms = 0, storage = 0;
		};
		Counts vertex, fragment, compute, total;
		for (size_t i = 0; i < entries.size(); ++i) {
			const auto& entry = entries[i];
			const auto stages = ToVulkanShaderStages(entry.visibility);
			if (stages == 0)
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			for (size_t j = 0; j < i; ++j)
				if (entries[j].binding == entry.binding)
					return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
			switch (entry.type) {
			case RHIBindingType::SampledTexture:
				type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				break;
			case RHIBindingType::Sampler:
				type = VK_DESCRIPTOR_TYPE_SAMPLER;
				break;
			case RHIBindingType::UniformBuffer:
				type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				break;
			case RHIBindingType::StorageBuffer:
				type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				break;
			default:
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			}

			const auto count = [&](Counts& values) {
				if (entry.type == RHIBindingType::StorageBuffer)
					++values.storage;
				else if (entry.type == RHIBindingType::UniformBuffer)
					++values.uniforms;
				else if (entry.type == RHIBindingType::Sampler)
					++values.samplers;
				else
					++values.images;
			};
			count(total);
			if (stages & VK_SHADER_STAGE_COMPUTE_BIT)
				count(compute);
			if (stages & VK_SHADER_STAGE_VERTEX_BIT)
				count(vertex);
			if (stages & VK_SHADER_STAGE_FRAGMENT_BIT)
				count(fragment);
			output[i] = {.binding = entry.binding, .descriptorType = type, .descriptorCount = 1, .stageFlags = stages};
		}
		const auto exceeds = [&](const Counts& values, uint32_t attachments) {
			return values.images > limits.maxPerStageDescriptorSampledImages ||
			    values.samplers > limits.maxPerStageDescriptorSamplers ||
			    values.uniforms > limits.maxPerStageDescriptorUniformBuffers ||
			    values.storage > limits.maxPerStageDescriptorStorageBuffers ||
			    static_cast<uint64_t>(values.images) + values.uniforms + values.storage + attachments >
			    limits.maxPerStageResources;
		};
		if ((!entries.empty() && limits.maxBoundDescriptorSets < 1) ||
		    total.images > limits.maxDescriptorSetSampledImages || total.samplers > limits.maxDescriptorSetSamplers ||
		    total.uniforms > limits.maxDescriptorSetUniformBuffers ||
		    total.storage > limits.maxDescriptorSetStorageBuffers || exceeds(vertex, 0) ||
		    exceeds(fragment, colorAttachmentCount) || exceeds(compute, 0))
			return {.code = RHIErrorCode::Unsupported, .operation = operation};
		return {};
	}

	RHIStatus ValidateVulkanUniformBinding(const RHIBinding& binding, const RHIBufferStateSnapshot& snapshot,
	    const RHICapabilities& capabilities) noexcept {
		constexpr const char* operation = "ValidateVulkanUniformBinding";
		if (binding.type != RHIBindingType::UniformBuffer || !binding.buffer.HasValue() || binding.texture.HasValue() ||
		    binding.sampler.HasValue() || binding.size == 0 ||
		    !HasBufferUsage(snapshot.desc.usage, RHIBufferUsage::Uniform) ||
		    capabilities.minUniformBufferOffsetAlignment == 0 ||
		    binding.offset % capabilities.minUniformBufferOffsetAlignment != 0 ||
		    !IsValidBufferRange(snapshot.desc.size, binding.offset, binding.size) ||
		    !RangeContains(snapshot.state.initialized, binding.offset, binding.size))
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (binding.size > capabilities.maxUniformBufferRange)
			return {.code = RHIErrorCode::Unsupported, .operation = operation};
		return {};
	}
}
