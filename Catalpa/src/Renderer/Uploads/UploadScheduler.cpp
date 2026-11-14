#include "catpch.h"
#include "UploadScheduler.h"

#include "Log/Log.h"
#include "RHI/RHIDevice.h"
#include "Renderer/RenderGraph/RenderGraph.h"

namespace Catalpa {

	UploadScheduler::~UploadScheduler() {
		Shutdown();
	}

	RHIStatus UploadScheduler::CreateBuffer(
	    std::span<const std::byte> data, RHIBufferUsage usage, BufferHandle& buffer) {
		constexpr const char* operation = "UploadScheduler::CreateBuffer";
		buffer = {};

		/* 几何缓冲可同时供计算着色器读取；复制用途仍由上传器统一补充。 */
		constexpr uint32_t geometryUsage = static_cast<uint32_t>(RHIBufferUsage::Vertex | RHIBufferUsage::Index);
		constexpr uint32_t allowedUsage = geometryUsage | static_cast<uint32_t>(RHIBufferUsage::Storage);
		const uint32_t requestedUsage = static_cast<uint32_t>(usage);
		if (data.empty() || (requestedUsage & geometryUsage) == 0 || (requestedUsage & ~allowedUsage) != 0)
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};

		BufferHandle destination{}, staging{};
		const auto discard = [&] {
			m_Device.ReleaseBuffer(std::exchange(staging, BufferHandle{}));
			m_Device.ReleaseBuffer(std::exchange(destination, BufferHandle{}));
		};
		try {
			if (m_Pending.size() == m_Pending.max_size())
				return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
			/* 所有容器准备先于原生创建；候选句柄跨try保留，以收束后续创建/写入的分配异常。 */
			m_Pending.reserve(m_Pending.size() + 1);
			const uint64_t size = static_cast<uint64_t>(data.size());
			const RHIBufferDesc destinationDesc{
			    .size = size, .usage = usage | RHIBufferUsage::TransferDestination, .memory = RHIMemoryDomain::Device};
			const RHIBufferDesc stagingDesc{
			    .size = size, .usage = RHIBufferUsage::TransferSource, .memory = RHIMemoryDomain::Upload};
			RHIStatus status = m_Device.CreateBuffer(destinationDesc, destination);
			if (status.IsSuccess())
				status = m_Device.CreateBuffer(stagingDesc, staging);
			if (status.IsSuccess())
				status = m_Device.WriteBuffer(staging, 0, data);
			if (!status.IsSuccess()) {
				discard();
				return status;
			}

			m_Pending.push_back({.destination = destination, .staging = staging, .size = size, .usage = usage});
			/* 待提交项持有暂存缓冲区，目标资源的所有权交给调用方。发布后，异常处理不再访问这些候选资源。 */
			buffer = std::exchange(destination, BufferHandle{});
			staging = {};
			return {};
		} catch (const std::bad_alloc&) {
			discard();
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
		}
	}

	RHIStatus UploadScheduler::CreateTexture(
	    std::span<const std::byte> data, const RHITextureDesc& desc, TextureHandle& texture, TextureViewHandle& view) {
		if (desc.mipLevels != 1 || desc.arrayLayers != 1)
			return {
			    .code = RHIErrorCode::InvalidDescription, .operation = "UploadScheduler/subresource copies required"};
		uint64_t bytes = 0;
		if (!TryGetTextureUploadByteSize(desc.extent, desc.format, bytes) || bytes != data.size())
			return {.code = RHIErrorCode::InvalidDescription, .operation = "UploadScheduler/tight texture byte count"};
		const RHIBufferTextureCopy copy;
		return CreateTexture(data, desc, std::span{&copy, 1}, texture, view);
	}

	RHIStatus UploadScheduler::CreateTexture(std::span<const std::byte> data, const RHITextureDesc& desc,
	    std::span<const RHIBufferTextureCopy> copies, TextureHandle& texture, TextureViewHandle& view) {
		constexpr const char* operation = "UploadScheduler::CreateTexture";
		texture = {};
		view = {};
		if (desc.mipLevels == 0 || desc.arrayLayers == 0 ||
		    copies.size() != uint64_t(desc.mipLevels) * desc.arrayLayers || data.empty())
			return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		std::set<std::pair<uint32_t, uint32_t>> seen;
		/* 逐项检查层级、数组层、像素范围及暂存字节偏移；在创建任何设备资源前拒绝非法复制。 */
		for (const auto& copy : copies) {
			const auto& range = copy.range;
			const Extent2D extent{std::max(1u, desc.extent.width >> std::min(range.baseMipLevel, 31u)),
			    std::max(1u, desc.extent.height >> std::min(range.baseMipLevel, 31u))};
			uint64_t bytes = 0;
			if (range.levelCount != 1 || range.layerCount != 1 || range.aspect != RHITextureAspect::Color ||
			    range.baseMipLevel >= desc.mipLevels || range.baseArrayLayer >= desc.arrayLayers ||
			    !seen.emplace(range.baseMipLevel, range.baseArrayLayer).second ||
			    !TryGetTextureUploadByteSize(extent, desc.format, bytes) || copy.bufferOffset > data.size() ||
			    bytes > data.size() - copy.bufferOffset)
				return {.code = RHIErrorCode::InvalidDescription, .operation = operation};
		}
		TextureHandle destination{};
		TextureViewHandle destinationView{};
		BufferHandle staging{};
		const auto discard = [&] {
			m_Device.ReleaseBuffer(std::exchange(staging, BufferHandle{}));
			m_Device.ReleaseTextureView(std::exchange(destinationView, TextureViewHandle{}));
			m_Device.ReleaseTexture(std::exchange(destination, TextureHandle{}));
		};
		try {
			if (m_PendingTextures.size() == m_PendingTextures.max_size())
				return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
			m_PendingTextures.reserve(m_PendingTextures.size() + 1);
			RHITextureDesc destinationDesc = desc;
			destinationDesc.usage = destinationDesc.usage | RHITextureUsage::TransferDestination;
			const RHIBufferDesc stagingDesc{
			    .size = data.size(), .usage = RHIBufferUsage::TransferSource, .memory = RHIMemoryDomain::Upload};
			std::vector<RHIBufferTextureCopy> ownedCopies(copies.begin(), copies.end());
			/* 先创建目标图像和配套视图，再创建并填写暂存缓冲。候选资源此时尚未进入上传队列。 */
			RHIStatus status = m_Device.CreateTexture(destinationDesc, destination);
			RHITextureViewDesc viewDesc;
			viewDesc.range.levelCount = desc.mipLevels;
			viewDesc.range.layerCount = desc.arrayLayers;
			viewDesc.type = desc.cubeCompatible ? RHITextureViewType::Cube
			    : desc.arrayLayers > 1          ? RHITextureViewType::Texture2DArray
			                                    : RHITextureViewType::Texture2D;
			if (status.IsSuccess())
				status = m_Device.CreateTextureView(destination, viewDesc, destinationView);
			if (status.IsSuccess())
				status = m_Device.CreateBuffer(stagingDesc, staging);
			if (status.IsSuccess())
				status = m_Device.WriteBuffer(staging, 0, data);
			if (!status.IsSuccess()) {
				discard();
				return status;
			}

			m_PendingTextures.push_back(
			    {.destination = destinationView, .staging = staging, .copies = std::move(ownedCopies)});
			texture = std::exchange(destination, TextureHandle{});
			view = std::exchange(destinationView, TextureViewHandle{});
			staging = {};
			return {};
		} catch (const std::bad_alloc&) {
			/* View先于图像归还，避免候选图像仍有View引用时提前销毁；Pending协议保持原样。 */
			discard();
			return {.code = RHIErrorCode::OutOfMemory, .operation = operation};
		}
	}

	RHIStatus UploadScheduler::AddPasses(RenderGraph& graph) {
		if (m_Recorded)
			return {.code = RHIErrorCode::InvalidState, .operation = "UploadScheduler::AddPasses"};

		m_Recorded = true;
		m_RecordedCount = m_Pending.size();
		m_RecordedTextureCount = m_PendingTextures.size();

		for (size_t i = 0; i < m_RecordedCount; ++i) {
			const PendingBuffer pending = m_Pending[i];
			RGBuffer source{}, destination{};

			if (const RHIStatus status = graph.ImportBuffer(pending.staging, source); !status.IsSuccess())
				return status;
			if (const RHIStatus status = graph.ImportBuffer(pending.destination, destination); !status.IsSuccess())
				return status;

			const RHIBufferCopy copy{.size = pending.size};

			if (const RHIStatus status = graph.AddCopyPass("Static Buffer Upload", source, destination, copy);
			    !status.IsSuccess())
				return status;
		}

		for (size_t i = 0; i < m_RecordedTextureCount; ++i) {
			const auto& pending = m_PendingTextures[i];
			RGBuffer source{};
			RGTexture destination{};

			if (const RHIStatus status = graph.ImportBuffer(pending.staging, source); !status.IsSuccess())
				return status;
			if (const RHIStatus status = graph.ImportTexture(pending.destination, destination); !status.IsSuccess())
				return status;

			for (const auto& copy : pending.copies)
				if (const RHIStatus status =
				        graph.AddTextureUploadPass("Static Texture Upload", source, destination, copy);
				    !status.IsSuccess())
					return status;
		}

		return {};
	}

	void UploadScheduler::OnSubmitted(const RHICompletionPoint& completion) noexcept {
		CAT_CORE_ASSERT(m_Recorded && completion.HasValue() && completion.device == m_Device.GetDeviceId(),
		    "Upload submission requires the completion point of its accepted drawing submission!");

		if (m_RecordedCount != 0 || m_RecordedTextureCount != 0)
			m_LastSubmission = completion;

		/* 只释放所有者引用；已提交命令继续保留 staging 至安全 reset。 */
		for (size_t i = 0; i < m_RecordedCount; ++i)
			m_Device.ReleaseBuffer(m_Pending[i].staging);
		for (size_t i = 0; i < m_RecordedTextureCount; ++i)
			m_Device.ReleaseBuffer(m_PendingTextures[i].staging);

		m_Pending.erase(m_Pending.begin(), m_Pending.begin() + static_cast<std::ptrdiff_t>(m_RecordedCount));
		m_PendingTextures.erase(
		    m_PendingTextures.begin(), m_PendingTextures.begin() + static_cast<std::ptrdiff_t>(m_RecordedTextureCount));

		m_RecordedCount = 0;
		m_RecordedTextureCount = 0;
		m_Recorded = false;
	}

	void UploadScheduler::OnCancelled() noexcept {
		m_RecordedCount = 0;
		m_RecordedTextureCount = 0;
		m_Recorded = false;
	}

	void UploadScheduler::OnPartiallySubmitted(std::span<const RHICompletionPoint> points) noexcept try {
		if (!m_Recorded)
			return;
		/* 用同一设备、同一队列上已受理的序号判断目标的最近写入是否属于本次提交。 */
		const auto accepted = [&](RHICompletionPoint writer) {
			return writer.HasValue() && std::any_of(points.begin(), points.end(), [&](const auto& point) {
				return point.device == writer.device && point.queue == writer.queue &&
				    point.sequence >= writer.sequence;
			});
		};
		/* 从后向前移除目标字节范围已完整初始化的缓冲上传，保留其余请求供下一帧重录。 */
		for (size_t i = m_RecordedCount; i > 0; --i) {
			const auto index = i - 1;
			RHIBufferStateSnapshot state;
			if (!m_Device.GetBufferState(m_Pending[index].destination, state).IsSuccess() ||
			    !accepted(state.state.frontier.writer) ||
			    !RangeContains(state.state.initialized, 0, m_Pending[index].size))
				continue;
			m_LastSubmission = state.state.frontier.writer;
			m_Device.ReleaseBuffer(m_Pending[index].staging);
			m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(index));
			--m_RecordedCount;
		}
		/* 纹理还须验证每个数组层和细节层级均已初始化，才可归还暂存缓冲的所有者引用。 */
		for (size_t i = m_RecordedTextureCount; i > 0; --i) {
			const auto index = i - 1;
			RHITextureStateSnapshot state;
			if (!m_Device.GetTextureState(m_PendingTextures[index].destination, state).IsSuccess() ||
			    !accepted(state.state.frontier.writer) || !state.state.initialized ||
			    !RangeContains(state.state.initializedSubresources, 0, uint64_t(state.mipLevels) * state.arrayLayers))
				continue;
			m_LastSubmission = state.state.frontier.writer;
			m_Device.ReleaseBuffer(m_PendingTextures[index].staging);
			m_PendingTextures.erase(m_PendingTextures.begin() + static_cast<std::ptrdiff_t>(index));
			--m_RecordedTextureCount;
		}
	} catch (const std::bad_alloc&) { /* 无法取得证明时继续保留staging，不能推测复制已完成。 */
	}

	void UploadScheduler::Shutdown() noexcept {
		for (const auto& pending : m_Pending)
			m_Device.ReleaseBuffer(pending.staging);
		for (const auto& pending : m_PendingTextures)
			m_Device.ReleaseBuffer(pending.staging);

		m_Pending.clear();
		m_PendingTextures.clear();
		m_RecordedCount = 0;
		m_RecordedTextureCount = 0;
		m_Recorded = false;
		m_LastSubmission = {};
	}

}
