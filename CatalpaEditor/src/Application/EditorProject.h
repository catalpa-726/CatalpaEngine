#pragma once

#include "catpch.h"
#include "Project/ProjectConfig.h"

namespace Catalpa::Editor {
    // 编辑器仅持作者会话；通用解析/挂载/身份注册仍由可独立调用的 Tools 完成。
    class EditorProject final {
    public:
        bool Load(const std::filesystem::path& absoluteProjectFile, std::string& error);
        bool Save(std::string& error) const;
        [[nodiscard]] const Tools::ProjectSession* GetSession() const noexcept { return m_Session.get(); }
        // 当前只释放项目值/只读挂载；未声称完成未来 Document/Jobs 的关闭协议。
        void Close() noexcept { m_Session.reset(); }
    private:
        std::unique_ptr<Tools::ProjectSession> m_Session;
    };
}
