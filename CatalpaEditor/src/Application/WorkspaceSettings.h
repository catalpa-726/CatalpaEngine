#pragma once

#include <filesystem>
#include <string>

namespace Catalpa::Editor {

    // 编辑器工作区配置；布局文本与场景/资产数据分开保存，内部面板 ID 保持稳定。
    struct WorkspaceSettings {
        bool vsync = true;
        bool showScene = true;
        bool showAssets = true;
        bool showInspector = true;
        bool showSettings = false;
        bool showContent = true;
        bool showBuild = true;
        bool showConsole = false;
        bool showDependencies = false;
        std::string language = "en-US";
        // 仅保存应用生成的 opaque ImGui 布局；人工编辑 JSON 时不要改 layout 或 layout_checksum。
        // 校验值用于检测意外变化，不是安全签名，也不验证 ImGui ini 的语义。
        std::string layout;
    };

    enum class WorkspaceLoadResult { Missing, Loaded, Failed };

    // 只读加载；所有字段通过校验后才替换 settings，缺失或失败均保留调用方状态。
    [[nodiscard]] WorkspaceLoadResult LoadWorkspaceSettings(
        const std::filesystem::path& path, WorkspaceSettings& settings, std::string& error);

    // 校验并保存完整工作区；平台层负责临时文件、备份与替换，失败不截断已有配置。
    [[nodiscard]] bool SaveWorkspaceSettings(
        const std::filesystem::path& path, const WorkspaceSettings& settings, std::string& error);

}
