#pragma once

#include "catpch.h"
#include "Core/Jobs/JobHandle.h"
#include "Materials/MaterialInstance.h"
#include "Scene/SceneHandle.h"
#include "Scene/PersistentEntityId.h"
#include "imgui.h"

namespace Catalpa::Editor {
    // 两张独立图集：解码后的文件纹理与引擎实际着色的材质球共用各自的 surface。
    class BrowserThumbnails {
      public:
        explicit BrowserThumbnails(uint64_t id) : m_Base(id) {}
        void Reset();
        void BeginFrame();
        bool Draw(const std::filesystem::path& root, const std::filesystem::path& path,
                  ImVec2 min, ImVec2 max, std::string& diagnostic);

      private:
        static constexpr unsigned Side = 64, Columns = 4, Slots = Columns * Columns;
        struct Tile {
            std::filesystem::path path;
            int frame = -1;
            std::shared_ptr<const std::vector<std::byte>> rgba;
            std::string source, error;
            AssetVersionRef<MaterialInstance> material;
            bool pending = false;
        };
        struct Work {
            std::filesystem::path path;
            bool material = false;
            std::vector<std::byte> pixels;
            std::string source, error;
        };
        void UploadImages();
        void UpdateMaterials();
        bool Start(const std::filesystem::path&, Tile&, bool material);
        bool ResolveMaterial(Tile&);
        uint64_t m_Base;
        uint64_t m_Revision = 0;
        bool m_ImagesDirty = false, m_MaterialsDirty = false;
        std::array<Tile, Slots> m_Images, m_Materials;
        std::shared_ptr<Work> m_Work;
        JobHandle m_Job;
        SceneHandle m_MaterialScene;
        AssetId m_AtlasId;
        uint64_t m_ImageToken = 0, m_MaterialToken = 0;
        std::string m_Error;
    };
}
