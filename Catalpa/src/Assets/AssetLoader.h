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
        template<class T> std::shared_ptr<const T> FindDependency(AssetId id, uint64_t version) const {
            for (const auto& item : dependencies)
                if (item.id == id && item.version == version && item.cppType == std::type_index(typeid(T)))
                    return std::static_pointer_cast<const T>(item.value);
            return {};
        }
    };
    // Worker decoder returns a fully owned immutable CPU value; it must obey maxResidentBytes.
    struct AssetLoader {
        AssetType type;
        std::type_index cppType{typeid(void)};
        std::function<bool(const AssetContainer&, const AssetDecodeContext&, AssetLoadResult&, std::string&)> decode;
        // Populated only by AssetManager::SetFallback after ordinary version publication;
        // RegisterLoader rejects caller-supplied fallback owners to preserve identity uniqueness.
        AssetId fallbackId;
        uint64_t fallbackVersion = 1;
        AssetLoadResult fallback;
    };
}
