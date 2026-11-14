#pragma once
#include "Terrain/TerrainAsset.h"
#include "Physics/PhysicsService.h"
#include "Scene/Components/TerrainComponent.h"
#include "Core/Memory/MemoryBudget.h"
namespace Catalpa {
	class JobSystem;
	struct TerrainTileDraw {
		EntityReference owner;
		uint64_t tileKey = 0;
		uint32_t x = 0, z = 0, lod = 0;
		AssetVersionRef<MeshAsset> mesh;
		AssetVersionRef<MaterialInstance> material;
		Math::Mat4 world{1};
	};
	/**
	 * @brief 捕获地形组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[out] output 接收 DynamicComponentMap 结果。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool CaptureTerrainComponents(const Scene&, EntityId, DynamicComponentMap&, std::string&);
	/**
	 * @brief 恢复地形组件。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] values 含待恢复地形组件记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreTerrainComponents(Scene&, EntityId, const DynamicComponentMap&, std::string&);
	/**
	 * @brief 恢复地形组件。
	 *
	 * @param[in] world 实体及组件所在的 ECS 世界。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] values 含待恢复地形组件记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool RestoreTerrainComponents(World&, EntityId, const DynamicComponentMap&, std::string&);
	/**
	 * @brief 校验地形组件。
	 *
	 * @param[in] values 含待验证地形组件记录的字段映射。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
	 */
	CATALPA_API bool ValidateTerrainComponents(const DynamicComponentMap&, std::string&);
	/**
	 * @brief 列出地形组件引用的资产标识。
	 *
	 * @param[in] values 用于提取所引用地形资产的组件字段映射。
	 *
	 * @return 上述遍历或筛选得到的资产标识列表。
	 */
	CATALPA_API std::vector<AssetId> TerrainComponentDependencies(const DynamicComponentMap&);
	/**
	 * @brief 设置地形。
	 *
	 * @param[in] scene 此次操作所针对的场景。
	 * @param[in] id 用于定位目标的实体标识。
	 * @param[in] value 要写入目标实体的地形组件及其资产引用。
	 *
	 * @return 世界操作的状态，失败时包含对应的错误类别。
	 */
	CATALPA_API WorldStatus SetTerrain(Scene&, EntityId, const TerrainComponent&);
	/* CPU 瓦片数据随快照回收。GPU 资源复用渲染器已有的缓存和完成点集合。 */
	class CATALPA_API TerrainService final {
	public:
		TerrainService();
		~TerrainService();
		/**
		 * @brief 根据相机位置更新所需瓦片，并处理异步生成结果。
		 *
		 * @param[in] scene 此次操作所针对的场景。
		 * @param[in] assets 提供资产查找和版本访问的管理器。
		 * @param[in] jobs 作业系统。
		 * @param[in] cameraWorld 用于选择地形细节层级的相机世界坐标。
		 * @param[in] physics 用于维护地形碰撞体的物理世界。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 成功时返回 true，失败时返回 false 并通过错误参数说明原因。
		 */
		bool Tick(Scene&, AssetManager&, JobSystem&, Math::Vec3 cameraWorld, PhysicsWorld*, std::string&);
		/**
		 * @brief 取消待处理的地形作业并回收工作资源。
		 */
		void Stop() noexcept;
		const std::vector<TerrainTileDraw>& Draws() const noexcept;
		const std::vector<std::string>& Diagnostics() const noexcept;
		MemoryBudgetStats Budget() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
} /* 命名空间 Catalpa */
