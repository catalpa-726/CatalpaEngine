#pragma once
#include "Assets/AssetHandle.h"
#include "Geometry/MeshAsset.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "RHI/RHICompletionPoint.h"
namespace Catalpa {
	class RHIDevice;
	class UploadScheduler;
	class ResourceResidencyManager;
	/* 缓冲区只有一个所有者。CPU 资产固定版本，GPU 资源的回收则等待后端命令释放引用。 */
	struct GeometryGpuResource {
		RHIDevice* device = nullptr;
		AssetVersionRef<MeshAsset> asset;
		BufferHandle vertices{}, indices{};
		bool uploadPending = true;
		bool acquired = false;
		bool stale = false;
		bool hasSourceNormal = false;
		float maximumVertexColor = 1.0f;
		~GeometryGpuResource();
		/**
		 * @brief 提供几何缓存上传到 GPU 时采用的交错顶点布局。
		 *
		 * @return 返回步长为 64 字节的布局，属性依次为位置、法线、切线、第一组纹理坐标和颜色；属性表具有静态存储期。
		 */
		[[nodiscard]] static CATALPA_API VertexLayout GetVertexLayout() noexcept;
	};
	class CATALPA_API GeometryGpuCache {
	public:
		GeometryGpuCache(RHIDevice& device, UploadScheduler& uploads, ResourceResidencyManager* residency = nullptr)
		    : m_Device(device), m_Uploads(uploads), m_Residency(residency) {}
		/**
		 * @brief 按固定网格版本获取或上传顶点与索引缓冲区。
		 *
		 * @param[in] asset 待渲染网格的固定资产版本。
		 * @param[out] output 接收本次绘制需保活的几何资源。
		 *
		 * @return 操作状态，code 为 None 表示成功；失败时包含错误类别，可用时附带原生错误码。
		 */
		[[nodiscard]] RHIStatus Acquire(
		    const AssetVersionRef<MeshAsset>&, std::shared_ptr<GeometryGpuResource>& output);
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
		void Clear() noexcept; /* 调用方先结束尚未完成的上传。 */
		[[nodiscard]] size_t Size() const noexcept { return m_Entries.size(); }

	private:
		RHIDevice& m_Device;
		UploadScheduler& m_Uploads;
		ResourceResidencyManager* m_Residency = nullptr;
		std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<GeometryGpuResource>> m_Entries;
	};
}
