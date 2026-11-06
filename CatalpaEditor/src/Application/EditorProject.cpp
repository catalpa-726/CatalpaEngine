#include "catpch.h"
#include "Application/EditorProject.h"

namespace Catalpa::Editor {
    bool EditorProject::ApplyConfig(const Tools::ProjectConfig& candidate, std::string& error) {
        if (!m_Session || candidate.id != m_Session->config.id || candidate.projectFile != m_Session->config.projectFile) {
            error = "Project settings cannot change session identity or save target";
            return false;
        }
        std::unique_ptr<Tools::ProjectSession> prepared;
        if (!Tools::PrepareProjectSession(candidate, prepared, error) ||
            !Tools::SaveProjectConfig(prepared->config, error)) return false;
        m_Session.swap(prepared);
        return true;
    }
    bool EditorProject::Load(const std::filesystem::path& absoluteProjectFile, std::string& error) {
        // 共享加载器负责候选构造和事务发布；Editor不再另建一层候选/交换逻辑。
        return Tools::LoadProject(absoluteProjectFile, m_Session, error);
    }
    bool EditorProject::Save(std::string& error) const {
        if (!m_Session) { error = "No author project is open"; return false; }
        return Tools::SaveProjectConfig(m_Session->config, error);
    }
}
