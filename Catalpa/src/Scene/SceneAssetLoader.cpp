#include "catpch.h"
#include "Scene/SceneAssetLoader.h"
#include "Scene/SceneSerializer.h"
#include "Assets/AssetManager.h"

namespace Catalpa {
    bool RegisterSceneAssetLoader(AssetManager& manager, std::string& error) {
        AssetLoader loader;
        if (!AssetType::TryParse("scene", loader.type)) return false;
        loader.cppType = typeid(SceneAsset);
        loader.decode = [](const AssetContainer& container, const AssetDecodeContext& context,
                           AssetLoadResult& output, std::string& error) {
            const auto& bytes = container.payload;
            if (container.payloadVersion != 1 || context.cancellation.IsCancellationRequested() ||
                bytes.size() > context.maxResidentBytes) {
                error = "Scene decode budget or cancellation";
                return false;
            }
            auto value = std::make_shared<SceneAsset>();
            if (!SceneSerializer::Read(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                                       *value, error)) return false;
            if (!SceneSerializer::BindDependencies(*value, context, error)) return false;
            const size_t resident = EstimateSceneAssetResidentBytes(*value);
            if (resident > context.maxResidentBytes) {
                error = "Scene domain tree/fixed dependency budget";
                return false;
            }
            output = MakeAssetLoadResult<SceneAsset>(value, resident);
            return true;
        };
        return manager.RegisterLoader(std::move(loader), error);
    }
}
