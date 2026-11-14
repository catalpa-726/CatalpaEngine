#include "catpch.h"

#include "Renderer/Resources/TextureGpuCache.h"

#include "Renderer/Uploads/UploadScheduler.h"
#include "Images/ImageValidation.h"
#include "RHI/RHIDevice.h"
#include "Renderer/Resources/ResourceResidencyManager.h"
namespace Catalpa {
	TextureGpuResource::~TextureGpuResource() {
		if (device) {
			device->ReleaseTextureView(view);
			device->ReleaseTexture(texture);
		}
	}

	RHIStatus TextureGpuCache::Acquire(
	    const AssetVersionRef<ImageAsset>& asset, std::shared_ptr<TextureGpuResource>& output) {
		return AcquireInternal(asset, 0, output);
	}

	RHIStatus TextureGpuCache::AcquireEmbedded(
	    const AssetVersionRef<ImageAsset>& asset, uint32_t slot, std::shared_ptr<TextureGpuResource>& output) {
		if (slot == 0 || slot > 4)
			return {.code = RHIErrorCode::InvalidDescription, .operation = "TextureGpuCache/embedded slot"};
		return AcquireInternal(asset, slot, output);
	}

	/**
	 * @brief 在图像身份、版本和槽位都匹配时复用纹理，否则创建上传资源。
	 *
	 * @param[in] asset 待使用的固定图像版本。
	 * @param[in] slot 区分同一资产内不同图像的插槽。
	 * @param[out] output 接收已匹配或新建的纹理资源。
	 *
	 * @return 返回纹理缓存命中或创建上传资源的状态。
	 */
	RHIStatus TextureGpuCache::AcquireInternal(
	    const AssetVersionRef<ImageAsset>& asset, uint32_t slot, std::shared_ptr<TextureGpuResource>& output) {
		constexpr const char* op = "TextureGpuCache::Acquire";
		std::string error;
		if (!asset.HasValue())
			return {.code = RHIErrorCode::InvalidDescription, .operation = op};
		const auto key = std::make_tuple(asset.GetId(), asset.GetVersion(), slot);
		auto found = m_Entries.find(key);
		if (found != m_Entries.end()) {
			if (found->second->asset.Get() != asset.Get())
				return {.code = RHIErrorCode::InvalidState, .operation = "TextureGpuCache/identity-version collision"};
			if (found->second->view.HasValue()) {
				found->second->acquired = true;
				output = found->second;
				return {};
			}
		}
		if (!ValidateImageAsset(*asset.Get(), error))
			return {.code = RHIErrorCode::InvalidDescription, .operation = op};
		const auto& image = asset.Get()->image;
		const auto format = GetPixelFormatInfo(image.format);
		const bool cube = image.dimension == ImageDimension::Cube;
		if ((!cube && image.dimension != ImageDimension::Texture2D) || image.depth != 1 || image.arrayLayers != 1 ||
		    format.isDepth || format.isInteger || !m_Device.GetFormatSupport(image.format).sampledImage)
			return {.code = RHIErrorCode::Unsupported, .operation = op};
		std::vector<RHIBufferTextureCopy> copies;
		for (const auto& sub : image.subresources) {
			if (sub.offset % format.bytesPerBlock != 0)
				return {.code = RHIErrorCode::InvalidDescription, .operation = "TextureGpuCache/block alignment"};
			if (sub.rowPitch !=
			        ((uint64_t(sub.width) + format.blockWidth - 1) / format.blockWidth) * format.bytesPerBlock ||
			    sub.slicePitch !=
			        sub.rowPitch * ((uint64_t(sub.height) + format.blockHeight - 1) / format.blockHeight) ||
			    sub.byteLength != sub.slicePitch)
				return {.code = RHIErrorCode::Unsupported, .operation = op};
			copies.push_back({.bufferOffset = sub.offset,
			    .range = {.baseMipLevel = sub.mip, .baseArrayLayer = cube ? sub.layer * 6 + sub.face : sub.layer}});
		}
		if (found == m_Entries.end()) {
			auto resource = std::make_shared<TextureGpuResource>();
			resource->device = &m_Device;
			resource->asset = asset;
			if (m_Residency) {
				if (const auto status = m_Residency->MakeRoom(image.pixels.size(),
				        [this](
				            const auto& point, bool& complete) { return m_Device.QueryCompletion(point, complete); });
				    !status.IsSuccess())
					return status;
				if (const auto status = m_Residency->Track(
				        {asset.GetId(), asset.GetVersion(), slot}, image.pixels.size(),
				        [this, key] {
					        const auto it = m_Entries.find(key);
					        return it != m_Entries.end() && !it->second->uploadPending && it->second.use_count() == 1;
				        },
				        [this, key] { m_Entries.erase(key); });
				    !status.IsSuccess())
					return status;
			}
			try {
				found = m_Entries.emplace(key, std::move(resource)).first;
			} catch (...) {
				if (m_Residency)
					m_Residency->Forget({asset.GetId(), asset.GetVersion(), slot});
				throw;
			}
		}
		auto& resource = *found->second;
		if (!resource.texture.HasValue()) {
			const RHITextureDesc desc{.extent = {image.width, image.height},
			    .format = image.format,
			    .usage = RHITextureUsage::Sampled,
			    .mipLevels = image.mipLevels,
			    .arrayLayers = cube ? image.arrayLayers * 6 : image.arrayLayers,
			    .cubeCompatible = cube};
			if (const auto status =
			        m_Uploads.CreateTexture(image.pixels, desc, copies, resource.texture, resource.view);
			    !status.IsSuccess())
				return status;
		}
		resource.acquired = true;
		output = found->second;
		return {};
	}

	void TextureGpuCache::OnSubmitted(std::span<const RHICompletionPoint> points) noexcept {
		for (auto& [key, value] : m_Entries) {
			if (m_Residency && (value->acquired || value->uploadPending || value.use_count() > 1))
				(void)m_Residency->Touch({std::get<0>(key), std::get<1>(key), std::get<2>(key)}, points);
			value->uploadPending = false;
			value->acquired = false;
		}
	}

	void TextureGpuCache::Prune() {
		if (m_Residency) {
			for (const auto& [key, value] : m_Entries)
				if (value->stale)
					m_Residency->RequestEviction({std::get<0>(key), std::get<1>(key), std::get<2>(key)});
			return;
		}
		for (auto it = m_Entries.begin(); it != m_Entries.end();)
			if (!it->second->uploadPending && it->second.use_count() == 1)
				it = m_Entries.erase(it);
			else
				++it;
	}

	void TextureGpuCache::Clear() noexcept {
		if (m_Residency)
			for (const auto& [key, value] : m_Entries)
				m_Residency->Forget({std::get<0>(key), std::get<1>(key), std::get<2>(key)});
		m_Entries.clear();
	}

	void TextureGpuCache::Invalidate(AssetId id, uint64_t version) noexcept {
		for (auto& [key, value] : m_Entries)
			if ((!id.HasValue() || std::get<0>(key) == id) && std::get<1>(key) != version)
				value->stale = true;
	}

	void TextureGpuCache::MarkUses(std::span<const RHICompletionPoint> points) noexcept {
		if (m_Residency)
			for (auto& [key, value] : m_Entries)
				if (value->acquired || value->uploadPending || value.use_count() > 1)
					(void)m_Residency->Touch({std::get<0>(key), std::get<1>(key), std::get<2>(key)}, points);
	}
}
