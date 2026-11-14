#pragma once
#include "catpch.h"

#include "Assets/AssetId.h"
namespace Catalpa {
	/* cppType 仅用于进程内检查类型转换，不进入持久化结构或文件。 */
	struct AssetLoadResult {
		std::shared_ptr<const void> value;
		std::type_index cppType{typeid(void)};
		size_t residentBytes = 0;
	};
	template <class T> AssetLoadResult MakeAssetLoadResult(std::shared_ptr<const T> value, size_t bytes) {
		return {std::move(value), std::type_index(typeid(T)), bytes};
	}
	struct AssetDependencyValue {
		AssetId id;
		uint64_t version = 0;
		std::shared_ptr<const void> value;
		std::type_index cppType{typeid(void)};
	};
}
