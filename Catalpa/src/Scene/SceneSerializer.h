#pragma once
#include "Scene/SceneAsset.h"
#include "Scene/SceneManager.h"

namespace Catalpa {
    class AssetManager;
    class Scene;
    struct AssetDecodeContext;
    class SchemaMigrationRegistry;
    class CATALPA_API SceneSerializer {
      public:
        static bool Capture(const Scene&, SceneAsset&, std::string& error);
        static bool Write(const SceneAsset&, std::string&, std::string& error);
        static bool Read(std::string_view, SceneAsset&, std::string& error);
        static bool ReadMigrated(std::string_view, const SchemaMigrationRegistry&, SceneAsset&, std::string& error);
        static std::vector<AssetId> Dependencies(const SceneAsset&);
        static bool ResolveMesh(const SceneMeshAsset&, AssetManager*, MeshRendererComponent&, std::string& error);
        static bool BindDependencies(SceneAsset&, const AssetDecodeContext&, std::string& error);
        // 候选场景全部创建及派生成功后才发布 handle，不改变 active 场景。
        static bool Instantiate(const SceneAsset&, SceneManager&, AssetManager*, SceneHandle&,
                                std::string& error);
    };
}
