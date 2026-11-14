#pragma once
#include "Scene/Scene.h"
#include "Geometry/ModelAsset.h"
#include "Materials/MaterialInstance.h"

namespace Catalpa {
	/* 显式预览覆盖不改写ModelAsset中的作者材质引用；为空时使用作者的固定版本。 */
	struct ModelMaterialOverride {
		AssetId source;
		AssetVersionRef<MaterialInstance> value;
	};

	/* 仅主线程读写。成功结果在整个模型的单条结构命令提交后发布。 */
	struct ModelInstantiationResult {
		bool complete = false, success = false;
		bool cancelRequested = false; /* 主线程在命令提交前请求取消，整批仍保持零发布。 */
		std::string error;
		std::vector<EntityReference> roots;
		std::vector<std::pair<StableId, EntityReference>> nodes;
	};

	class CATALPA_API ModelInstantiation final {
	public:
		/**
		 * @brief 校验模型及其固定依赖版本，将实例化操作排入场景命令队列。
		 * 引用均为确切版本；函数不加载资源。任何验证/提交失败回滚本次全部实体。
		 *
		 * @param[in,out] scene 接收实例化命令的目标场景。
		 * @param[in] model 待实例化模型的固定版本引用。
		 * @param[in] meshes 模型引用的网格版本快照。
		 * @param[in] materials 实例化所需的材质版本快照。
		 * @param[out] output 接收用于跟踪本次排队实例化结果的共享对象。
		 * @param[out] error 失败时接收原因说明。
		 * @param[in] parent 新模型根节点要挂接的父实体；空值表示场景根。
		 * @param[in] overrides 将模型使用的源材质标识映射到替换材质的固定版本。每个源标识最多出现一次。
		 *
		 * @return 实例化命令已入队时为 true；实际创建结果在提交后从 output 读取。
		 */
		static bool Queue(Scene& scene, AssetVersionRef<ModelAsset> model,
		    std::vector<AssetVersionRef<MeshAsset>> meshes, std::vector<AssetVersionRef<MaterialInstance>> materials,
		    std::shared_ptr<ModelInstantiationResult>& output, std::string& error, EntityId parent = {},
		    std::span<const ModelMaterialOverride> overrides = {});
	};
}
