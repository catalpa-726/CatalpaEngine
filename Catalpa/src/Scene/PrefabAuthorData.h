#pragma once
#include "Scene/SceneSerializer.h"
#include "Scene/DomainComponents.h"
#include "Core/Serialization/AuthorJson.h"

/* 实现私有的比较表示；固定版本引用转换为资产身份，不比较 GPU 或 C++ 对象地址。 */
namespace Catalpa::PrefabDetail {
	/**
	 * @brief 按实例实体标识建立反查表，供比较预制体作者数据时恢复局部标识。
	 *
	 * @param[in] mapping 从预制体局部实体标识到实例持久标识的已有映射。
	 *
	 * @return 键为实例实体持久标识、值为预制体局部持久标识的反向映射。
	 */
	inline auto Inverse(const std::map<PersistentEntityId, PersistentEntityId>& mapping) {
		std::map<PersistentEntityId, PersistentEntityId> inverse;
		for (const auto& [local, instance] : mapping)
			inverse.emplace(instance, local);
		return inverse;
	}
	/**
	 * @brief 将实例实体及其脚本、行为和动态组件中的实体引用转换成预制体局部标识。
	 *
	 * @param[in] node 待局部化的实体副本，包含自身标识及可能引用其他实体的字段。
	 * @param[in] inverse 按实例持久标识查找对应预制体局部标识的已有映射。
	 *
	 * @return 局部化后的实体副本。映射中不存在的标识保留原值；领域引用转换失败时抛出异常。
	 */
	inline SceneEntityAsset LocalizeWithInverse(
	    SceneEntityAsset node, const std::map<PersistentEntityId, PersistentEntityId>& inverse) {
		if (const auto id = inverse.find(node.id); id != inverse.end())
			node.id = id->second;
		const auto values = [&](ScriptValues& fields) {
			for (auto& [name, value] : fields)
				if (auto* id = std::get_if<PersistentEntityId>(&value))
					if (const auto local = inverse.find(*id); local != inverse.end())
						*id = local->second;
		};
		if (node.script)
			values(node.script->properties);
		if (node.behavior)
			values(node.behavior->variables);
		std::string error;
		if (!RemapDomainEntityReferences(node.dynamicComponents, inverse, error))
			throw std::invalid_argument(error);
		return node;
	}
	inline SceneEntityAsset Localize(
	    SceneEntityAsset node, const std::map<PersistentEntityId, PersistentEntityId>& mapping) {
		return LocalizeWithInverse(std::move(node), Inverse(mapping));
	}
	/**
	 * @brief 为覆盖比较生成排除父关系和实例元数据的实体 JSON。
	 *
	 * @param[in] node 待比较的实体副本，生成 JSON 前会移除其父关系和预制体实例元数据。
	 *
	 * @return 用场景序列化规则生成的单个实体 JSON 对象；序列化失败时抛出异常。
	 */
	inline AuthorJson::Json EntityText(SceneEntityAsset node) {
		node.parent = {};
		node.prefab.reset();
		SceneAsset one;
		one.entities.push_back(std::move(node));
		std::string text, error;
		if (!SceneSerializer::Write(one, text, error))
			throw std::invalid_argument(error);
		return AuthorJson::Json::parse(text).at("entities").front();
	}
	/**
	 * @brief 检查属性路径是否属于预制体支持覆盖的字段。
	 *
	 * @param[in] path 待检查是否允许作为预制体覆盖目标的字段路径。
	 *
	 * @return 布尔结果，true 表示上述操作或判断成立。
	 */
	inline bool AllowedPath(std::string_view path) {
		return path == "/name" || path == "/translation" || path.starts_with("/translation/") || path == "/rotation" ||
		    path.starts_with("/rotation/") || path == "/scale" || path.starts_with("/scale/") ||
		    path == "/mesh/color" || path.starts_with("/mesh/color/") || path == "/mesh/visible";
	}
}
