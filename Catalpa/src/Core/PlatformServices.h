#pragma once

#include "catpch.h"

#include "Core/Base.h"

namespace Catalpa {
    // 定位当前宿主的部署根，不从工作目录或沙盒名称推导路径；失败保留 output。
    [[nodiscard]] CATALPA_API bool GetExecutableDirectory(std::filesystem::path& output, std::string& error);
    // 系统随机源，单次至多1MiB；成功才覆盖调用方缓冲区，不退回时间/计数伪随机。
    [[nodiscard]] CATALPA_API bool FillRandomBytes(std::span<std::byte> output, std::string& error);
    // 内容身份使用真实SHA-256；失败不发布部分摘要，空输入亦有确定摘要。
    [[nodiscard]] CATALPA_API bool ComputeSha256(std::span<const std::byte> input, std::array<std::byte, 32>& output, std::string& error);
    // 返回含程序名的UTF-8参数，避免作者项目路径经过ANSI argv丢失Unicode字符。
    [[nodiscard]] CATALPA_API bool GetProcessArguments(std::vector<std::string>& output, std::string& error);
    // 比较已canonical的绝对路径组件（含根本身），Windows使用ordinal忽略大小写。
    // 不解析reparse点，不是访问授权检查；调用方负责先解析实际路径，VFS另验最终句柄。
    [[nodiscard]] CATALPA_API bool IsPathWithinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root);
}
