#pragma once

#include "catpch.h"
#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
    // 编辑器仅持作者会话；通用解析/挂载/身份注册仍由可独立调用的 Tools 完成。
    class EditorProject final {
    public:
        bool Load(const std::filesystem::path& absoluteProjectFile, std::string& error);
        bool Save(std::string& error) const;
        bool ApplyConfig(const Tools::ProjectConfig&, std::string& error);
        [[nodiscard]] const Tools::ProjectSession* GetSession() const noexcept { return m_Session.get(); }
        // 调用方先取消并等待本项目的Build任务，再释放作者值；未来Document会话另行接入。
        void Close() noexcept { m_Session.reset(); }
    private:
        std::unique_ptr<Tools::ProjectSession> m_Session;
    };
}
