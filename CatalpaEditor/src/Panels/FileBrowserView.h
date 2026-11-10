#pragma once

#include "Panels/PanelText.h"
#include "Panels/BrowserDirectoryCache.h"
#include "Panels/BrowserThumbnails.h"

namespace Catalpa::Editor {
    // 项目 Assets 与导入浏览器使用相同的导航规则，导入选项仍由调用者管理。
    class FileBrowserView {
      public:
        explicit FileBrowserView(uint64_t id) : m_Thumbnails(id) {}
        void Reset();
        void Update() { m_Thumbnails.BeginFrame(); }
        bool Draw(const std::filesystem::path& root, std::string_view rootLabel,
                  std::filesystem::path& directory, std::filesystem::path& selected,
                  float height, const PanelText& text);
      private:
        void DrawFolder(const std::filesystem::path&, std::string_view, const std::filesystem::path& current,
                        std::filesystem::path& navigate, unsigned depth, unsigned& budget);
        std::filesystem::path m_Root;
        BrowserDirectoryCache m_Folders;
        std::array<char, 128> m_Filter{};
        bool m_Grid = true;
        BrowserThumbnails m_Thumbnails;
    };
}
