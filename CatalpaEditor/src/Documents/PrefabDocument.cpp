#include "Documents/PrefabDocument.h"

#include "Scene/SceneSerializer.h"

namespace Catalpa::Editor {
	bool PrefabDocument::FromSelection(
	    SceneDocument& doc, PersistentEntityId root, PrefabAsset& output, std::string& error) {
		auto* scene = doc.Resolve();
		SceneAsset all;
		if (!scene || doc.Playing() || !SceneSerializer::Capture(*scene, all, error))
			return false;
		std::map<PersistentEntityId, std::vector<PersistentEntityId>> children;
		bool foundRoot = false;
		for (const auto& node : all.entities) {
			children[node.parent].push_back(node.id);
			foundRoot |= node.id == root;
		}
		if (!foundRoot) {
			error = "Select an author entity before creating a prefab";
			return false;
		}

		/* 只遍历所选子树一次，避免深层级导出反复扫描全部实体。 */
		std::vector<PersistentEntityId> pending{root};
		std::set<PersistentEntityId> ids{root};
		for (size_t i = 0; i < pending.size(); ++i)
			for (auto child : children[pending[i]])
				if (ids.insert(child).second)
					pending.push_back(child);
		std::erase_if(all.entities, [&](const auto& node) { return !ids.contains(node.id); });
		if (all.entities.empty()) {
			error = "Select an author entity before creating a prefab";
			return false;
		}
		for (auto& node : all.entities) {
			if (node.id == root)
				node.parent = {};
			node.prefab.reset();
			const auto check = [&](const ScriptValues& values) {
				for (const auto& [name, value] : values)
					if (const auto* ref = std::get_if<PersistentEntityId>(&value); ref && !ids.contains(*ref)) {
						error = "Prefab export contains an external entity reference in " + name;
						return false;
					}
				return true;
			};
			if ((node.script && !check(node.script->properties)) || (node.behavior && !check(node.behavior->variables)))
				return false;
		}
		PrefabAsset candidate{std::move(all), {}};
		std::string checked;
		if (!WritePrefab(candidate, checked, error))
			return false;
		output = std::move(candidate);
		return true;
	}
}
