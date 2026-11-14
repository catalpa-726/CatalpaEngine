#pragma once
#include "catpch.h"

#include "Assets/AssetContainerFormat.h"
#include "Assets/AssetLoadResult.h"
#include "Core/Jobs/CancellationToken.h"
namespace Catalpa {
	struct AssetDecodeContext {
		CancellationToken cancellation;
		size_t maxResidentBytes = 0;
		std::vector<AssetDependencyValue> dependencies;
		/* 惰性流送依赖仍须匹配目录的精确版本/摘要，只延迟像素/PCM的读取。 */
		std::vector<AssetDependency> declaredDependencies;
		/* 仅捕获不可变VFS/目录快照；工作线程读取旧版本chunk，不借用Manager或World。 */
		std::function<bool(AssetId, size_t, std::vector<std::byte>&, std::string&)> readDeferred;
		/**
		 * @brief 查找依赖。
		 *
		 * @param[in] id 用于定位目标的资产标识。
		 * @param[in] version 被请求的确切依赖版本，须与解码上下文中登记的版本相同。
		 *
		 * @return 持有匹配资产和版本的只读共享指针；标识、版本或 C++ 类型不匹配时为空。
		 */
		template <class T> std::shared_ptr<const T> FindDependency(AssetId id, uint64_t version) const {
			for (const auto& item : dependencies)
				if (item.id == id && item.version == version && item.cppType == std::type_index(typeid(T)))
					return std::static_pointer_cast<const T>(item.value);
			return {};
		}
	};
	/* 工作线程的解码器须在 maxResidentBytes 预算内返回拥有其数据的只读 CPU 结果。 */
	struct AssetLoader {
		AssetType type;
		std::type_index cppType{typeid(void)};
		std::function<bool(const AssetContainer&, const AssetDecodeContext&, AssetLoadResult&, std::string&)> decode;
		/*
		 * 仅由 AssetManager::SetFallback 在常规版本成功发布后填充。
		 * RegisterLoader 拒绝调用者预先填入的回退资源，以免同一身份对应多个所有者。
		 */
		AssetId fallbackId;
		uint64_t fallbackVersion = 1;
		AssetLoadResult fallback;
		bool deferredDependencies = false;
	};
}
