#include "catpch.h"

#include "RenderGraphBuilder.h"

#include "RHI/RHIDevice.h"
#include "RHI/RHITextureDesc.h"

namespace Catalpa {

	namespace {

		/**
		 * @brief 单线程分配图身份，耗尽后不回绕复用。
		 *
		 * @return 返回此次签发的非零图标识；标识空间耗尽后返回 0。
		 */
		uint64_t AllocateGraphId() noexcept {
			static uint64_t next = 1;
			const uint64_t result = next;
			if (next != 0)
				next = next == std::numeric_limits<uint64_t>::max() ? 0 : next + 1;
			return result;
		}

		/**
		 * @brief 限定当前颜色附件、上传和 fragment 采样能够处理的布局。
		 *
		 * @param[in] layout 待检查的抽象纹理布局，须能由当前渲染图转换为有效资源状态。
		 *
		 * @return 布局可在当前渲染图中描述并编译时返回 true。
		 */
		bool IsSupportedLayout(RHITextureLayout layout) noexcept {
			return layout == RHITextureLayout::Undefined || layout == RHITextureLayout::ColorAttachment ||
			    layout == RHITextureLayout::DepthAttachment || layout == RHITextureLayout::TransferDestination ||
			    layout == RHITextureLayout::TransferSource || layout == RHITextureLayout::ShaderReadOnly ||
			    layout == RHITextureLayout::Present;
		}

	}

	RenderGraphBuilder::RenderGraphBuilder(RHIDevice& device) : m_Device(device), m_Id(AllocateGraphId()) {}

	/**
	 * @brief 检查图仍处于允许添加声明的阶段。
	 *
	 * @param[in] operation 写入失败状态的操作名称，用来定位当前构图调用。
	 *
	 * @return 返回图是否仍可添加声明的状态；已编译或标识失效时返回错误。
	 */
	RHIStatus RenderGraphBuilder::CheckBuilding(const char* operation) const {
		if (m_Id == 0)
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
		if (m_Compiled || m_Present.HasValue())
			return {.code = RHIErrorCode::InvalidState, .operation = operation};
		return {};
	}

	const RenderGraphBuilder::BufferResource* RenderGraphBuilder::Resolve(RGBuffer resource) const {
		if (!resource.HasValue() || resource.graph != m_Id || resource.slot.generation != 1 ||
		    resource.slot.index >= m_Buffers.size())
			return nullptr;

		return &m_Buffers[resource.slot.index];
	}

	const RenderGraphBuilder::TextureResource* RenderGraphBuilder::Resolve(RGTexture resource) const {
		if (!resource.HasValue() || resource.graph != m_Id || resource.slot.generation != 1 ||
		    resource.slot.index >= m_Textures.size())
			return nullptr;

		return &m_Textures[resource.slot.index];
	}

	RHIStatus RenderGraphBuilder::ImportBuffer(BufferHandle buffer, RGBuffer& resource) {
		constexpr const char* operation = "RenderGraphBuilder::ImportBuffer";
		resource = {};

		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;

		if (!buffer.HasValue() || buffer.device != m_Device.GetDeviceId())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		for (size_t i = 0; i < m_Buffers.size(); ++i) {
			if (m_Buffers[i].handle == buffer) {
				resource = {.graph = m_Id, .slot = {.index = static_cast<uint32_t>(i), .generation = 1}};
				return {};
			}
		}

		if (m_Buffers.size() >= GenerationalHandle<RGBufferTag>::InvalidIndex ||
		    m_Buffers.size() == m_Buffers.max_size())
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};

		RHIBufferStateSnapshot snapshot{};
		if (const RHIStatus status = m_Device.GetBufferState(buffer, snapshot); !status.IsSuccess())
			return status;

		if (snapshot.desc.size == 0 || snapshot.state.revision == 0)
			return {.code = RHIErrorCode::InvalidState, .operation = operation};

		const uint32_t index = static_cast<uint32_t>(m_Buffers.size());
		m_Buffers.push_back({.handle = buffer, .snapshot = std::move(snapshot)});
		resource = {.graph = m_Id, .slot = {.index = index, .generation = 1}};
		return {};
	}

	RHIStatus RenderGraphBuilder::CreateTransientBuffer(const RHIBufferDesc& desc, RGBuffer& resource) try {
		resource = {};
		if (const auto status = CheckBuilding("RenderGraph/CreateTransientBuffer"); !status.IsSuccess())
			return status;
		if (desc.memory != RHIMemoryDomain::Device || desc.size == 0 || desc.size > 64 * 1024 * 1024 ||
		    desc.usage == RHIBufferUsage::None || m_Buffers.size() >= 4096)
			return {RHIErrorCode::InvalidDescription, 0, "RenderGraph/TransientDescriptionOrBudget"};
		const auto index = uint32_t(m_Buffers.size());
		BufferResource buffer;
		buffer.snapshot.desc = desc;
		buffer.transient = true;
		m_Buffers.push_back(std::move(buffer));
		resource = {m_Id, {index, 1}};
		return {};
	} catch (const std::bad_alloc&) {
		return {RHIErrorCode::OutOfMemory, 0, "RenderGraph/TransientDeclaration"};
	}

	RHIStatus RenderGraphBuilder::ImportTexture(TextureViewHandle view, RGTexture& resource) {
		constexpr const char* operation = "RenderGraphBuilder::ImportTexture";
		resource = {};

		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;

		if (!view.HasValue() || view.device != m_Device.GetDeviceId())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		for (size_t i = 0; i < m_Textures.size(); ++i) {
			if (m_Textures[i].handle == view) {
				resource = {.graph = m_Id, .slot = {.index = static_cast<uint32_t>(i), .generation = 1}};
				return {};
			}
		}

		/* 允许同时导入普通纹理和交换链图像，限制图内句柄索引及容器容量。 */
		if (m_Textures.size() >= GenerationalHandle<RGTextureTag>::InvalidIndex ||
		    m_Textures.size() == m_Textures.max_size())
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};

		RHITextureStateSnapshot snapshot{};
		if (const RHIStatus status = m_Device.GetTextureState(view, snapshot); !status.IsSuccess())
			return status;

		if (snapshot.extent.IsEmpty() || snapshot.state.revision == 0)
			return {.code = RHIErrorCode::InvalidState, .operation = operation};

		if (snapshot.sampleCount != 1 || snapshot.format == PixelFormat::Unknown ||
		    !IsSupportedLayout(snapshot.state.access.layout))
			return {.code = RHIErrorCode::Unsupported, .operation = operation};

		const uint32_t index = static_cast<uint32_t>(m_Textures.size());
		m_Textures.push_back({.handle = view, .snapshot = std::move(snapshot)});
		resource = {.graph = m_Id, .slot = {.index = index, .generation = 1}};
		return {};
	}

	RHIStatus RenderGraphBuilder::AddCopyPass(
	    const char* name, RGBuffer source, RGBuffer destination, const RHIBufferCopy& copy) {
		constexpr const char* operation = "RenderGraphBuilder::AddCopyPass";
		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;

		const auto* sourceResource = Resolve(source);
		const auto* destinationResource = Resolve(destination);

		if (!name || !*name || !sourceResource || !destinationResource || copy.size == 0)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		const auto& sourceDesc = sourceResource->snapshot.desc;
		const auto& destinationDesc = destinationResource->snapshot.desc;

		if (!HasBufferUsage(sourceDesc.usage, RHIBufferUsage::TransferSource) ||
		    !HasBufferUsage(destinationDesc.usage, RHIBufferUsage::TransferDestination) ||
		    !IsValidBufferRange(sourceDesc.size, copy.sourceOffset, copy.size) ||
		    !IsValidBufferRange(destinationDesc.size, copy.destinationOffset, copy.size))
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		if (source == destination && copy.sourceOffset < copy.destinationOffset + copy.size &&
		    copy.destinationOffset < copy.sourceOffset + copy.size)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};

		m_Passes.push_back({.name = name, .source = source, .destination = destination, .copy = copy});

		return {};
	}

	RHIStatus RenderGraphBuilder::AddTextureUploadPass(
	    const char* name, RGBuffer source, RGTexture destination, const RHIBufferTextureCopy& copy) {
		constexpr const char* operation = "RenderGraphBuilder::AddTextureUploadPass";
		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;

		const auto* sourceResource = Resolve(source);
		const auto* destinationResource = Resolve(destination);

		if (!name || !*name || !sourceResource || !destinationResource)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		const auto& sourceDesc = sourceResource->snapshot.desc;
		const auto& destinationSnapshot = destinationResource->snapshot;

		if (!HasBufferUsage(sourceDesc.usage, RHIBufferUsage::TransferSource) ||
		    !HasTextureUsage(destinationSnapshot.usage, RHITextureUsage::TransferDestination) ||
		    destinationSnapshot.presentable || copy.bufferOffset % 4 != 0)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		if (copy.range.aspect != RHITextureAspect::Color || copy.range.levelCount != 1 || copy.range.layerCount != 1 ||
		    copy.range.baseMipLevel >= destinationSnapshot.mipLevels ||
		    copy.range.baseArrayLayer >= destinationSnapshot.arrayLayers)
			return {.code = RHIErrorCode::Unsupported, .operation = operation};

		uint64_t bytes = 0;

		const Extent2D mipExtent{std::max(1u, destinationSnapshot.extent.width >> copy.range.baseMipLevel),
		    std::max(1u, destinationSnapshot.extent.height >> copy.range.baseMipLevel)};
		if (!TryGetTextureUploadByteSize(mipExtent, destinationSnapshot.format, bytes))
			return {.code = RHIErrorCode::Unsupported, .operation = operation};

		if (!IsValidBufferRange(sourceDesc.size, copy.bufferOffset, bytes))
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};

		m_Passes.push_back({.name = name,
		    .kind = PassKind::TextureUpload,
		    .source = source,
		    .destinationTexture = destination,
		    .textureCopy = copy,
		    .textureBytes = bytes});

		return {};
	}

	RHIStatus RenderGraphBuilder::AddDrawPass(const char* name, const RGIndexedDrawDesc& desc) {
		constexpr const char* operation = "RenderGraphBuilder::AddDrawPass";
		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		if (desc.pushConstants.size() % 4 != 0)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (desc.pushConstants.size() > m_Device.GetCapabilities().maxPushConstantBytes)
			return {.code = RHIErrorCode::Unsupported, .operation = operation};

		/* 解析本图中的几何与附件句柄；顶点数为零时按索引绘制路径校验必需资源。 */
		const auto* vertices = Resolve(desc.vertices);
		const auto* indices = Resolve(desc.indices);
		const auto* color = Resolve(desc.color);
		const auto* depth = Resolve(desc.depth);
		const bool indexed = desc.vertexCount == 0;
		if (!name || !*name || (indexed && (!vertices || !indices)) ||
		    (!indexed && ((desc.vertices.HasValue() && !vertices) || desc.indices.HasValue())) || (!color && !depth) ||
		    (!color && desc.color != RGTexture{}) || (!depth && desc.depth != RGTexture{}) ||
		    !desc.pipeline.HasValue() || desc.pipeline.device != m_Device.GetDeviceId())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		/* 核对附件加载、存储及深度读写方式，检查视口数值与裁剪矩形的附件边界。 */
		const auto validLoad = [](RHILoadOp value) {
			return value == RHILoadOp::Load || value == RHILoadOp::Clear || value == RHILoadOp::DontCare;
		};
		const auto validStore = [](RHIStoreOp value) {
			return value == RHIStoreOp::Store || value == RHIStoreOp::DontCare;
		};
		if (color) {
			if (color->snapshot.mipLevels != 1 || color->snapshot.arrayLayers != 1 ||
			    GetPixelFormatInfo(color->snapshot.format).isDepth ||
			    !HasTextureUsage(color->snapshot.usage, RHITextureUsage::ColorAttachment) || !validLoad(desc.loadOp) ||
			    !validStore(desc.storeOp))
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			if (desc.loadOp == RHILoadOp::Clear)
				for (const float value : desc.clearColor)
					if (!std::isfinite(value))
						return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		}
		if (depth) {
			if (depth->snapshot.mipLevels != 1 || depth->snapshot.arrayLayers != 1 ||
			    !GetPixelFormatInfo(depth->snapshot.format).isDepth || depth->snapshot.presentable ||
			    !HasTextureUsage(depth->snapshot.usage, RHITextureUsage::DepthAttachment) ||
			    !validLoad(desc.depthLoadOp) || !validStore(desc.depthStoreOp) || !std::isfinite(desc.clearDepth) ||
			    desc.clearDepth < 0.0f || desc.clearDepth > 1.0f ||
			    (desc.depthReadOnly &&
			        (desc.depthLoadOp != RHILoadOp::Load || desc.depthStoreOp != RHIStoreOp::Store)) ||
			    (color && (desc.depth == desc.color || color->snapshot.extent != depth->snapshot.extent)))
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		}
		const auto extent = color ? color->snapshot.extent : depth->snapshot.extent;
		if (extent.width > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
		    extent.height > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
		    !std::isfinite(desc.viewport.x) || !std::isfinite(desc.viewport.y) || !std::isfinite(desc.viewport.width) ||
		    !std::isfinite(desc.viewport.height) || !std::isfinite(desc.viewport.minDepth) ||
		    !std::isfinite(desc.viewport.maxDepth) || desc.viewport.width <= 0.0f || desc.viewport.height <= 0.0f ||
		    desc.viewport.minDepth < 0.0f || desc.viewport.maxDepth > 1.0f ||
		    desc.viewport.minDepth > desc.viewport.maxDepth)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (!desc.scissor.IsEmpty()) {
			if (desc.scissor.offset.x < 0 || desc.scissor.offset.y < 0 ||
			    static_cast<uint32_t>(desc.scissor.offset.x) > extent.width ||
			    desc.scissor.extent.width > extent.width - static_cast<uint32_t>(desc.scissor.offset.x) ||
			    static_cast<uint32_t>(desc.scissor.offset.y) > extent.height ||
			    desc.scissor.extent.height > extent.height - static_cast<uint32_t>(desc.scissor.offset.y))
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		}

		/* 检查采样纹理和统一缓冲区的绑定槽、用途及范围，避免多个输入占用同一位置。 */
		if (desc.sampledTextures.size() > RHIMaxBindingsPerSet / 2 ||
		    desc.uniformBuffers.size() > RHIMaxBindingsPerSet - desc.sampledTextures.size() * 2)
			return {.code = RHIErrorCode::Unsupported, .operation = operation};
		for (size_t i = 0; i < desc.sampledTextures.size(); ++i) {
			const auto& binding = desc.sampledTextures[i];
			const auto* sampled = Resolve(binding.texture);
			if (!sampled || binding.texture == desc.color || binding.texture == desc.depth ||
			    !HasTextureUsage(sampled->snapshot.usage, RHITextureUsage::Sampled) || !binding.sampler.HasValue() ||
			    binding.sampler.device != m_Device.GetDeviceId() || binding.textureBinding == binding.samplerBinding)
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			for (size_t j = 0; j < i; ++j) {
				const auto& previous = desc.sampledTextures[j];
				if (binding.textureBinding == previous.textureBinding ||
				    binding.textureBinding == previous.samplerBinding ||
				    binding.samplerBinding == previous.textureBinding ||
				    (binding.samplerBinding == previous.samplerBinding && binding.sampler != previous.sampler))
					return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			}
		}
		const auto& caps = m_Device.GetCapabilities();
		for (size_t i = 0; i < desc.uniformBuffers.size(); ++i) {
			const auto& binding = desc.uniformBuffers[i];
			const auto* uniform = Resolve(binding.buffer);
			if (!uniform || binding.buffer == desc.vertices || binding.buffer == desc.indices ||
			    !HasBufferUsage(uniform->snapshot.desc.usage, RHIBufferUsage::Uniform) || binding.size == 0 ||
			    !IsValidBufferRange(uniform->snapshot.desc.size, binding.offset, binding.size) ||
			    (binding.visibility != RHIShaderVisibility::Vertex &&
			        binding.visibility != RHIShaderVisibility::Fragment &&
			        binding.visibility != RHIShaderVisibility::VertexFragment))
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			if (caps.minUniformBufferOffsetAlignment == 0 ||
			    binding.offset % caps.minUniformBufferOffsetAlignment != 0 || binding.size > caps.maxUniformBufferRange)
				return {.code = RHIErrorCode::Unsupported, .operation = operation};
			for (size_t j = 0; j < i; ++j)
				if (binding.binding == desc.uniformBuffers[j].binding)
					return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			for (const auto& sampled : desc.sampledTextures)
				if (binding.binding == sampled.textureBinding || binding.binding == sampled.samplerBinding)
					return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		}

		/* 核实索引、顶点和间接绘制所读取的字节范围；所有资源均通过检查后才登记绘制步骤。 */
		if (indexed) {
			uint32_t indexSize = 0;
			switch (desc.indexType) {
			case RHIIndexType::UInt16:
				indexSize = 2;
				break;
			case RHIIndexType::UInt32:
				indexSize = 4;
				break;
			default:
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			}
			const auto& vertexDesc = vertices->snapshot.desc;
			const auto& indexDesc = indices->snapshot.desc;
			if (!HasBufferUsage(vertexDesc.usage, RHIBufferUsage::Vertex) ||
			    !HasBufferUsage(indexDesc.usage, RHIBufferUsage::Index) || desc.vertexOffset >= vertexDesc.size ||
			    desc.vertexOffset % 4 != 0 || desc.indexOffset >= indexDesc.size || desc.indexOffset % indexSize != 0)
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
			const uint64_t firstByte = static_cast<uint64_t>(desc.firstIndex) * indexSize;
			const uint64_t byteCount = static_cast<uint64_t>(desc.indexCount) * indexSize;
			if (!IsValidBufferRange(indexDesc.size - desc.indexOffset, firstByte, byteCount))
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		} else if (vertices &&
		    (!HasBufferUsage(vertices->snapshot.desc.usage, RHIBufferUsage::Vertex) ||
		        desc.vertexOffset >= vertices->snapshot.desc.size || desc.vertexOffset % 4 != 0))
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
		if (desc.indirect.HasValue()) {
			const auto* indirect = Resolve(desc.indirect);
			if (!indexed || !indirect || !HasBufferUsage(indirect->snapshot.desc.usage, RHIBufferUsage::Indirect) ||
			    desc.indirectOffset % 4 || !IsValidBufferRange(indirect->snapshot.desc.size, desc.indirectOffset, 20))
				return {RHIErrorCode::InvalidDescription, 0, operation};
		}
		for (const auto& binding : desc.storageBuffers) {
			const auto* buffer = Resolve(binding.buffer);
			if (!buffer || !HasBufferUsage(buffer->snapshot.desc.usage, RHIBufferUsage::Storage) ||
			    binding.mode != RGAccessMode::Read || binding.initializesRange || !binding.size ||
			    !IsValidBufferRange(buffer->snapshot.desc.size, binding.offset, binding.size))
				return {RHIErrorCode::InvalidDescription, 0, operation};
		}
		m_Passes.push_back({.name = name, .kind = PassKind::Draw, .draw = desc});
		return {};
	}

	RHIStatus RenderGraphBuilder::AddClearPass(const char* name, RGTexture color, std::array<float, 4> clearColor) {
		constexpr const char* operation = "RenderGraphBuilder::AddClearPass";
		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		const auto* target = Resolve(color);
		if (!name || !*name || !target || target->snapshot.mipLevels != 1 || target->snapshot.arrayLayers != 1 ||
		    GetPixelFormatInfo(target->snapshot.format).isDepth ||
		    !HasTextureUsage(target->snapshot.usage, RHITextureUsage::ColorAttachment))
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		for (const float value : clearColor)
			if (!std::isfinite(value))
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (m_Passes.size() >= GenerationalHandle<RGPassTag>::InvalidIndex || m_Passes.size() == m_Passes.max_size())
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
		m_Passes.push_back({.name = name, .kind = PassKind::Clear, .clearTexture = color, .clearColor = clearColor});
		return {};
	}

	RHIStatus RenderGraphBuilder::AddDepthClearPass(const char* name, RGTexture depth, float clearDepth) {
		constexpr const char* operation = "RenderGraphBuilder::AddDepthClearPass";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		const auto* target = Resolve(depth);
		if (!name || !*name || !target || !GetPixelFormatInfo(target->snapshot.format).isDepth ||
		    !HasTextureUsage(target->snapshot.usage, RHITextureUsage::DepthAttachment) || !std::isfinite(clearDepth) ||
		    clearDepth < 0.0f || clearDepth > 1.0f || target->snapshot.mipLevels != 1 ||
		    target->snapshot.arrayLayers != 1)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		m_Passes.push_back(
		    {.name = name, .kind = PassKind::Clear, .draw = {.clearDepth = clearDepth}, .clearTexture = depth});
		return {};
	}

	RHIStatus RenderGraphBuilder::AddComputePass(const char* name, const RGComputeDesc& desc) {
		constexpr const char* operation = "RenderGraph/AddComputePass";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		if (!name || !*name || !desc.pipeline.HasValue() || desc.pipeline.device != m_Device.GetDeviceId() ||
		    !m_Device.GetCapabilities().compute || desc.groups[0] == 0 || desc.groups[1] == 0 || desc.groups[2] == 0)
			return {RHIErrorCode::InvalidDescription, 0, operation};
		for (size_t axis = 0; axis < 3; ++axis)
			if (desc.groups[axis] > m_Device.GetCapabilities().maxComputeWorkGroupCount[axis])
				return {RHIErrorCode::Unsupported, 0, operation};
		for (const auto& binding : desc.storageBuffers) {
			const auto* buffer = Resolve(binding.buffer);
			if (!buffer || !HasBufferUsage(buffer->snapshot.desc.usage, RHIBufferUsage::Storage) || binding.size == 0 ||
			    !IsValidBufferRange(buffer->snapshot.desc.size, binding.offset, binding.size) ||
			    (binding.initializesRange && binding.mode != RGAccessMode::Write))
				return {RHIErrorCode::InvalidDescription, 0, operation};
		}
		for (const auto& binding : desc.uniformBuffers) {
			const auto* buffer = Resolve(binding.buffer);
			if (!buffer || !HasBufferUsage(buffer->snapshot.desc.usage, RHIBufferUsage::Uniform) || !binding.size ||
			    !IsValidBufferRange(buffer->snapshot.desc.size, binding.offset, binding.size))
				return {RHIErrorCode::InvalidDescription, 0, operation};
		}
		for (const auto& binding : desc.sampledTextures) {
			const auto* texture = Resolve(binding.texture);
			if (!texture || !binding.sampler.HasValue() ||
			    !HasTextureUsage(texture->snapshot.usage, RHITextureUsage::Sampled))
				return {RHIErrorCode::InvalidDescription, 0, operation};
		}
		m_Passes.push_back({.name = name, .kind = PassKind::Compute, .compute = desc});
		return {};
	}

	RHIStatus RenderGraphBuilder::ExportPresent(RGTexture resource) {
		constexpr const char* operation = "RenderGraphBuilder::ExportPresent";
		if (const RHIStatus status = CheckBuilding(operation); !status.IsSuccess())
			return status;

		const auto* texture = Resolve(resource);

		if (!texture || !texture->snapshot.presentable)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		m_Present = resource;
		return {};
	}

	RHIStatus RenderGraphBuilder::ExportTexture(RGTexture resource) {
		constexpr const char* operation = "RenderGraphBuilder::ExportTexture";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		const auto* texture = Resolve(resource);
		if (!texture || texture->snapshot.presentable ||
		    !HasTextureUsage(texture->snapshot.usage, RHITextureUsage::Sampled) ||
		    std::find(m_ReadbackExports.begin(), m_ReadbackExports.end(), resource) != m_ReadbackExports.end())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (std::find(m_Exports.begin(), m_Exports.end(), resource) == m_Exports.end())
			m_Exports.push_back(resource);
		return {};
	}

	RHIStatus RenderGraphBuilder::ExportReadbackTexture(RGTexture resource) {
		constexpr const char* operation = "RenderGraphBuilder::ExportReadbackTexture";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		const auto* texture = Resolve(resource);
		if (!texture || texture->snapshot.presentable ||
		    !HasTextureUsage(texture->snapshot.usage, RHITextureUsage::TransferSource) ||
		    std::find(m_Exports.begin(), m_Exports.end(), resource) != m_Exports.end())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		if (std::find(m_ReadbackExports.begin(), m_ReadbackExports.end(), resource) == m_ReadbackExports.end())
			m_ReadbackExports.push_back(resource);
		return {};
	}

	RHIStatus RenderGraphBuilder::SetTimestampQueries(std::shared_ptr<RHIQueryPool> pool) {
		constexpr const char* operation = "RenderGraphBuilder::SetTimestampQueries";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		if (!pool || pool->GetDeviceId() != m_Device.GetDeviceId() || pool->GetCount() < 2)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		m_Queries = std::move(pool);
		return {};
	}

	RHIStatus RenderGraphBuilder::AddTextureReadbackPass(
	    const char* name, RGTexture source, RGBuffer destination, const RHITextureBufferCopy& copy) {
		constexpr const char* operation = "RenderGraphBuilder::AddTextureReadbackPass";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		const auto* texture = Resolve(source);
		const auto* buffer = Resolve(destination);
		if (!name || !*name || !texture || !buffer || buffer->snapshot.desc.memory != RHIMemoryDomain::Readback ||
		    !HasTextureUsage(texture->snapshot.usage, RHITextureUsage::TransferSource) ||
		    !HasBufferUsage(buffer->snapshot.desc.usage, RHIBufferUsage::TransferDestination) ||
		    copy.range.aspect != RHITextureAspect::Color || copy.range.levelCount != 1 || copy.range.layerCount != 1 ||
		    copy.range.baseMipLevel >= texture->snapshot.mipLevels ||
		    copy.range.baseArrayLayer >= texture->snapshot.arrayLayers || copy.textureOffset.x < 0 ||
		    copy.textureOffset.y < 0 || copy.bufferOffset % 4 != 0)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		const Extent2D extent{std::max(1u, texture->snapshot.extent.width >> copy.range.baseMipLevel),
		    std::max(1u, texture->snapshot.extent.height >> copy.range.baseMipLevel)};
		uint64_t bytes = 0;
		if (!TryGetTextureUploadByteSize(copy.extent, texture->snapshot.format, bytes) ||
		    uint32_t(copy.textureOffset.x) > extent.width ||
		    copy.extent.width > extent.width - uint32_t(copy.textureOffset.x) ||
		    uint32_t(copy.textureOffset.y) > extent.height ||
		    copy.extent.height > extent.height - uint32_t(copy.textureOffset.y) ||
		    !IsValidBufferRange(buffer->snapshot.desc.size, copy.bufferOffset, bytes))
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		m_Passes.push_back({.name = name,
		    .kind = PassKind::TextureReadback,
		    .destination = destination,
		    .textureBytes = bytes,
		    .sourceTexture = source,
		    .readbackCopy = copy});
		return {};
	}

	RGPass RenderGraphBuilder::GetLastPass() const noexcept {
		if (m_Passes.empty())
			return {};
		return {.graph = m_Id, .slot = {.index = static_cast<uint32_t>(m_Passes.size() - 1), .generation = 1}};
	}

	RHIStatus RenderGraphBuilder::AddDependency(RGPass before, RGPass after) try {
		constexpr const char* operation = "RenderGraphBuilder::AddDependency";
		if (const auto status = CheckBuilding(operation); !status.IsSuccess())
			return status;
		const auto valid = [&](RGPass pass) {
			return pass.HasValue() && pass.graph == m_Id && pass.slot.generation == 1 &&
			    pass.slot.index < m_Passes.size();
		};
		if (!valid(before) || !valid(after) || before == after)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		const RGDependency edge{.before = before.slot.index, .after = after.slot.index};
		if (std::find(m_Dependencies.begin(), m_Dependencies.end(), edge) == m_Dependencies.end())
			m_Dependencies.push_back(edge);
		return {};
	} catch (const std::bad_alloc&) {
		return {.code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphBuilder::DependencyAllocation"};
	} catch (const std::length_error&) {
		return {.code = RHIErrorCode::OutOfMemory, .operation = "RenderGraphBuilder::DependencyCapacity"};
	}

	/**
	 * @brief 从任务录制参数提取编译所需的资源声明及逐任务访问范围。
	 * 从录制参数生成同源范围访问，纹理目前只描述整个 mip0/layer0。
	 *
	 * @param[out] resources 接收缓冲区和纹理声明。
	 * @param[out] passes 接收对应任务及读写关系。
	 */
	void RenderGraphBuilder::Describe(
	    std::vector<RGResourceDeclaration>& resources, std::vector<RGPassDeclaration>& passes) const {
		resources.clear();
		passes.clear();
		resources.reserve(m_Buffers.size() + m_Textures.size());
		passes.reserve(m_Passes.size());
		for (const auto& buffer : m_Buffers)
			resources.push_back({.kind = RGResourceKind::Buffer,
			    .size = buffer.snapshot.desc.size,
			    .initialized = buffer.snapshot.state.initialized});
		for (const auto& texture : m_Textures) {
			RGResourceDeclaration resource{.kind = RGResourceKind::Texture,
			    .size = uint64_t(texture.snapshot.mipLevels) * texture.snapshot.arrayLayers};
			if (texture.snapshot.state.initialized)
				resource.initialized.push_back({.offset = 0, .size = resource.size});
			else
				resource.initialized = texture.snapshot.state.initializedSubresources;
			resources.push_back(std::move(resource));
		}
		const auto textureIndex = [&](RGTexture texture) {
			return static_cast<uint32_t>(m_Buffers.size()) + texture.slot.index;
		};
		for (const auto& pass : m_Passes) {
			RGPassDeclaration declaration;
			/* UploadManager 在整批提交后统一认领已上传资源，两类复制均为不可剔除副作用。 */
			declaration.sideEffect = pass.kind == PassKind::Copy || pass.kind == PassKind::TextureUpload ||
			    pass.kind == PassKind::TextureReadback || (pass.kind == PassKind::Compute && pass.compute.sideEffect);
			const auto readBuffer = [&](RGBuffer buffer, uint64_t offset, uint64_t size) {
				if (size != 0)
					declaration.accesses.push_back({.resource = buffer.slot.index, .offset = offset, .size = size});
			};
			const auto attachment = [&](RGTexture texture, RHILoadOp load, RHIStoreOp store, bool readOnly) {
				if (!texture.HasValue())
					return;
				declaration.accesses.push_back({.resource = textureIndex(texture),
				    .size = uint64_t(m_Textures[texture.slot.index].snapshot.mipLevels) *
				        m_Textures[texture.slot.index].snapshot.arrayLayers,
				    .mode = readOnly              ? RGAccessMode::Read
				        : load == RHILoadOp::Load ? RGAccessMode::ReadWrite
				                                  : RGAccessMode::Write,
				    .initialization = readOnly                                         ? RGWriteInitialization::Preserve
				        : store == RHIStoreOp::DontCare || load == RHILoadOp::DontCare ? RGWriteInitialization::Discard
				        : load == RHILoadOp::Clear ? RGWriteInitialization::Initialize
				                                   : RGWriteInitialization::Preserve});
			};
			switch (pass.kind) {
			case PassKind::Compute:
				for (const auto& binding : pass.compute.storageBuffers)
					declaration.accesses.push_back({.resource = binding.buffer.slot.index,
					    .offset = binding.offset,
					    .size = binding.size,
					    .mode = binding.mode,
					    .initialization = binding.initializesRange ? RGWriteInitialization::Initialize
					                                               : RGWriteInitialization::Preserve});
				for (const auto& binding : pass.compute.uniformBuffers)
					readBuffer(binding.buffer, binding.offset, binding.size);
				for (const auto& binding : pass.compute.sampledTextures)
					declaration.accesses.push_back({.resource = textureIndex(binding.texture),
					    .size = uint64_t(m_Textures[binding.texture.slot.index].snapshot.mipLevels) *
					        m_Textures[binding.texture.slot.index].snapshot.arrayLayers});
				break;
			case PassKind::Copy:
				readBuffer(pass.source, pass.copy.sourceOffset, pass.copy.size);
				declaration.accesses.push_back({.resource = pass.destination.slot.index,
				    .offset = pass.copy.destinationOffset,
				    .size = pass.copy.size,
				    .mode = RGAccessMode::Write,
				    .initialization = RGWriteInitialization::Initialize});
				break;
			case PassKind::TextureUpload:
				readBuffer(pass.source, pass.textureCopy.bufferOffset, pass.textureBytes);
				declaration.accesses.push_back({.resource = textureIndex(pass.destinationTexture),
				    .offset = uint64_t(pass.textureCopy.range.baseArrayLayer) *
				            m_Textures[pass.destinationTexture.slot.index].snapshot.mipLevels +
				        pass.textureCopy.range.baseMipLevel,
				    .size = 1,
				    .mode = RGAccessMode::Write,
				    .initialization = RGWriteInitialization::Initialize});
				break;
			case PassKind::TextureReadback:
				declaration.accesses.push_back({.resource = textureIndex(pass.sourceTexture),
				    .offset = uint64_t(pass.readbackCopy.range.baseArrayLayer) *
				            m_Textures[pass.sourceTexture.slot.index].snapshot.mipLevels +
				        pass.readbackCopy.range.baseMipLevel,
				    .size = 1});
				declaration.accesses.push_back({.resource = pass.destination.slot.index,
				    .offset = pass.readbackCopy.bufferOffset,
				    .size = pass.textureBytes,
				    .mode = RGAccessMode::Write,
				    .initialization = RGWriteInitialization::Initialize});
				break;
			case PassKind::Clear:
				attachment(pass.clearTexture, RHILoadOp::Clear, RHIStoreOp::Store, false);
				break;
			case PassKind::Draw: {
				const auto& draw = pass.draw;
				if (draw.vertexCount != 0 && draw.instanceCount != 0 && draw.vertices.HasValue())
					readBuffer(draw.vertices, draw.vertexOffset,
					    m_Buffers[draw.vertices.slot.index].snapshot.desc.size - draw.vertexOffset);
				if (draw.vertexCount == 0 && draw.indexCount != 0 && draw.instanceCount != 0) {
					readBuffer(draw.vertices, draw.vertexOffset,
					    m_Buffers[draw.vertices.slot.index].snapshot.desc.size - draw.vertexOffset);
					const uint64_t indexSize = draw.indexType == RHIIndexType::UInt16 ? 2u : 4u;
					readBuffer(draw.indices, draw.indexOffset + static_cast<uint64_t>(draw.firstIndex) * indexSize,
					    static_cast<uint64_t>(draw.indexCount) * indexSize);
				}
				for (const auto& uniform : draw.uniformBuffers)
					readBuffer(uniform.buffer, uniform.offset, uniform.size);
				if (draw.indirect.HasValue())
					readBuffer(draw.indirect, draw.indirectOffset, 20);
				for (const auto& binding : draw.storageBuffers)
					readBuffer(binding.buffer, binding.offset, binding.size);
				for (const auto& sampled : draw.sampledTextures)
					declaration.accesses.push_back({.resource = textureIndex(sampled.texture),
					    .size = uint64_t(m_Textures[sampled.texture.slot.index].snapshot.mipLevels) *
					        m_Textures[sampled.texture.slot.index].snapshot.arrayLayers});
				attachment(draw.color, draw.loadOp, draw.storeOp, false);
				attachment(draw.depth, draw.depthLoadOp, draw.depthStoreOp, draw.depthReadOnly);
				break;
			}
			}
			passes.push_back(std::move(declaration));
		}
	}

}
