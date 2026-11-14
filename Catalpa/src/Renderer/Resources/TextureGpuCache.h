#pragma once
#include "Core/Base.h"
#include "Assets/AssetHandle.h"
#include "Images/ImageAsset.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "RHI/RHICompletionPoint.h"
namespace Catalpa {
	class RHIDevice;
	class UploadScheduler;
	class ResourceResidencyManager;
	struct TextureGpuResource {
		RHIDevice* device = nullptr;
		AssetVersionRef<ImageAsset> asset;
		TextureHandle texture{};
		TextureViewHandle view{};
		bool uploadPending = true;
		bool acquired = false;
		bool stale = false;
		~TextureGpuResource();
	};
	/* 图片身份/版本复用纹理与view；sampler策略独立，CPU布局不被静默重排。 */
	class CATALPA_API TextureGpuCache {
	public:
		TextureGpuCache(RHIDevice& device, UploadScheduler& uploads, ResourceResidencyManager* residency = nullptr)
		    : m_Device(device), m_Uploads(uploads), m_Residency(residency) {}
		[[nodiscard]] RHIStatus Acquire(
		    const AssetVersionRef<ImageAsset>&, std::shared_ptr<TextureGpuResource>& output);
		/**
		 * @brief 环境资源内嵌图像使用独立的槽位命名空间。别名引用保活同一父资源，无须为图像构造全局 AssetId。
		 *
		 * @param[in] asset 随父载荷保活的图像版本。
		 * @param[in] slot 内嵌图像的插槽编号，允许 1 至 4；0 留给独立图像。
		 * @param[out] output 接收可供绘制使用的 GPU 资源。
		 *
		 * @return 返回指定内嵌插槽的纹理获取结果。
		 */
		[[nodiscard]] RHIStatus AcquireEmbedded(
		    const AssetVersionRef<ImageAsset>&, uint32_t slot, std::shared_ptr<TextureGpuResource>& output);
		/**
		 * @brief 更新已使用条目的驻留完成点并清除本帧获取与上传标记。
		 *
		 * @param[in] points 已接受这些资源使用的各队列完成点。
		 */
		void OnSubmitted(std::span<const RHICompletionPoint> points = {}) noexcept;
		/**
		 * @brief 根据上传和占用状态清理缓存；受管理的过期资源交由驻留管理器驱逐。
		 */
		void Prune();
		/**
		 * @brief 将不再对应当前资产版本的缓存项标为需要清理。
		 *
		 * @param[in] id 发生变动的资产标识。
		 * @param[in] currentVersion 当前应保留的资产版本。
		 */
		void Invalidate(AssetId id, uint64_t currentVersion) noexcept;
		/**
		 * @brief 将本次 GPU 使用关联到驻留资源的队列完成记录。
		 *
		 * @param[in] points 已经接受使用这些资源的队列完成点。
		 */
		void MarkUses(std::span<const RHICompletionPoint>) noexcept;
		/**
		 * @brief 归还全部缓存引用并解除当前载荷的驻留登记。
		 */
		void Clear() noexcept;
		[[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }

	private:
		RHIDevice& m_Device;
		UploadScheduler& m_Uploads;
		ResourceResidencyManager* m_Residency = nullptr;
		[[nodiscard]] RHIStatus AcquireInternal(
		    const AssetVersionRef<ImageAsset>&, uint32_t slot, std::shared_ptr<TextureGpuResource>& output);
		std::map<std::tuple<AssetId, uint64_t, uint32_t>, std::shared_ptr<TextureGpuResource>> m_Entries;
	};
}
