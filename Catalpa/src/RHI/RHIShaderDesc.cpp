#include "catpch.h"

#include "RHI/RHIPipelineDesc.h"
namespace Catalpa {
	bool ValidateRHIShader(const RHIShaderDesc& desc, std::string& error) {
		if (!desc.asset.HasValue() || !desc.entryPoint || !*desc.entryPoint) {
			error = "Shader requires a fixed asset version and entry";
			return false;
		}
		if (desc.stage != RHIShaderStage::Vertex && desc.stage != RHIShaderStage::Fragment &&
		    desc.stage != RHIShaderStage::Compute) {
			error = "Unsupported graphics shader stage";
			return false;
		}
		if (!ValidateShaderAsset(*desc.asset.Get(), error))
			return false;
		const auto stage = desc.stage == RHIShaderStage::Vertex ? ShaderStage::Vertex
		    : desc.stage == RHIShaderStage::Fragment            ? ShaderStage::Fragment
		                                                        : ShaderStage::Compute;
		if (!FindShaderEntry(desc.asset.Get()->metadata, desc.entryPoint, stage)) {
			error = "Shader entry/stage is absent from final metadata";
			return false;
		}
		return true;
	}

	bool ValidateRHIPipelineShaderInterface(const RHIGraphicsPipelineDesc& desc, std::string& error) {
		if (!ValidateRHIShader(desc.vertex, error) || !ValidateRHIShader(desc.fragment, error))
			return false;
		if (desc.vertex.stage != RHIShaderStage::Vertex || desc.fragment.stage != RHIShaderStage::Fragment) {
			error = "Graphics stage order is invalid";
			return false;
		}
		const auto& vertex =
		    *FindShaderEntry(desc.vertex.asset.Get()->metadata, desc.vertex.entryPoint, ShaderStage::Vertex);
		const auto& fragment =
		    *FindShaderEntry(desc.fragment.asset.Get()->metadata, desc.fragment.entryPoint, ShaderStage::Fragment);
		/* ID 附件与普通颜色附件的标量类型不同，不能把不匹配留给原生验证层兜底。 */
		if (desc.colorFormat == PixelFormat::Unknown) {
			if (!fragment.outputs.empty()) {
				error = "Depth-only pipeline cannot declare a color output";
				return false;
			}
		} else {
			const auto format = GetPixelFormatInfo(desc.colorFormat);
			if (fragment.outputs.size() != 1 || fragment.outputs.front().location != 0 || format.channels == 0) {
				error = "Color pipeline requires exactly one output at location zero";
				return false;
			}
			const auto& shape = fragment.outputs.front().type;
			if (shape.width != 32 || shape.rows != 1 || shape.arrayCount != 1 || shape.columns != format.channels ||
			    shape.scalar != (format.isInteger ? ShaderScalarKind::UnsignedInteger : ShaderScalarKind::Float)) {
				error = "Fragment output type differs from attachment component type/count";
				return false;
			}
		}
		if (vertex.inputs.size() != desc.vertexLayout.attributes.size()) {
			error = "Vertex input count differs from reflected interface";
			return false;
		}
		for (const auto& input : vertex.inputs) {
			const auto found = std::find_if(desc.vertexLayout.attributes.begin(), desc.vertexLayout.attributes.end(),
			    [&](const auto& attribute) { return attribute.location == input.location; });
			if (found == desc.vertexLayout.attributes.end() || input.type.scalar != ShaderScalarKind::Float ||
			    input.type.width != 32 || input.type.rows != 1 || input.type.arrayCount != 1 ||
			    GetVertexFormatByteSize(found->format) != input.type.columns * 4) {
				error = "Vertex type/location disagrees with reflected interface";
				return false;
			}
		}
		for (const auto& input : fragment.inputs) {
			const auto found = std::find_if(vertex.outputs.begin(), vertex.outputs.end(),
			    [&](const auto& value) { return value.location == input.location; });
			if (found == vertex.outputs.end() || found->type != input.type) {
				error = "Vertex/fragment stage interfaces disagree";
				return false;
			}
		}
		/* 当前RHI只接受set0/单descriptor；不截断数组或忽略storage资源。 */
		struct Requirement {
			RHIBindingType type;
			uint32_t stages = 0, bytes = 0;
			const ShaderBinding* metadata = nullptr;
		};
		std::map<uint32_t, Requirement> requirements;
		const auto collect = [&](const ShaderEntryPointMetadata& entry, uint32_t stage) {
			for (const auto& binding : entry.bindings) {
				RHIBindingType type{};
				switch (binding.kind) {
				case ShaderResourceKind::UniformBuffer:
					type = RHIBindingType::UniformBuffer;
					break;
				case ShaderResourceKind::StorageBuffer:
					type = RHIBindingType::StorageBuffer;
					break;
				case ShaderResourceKind::SampledImage:
					type = RHIBindingType::SampledTexture;
					break;
				case ShaderResourceKind::Sampler:
					type = RHIBindingType::Sampler;
					break;
				default:
					error = "Shader resource kind is unsupported by this RHI";
					return false;
				}
				if (binding.set != 0 || binding.count != 1) {
					error = "Descriptor set/array exceeds current RHI contract";
					return false;
				}
				auto [it, inserted] =
				    requirements.try_emplace(binding.binding, Requirement{type, stage, binding.blockSize, &binding});
				if (!inserted) {
					if (it->second.type != type || it->second.bytes != binding.blockSize ||
					    it->second.metadata->imageDimension != binding.imageDimension ||
					    it->second.metadata->members != binding.members) {
						error = "Cross-stage descriptor ABI conflict";
						return false;
					}
					it->second.stages |= stage;
				}
			}
			return true;
		};
		if (!collect(vertex, 1) || !collect(fragment, 2))
			return false;
		if (requirements.size() != desc.bindingLayout.entries.size()) {
			error = "Descriptor layout count differs from metadata";
			return false;
		}
		std::set<uint32_t> seen;
		const auto bits = [](RHIShaderVisibility visibility) -> uint32_t {
			switch (visibility) {
			case RHIShaderVisibility::None:
				return 0;
			case RHIShaderVisibility::Vertex:
				return 1;
			case RHIShaderVisibility::Fragment:
				return 2;
			case RHIShaderVisibility::VertexFragment:
				return 3;
			default:
				return 4;
			}
		};
		for (const auto& binding : desc.bindingLayout.entries) {
			const auto found = requirements.find(binding.binding);
			if (found != requirements.end() && binding.type == RHIBindingType::SampledTexture) {
				const auto dimension = found->second.metadata->imageDimension;
				const bool matches = (dimension == ShaderImageDimension::Texture2D &&
				                         binding.imageViewType == RHITextureViewType::Texture2D) ||
				    (dimension == ShaderImageDimension::Cube && binding.imageViewType == RHITextureViewType::Cube);
				if (!matches) {
					error = "Texture view dimension differs from reflected shader interface";
					return false;
				}
			}
			if (found == requirements.end() || !seen.insert(binding.binding).second ||
			    binding.type != found->second.type || bits(binding.visibility) != found->second.stages ||
			    (binding.type == RHIBindingType::UniformBuffer && binding.minimumBufferBytes != found->second.bytes)) {
				error = "Descriptor type/visibility/block size differs from metadata";
				return false;
			}
		}
		const uint32_t pushBytes = std::max(vertex.pushConstantBytes, fragment.pushConstantBytes);
		const uint32_t pushStages = (vertex.pushConstantBytes ? 1u : 0u) | (fragment.pushConstantBytes ? 2u : 0u);
		if (desc.pushConstants.size != pushBytes || bits(desc.pushConstants.visibility) != pushStages ||
		    (vertex.pushConstantBytes && fragment.pushConstantBytes &&
		        vertex.pushConstantMembers != fragment.pushConstantMembers)) {
			error = "Push constant ABI differs from metadata";
			return false;
		}
		error.clear();
		return true;
	}
}
