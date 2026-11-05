#pragma once
#include "Panels/PanelText.h"
#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
    class BuildPanel;

    // 只读作者文件选择和导入设置；构建任务由工作区长期持有的BuildPanel管理。
    class ContentBrowserPanel final {
      public:
        void Reset();
        void Draw(bool& visible, const Tools::ProjectSession*, const std::filesystem::path& shaderRoot,
                  BuildPanel&, const PanelText&);

      private:
        struct Entry {
            std::filesystem::path path;
            std::string name;
            bool directory = false;
        };

        void ClearSelection();
        void Refresh(const std::filesystem::path& root);
        std::filesystem::path m_Directory, m_Selected;
        std::vector<Entry> m_Files;
        std::array<char, 128> m_Filter{};
        std::string m_Error;
        size_t m_Root = 0;
        double m_UnitScale = 1;
        int m_Up = 0;
        bool m_Normals = true, m_Tangents = true;
        std::array<char, 4096> m_RemapUri{}, m_RemapTarget{};
        std::map<std::string, std::string, std::less<>> m_Remaps;
    };
}
