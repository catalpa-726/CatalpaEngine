#pragma once

#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
    // 定位当前宿主的部署根，不从工作目录或沙盒名称推导路径；失败保留 output。
    [[nodiscard]] CATALPA_API bool GetExecutableDirectory(std::filesystem::path& output, std::string& error);
}
