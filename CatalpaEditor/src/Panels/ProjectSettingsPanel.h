#pragma once
#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
    class EditorProject;
    class ProjectSettingsPanel {
      public:
        void Draw(EditorProject&, bool buildsBusy, std::string& error);
        void Reset() { m_Project = {}; m_Draft.reset(); m_FieldOverflow = false; }
      private:
        StableId m_Project;
        std::optional<Tools::ProjectConfig> m_Draft;
        std::array<std::array<char, 2048>, 6> m_Fields{};
        bool m_FieldOverflow = false;
    };
}
