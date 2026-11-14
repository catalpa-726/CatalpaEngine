#pragma once
#include "Materials/MaterialInstance.h"
#include "RHI/RHIHandle.h"
#include "RHI/RHIError.h"
#include "Materials/MaterialSurface.h"
namespace Catalpa {
	/* 按 Common/Surface.slang 的字段打包，不直接上传变体或领域结构的内存。 */
	struct alignas(16) MaterialGpuParameters {
		std::array<float, 4> baseColor{};
		std::array<float, 4> emissiveMetallic{};
		std::array<float, 4> roughnessNormalOcclusionCutoff{};
		std::array<float, 4> uvScaleOffset{};
		std::array<float, 4> timeEffect{};
	};
	static_assert(sizeof(MaterialGpuParameters) == 80 && offsetof(MaterialGpuParameters, emissiveMetallic) == 16 &&
	    offsetof(MaterialGpuParameters, roughnessNormalOcclusionCutoff) == 32 &&
	    offsetof(MaterialGpuParameters, uvScaleOffset) == 48 && offsetof(MaterialGpuParameters, timeEffect) == 64);
	class RHIDevice;
	struct MaterialGpuResource {
		RHIDevice* device = nullptr;
		AssetVersionRef<MaterialInstance> asset;
		BufferHandle parameters{};
		MaterialSurfaceParameters surface;
		~MaterialGpuResource();
	};
	/* 普通PBR/Unlit共用80字节Material UBO；Frame/View/Draw由帧上下文拥有。 */
	class CATALPA_API MaterialGpuCache {
	public:
		explicit MaterialGpuCache(RHIDevice& device) : m_Device(device) {}
		/**
		 * @brief 验证并复用指定材质实例版本所需的 GPU 资源。
		 *
		 * @param[in] asset 材质实例的固定资产版本。
		 * @param[out] output 接收可供绘制保活的材质资源。
		 *
		 * @return 返回指定版本材质的缓存查询或创建状态。
		 */
		[[nodiscard]] RHIStatus Acquire(
		    const AssetVersionRef<MaterialInstance>&, std::shared_ptr<MaterialGpuResource>& output);
		/**
		 * @brief 移除仅余缓存自身持有的条目。
		 */
		void Prune();
		void Clear() noexcept { m_Entries.clear(); }

	private:
		RHIDevice& m_Device;
		std::map<std::pair<AssetId, uint64_t>, std::shared_ptr<MaterialGpuResource>> m_Entries;
	};
}
