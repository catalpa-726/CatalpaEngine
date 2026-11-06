#pragma once
#include "catpch.h"

namespace Catalpa::Editor {
    // 原生对话框只返回路径和取消状态，加载/保存由文档服务校验。
    bool SelectAuthorFile(bool save, const std::filesystem::path& initialDirectory,
                         std::filesystem::path& selected, bool& cancelled, std::string& error,
                         std::string_view defaultExtension = {});
}
