#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Catalpa::Editor {

    // 返回实际可执行文件目录；设置与语言资源由调用方拼接，均不依赖工作目录。
    [[nodiscard]] bool GetWorkspacePaths(std::filesystem::path& executableDir, std::string& error);

    // 有界读取完整文件；失败保留 text，不修改源文件。
    [[nodiscard]] bool ReadWorkspaceText(
        const std::filesystem::path& path, std::string& text, std::size_t maxBytes, std::string& error);

    // 同目录写临时文件并刷新，再备份旧文件至 .bak、替换目标；失败保留原配置。
    [[nodiscard]] bool WriteWorkspaceTextAtomic(
        const std::filesystem::path& path, std::string_view text, std::string& error);

}
