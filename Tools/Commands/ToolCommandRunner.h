#pragma once
#include "catpch.h"
#include "Core/Jobs/CancellationToken.h"
namespace Catalpa::Tools {
    struct ToolCommandResult {
        int exitCode=1;
        std::vector<std::string> diagnostics;
        std::vector<std::filesystem::path> outputs;
    };
    // 可链接无设备工具入口；宿主只转交UTF-8参数，首项为命令而不是程序名。
    class ToolCommandRunner final {
    public: static ToolCommandResult Run(std::span<const std::string> arguments,CancellationToken cancellation={});
    };
}
