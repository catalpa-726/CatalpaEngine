#pragma once
#include "Panels/FileBrowserView.h"
#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
    class BuildPanel;

    // 只读作者文件选择和导入设置；构建任务由工作区长期持有的BuildPanel管理。
    class ContentBrowserPanel final {
      public:
        void Reset();
        void Update() { m_Browser.Update(); }
        void Draw(bool& visible, const Tools::ProjectSession*, const std::filesystem::path& shaderRoot,
                  BuildPanel&, const PanelText&);

      private:
        void ClearSelection();
        std::filesystem::path m_Directory, m_Selected;
        FileBrowserView m_Browser{0x434f4e54454e5410ULL};
        std::string m_Error;
        size_t m_Root = 0;
        double m_UnitScale = 1;
        int m_Up = 0;
        bool m_Normals = true, m_Tangents = true;
        std::array<char, 4096> m_RemapUri{}, m_RemapTarget{};
        std::map<std::string, std::string, std::less<>> m_Remaps;
    };
}
