#include "catpch.h"
#include "WorkspaceSettings.h"
#include "../Platform/WorkspaceFiles.h"

#include <nlohmann/json.hpp>

namespace Catalpa::Editor {

    namespace {

        constexpr size_t MaxWorkspaceBytes = 1024 * 1024;

        // 对解码后的布局原始字节做 64 位 FNV-1a，固定输出 16 位小写十六进制。
        // 只检测应用生成的 opaque layout 是否意外变化，不是安全签名或第二份 ini 解析器。
        std::string LayoutChecksum(std::string_view layout)
        {
            uint64_t hash = 14695981039346656037ull;
            for (const char value : layout) {
                hash ^= static_cast<unsigned char>(value);
                hash *= 1099511628211ull;
            }
            constexpr char digits[] = "0123456789abcdef";
            std::string checksum(16, '0');
            for (size_t i = checksum.size(); i > 0; --i) {
                checksum[i - 1] = digits[hash & 0xf];
                hash >>= 4;
            }
            return checksum;
        }

        // 限制解析结构深度；跳过字符串内的括号，非法 JSON 仍由解析器报告。
        bool HasBoundedJsonDepth(std::string_view text)
        {
            size_t depth = 0;
            bool quoted = false, escaped = false;
            for (const char value : text) {
                if (value == '\0') return false;
                if (quoted) {
                    if (escaped) escaped = false;
                    else if (value == '\\') escaped = true;
                    else if (value == '"') quoted = false;
                }
                else if (value == '"') quoted = true;
                else if (value == '{' || value == '[') {
                    if (++depth > 8) return false;
                }
                else if (value == '}' || value == ']') {
                    if (depth == 0) return false;
                    --depth;
                }
            }
            return depth == 0 && !quoted;
        }

        // dump 使用严格 UTF-8；预先拒绝非法编码，避免正常错误路径依赖异常或替换字符。
        bool IsValidUtf8(std::string_view text)
        {
            size_t index = 0;
            while (index < text.size()) {
                const auto first = static_cast<unsigned char>(text[index++]);
                if (first < 0x80) continue;
                uint32_t codepoint = 0, minimum = 0;
                size_t continuation = 0;
                if (first >= 0xc2 && first <= 0xdf) { codepoint = first & 0x1f; minimum = 0x80; continuation = 1; }
                else if (first >= 0xe0 && first <= 0xef) { codepoint = first & 0x0f; minimum = 0x800; continuation = 2; }
                else if (first >= 0xf0 && first <= 0xf4) { codepoint = first & 0x07; minimum = 0x10000; continuation = 3; }
                else return false;
                if (continuation > text.size() - index) return false;
                for (size_t i = 0; i < continuation; ++i) {
                    const auto value = static_cast<unsigned char>(text[index++]);
                    if ((value & 0xc0) != 0x80) return false;
                    codepoint = (codepoint << 6) | (value & 0x3f);
                }
                if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
            }
            return true;
        }

        // 语言键使用固定白名单，布局和作者命名保持独立。
        bool ValidateSettings(const WorkspaceSettings& settings, std::string& error)
        {
            if (settings.language != "en-US" && settings.language != "zh-CN")
                error = "Unsupported workspace language.";
            else if (settings.layout.size() > MaxWorkspaceBytes) error = "Workspace layout exceeds the 1 MiB limit.";
            else if (settings.layout.find('\0') != std::string::npos) error = "Workspace layout contains an embedded null byte.";
            else if (!IsValidUtf8(settings.layout)) error = "Workspace layout is not valid UTF-8.";
            else return true;
            return false;
        }

    }

    WorkspaceLoadResult LoadWorkspaceSettings(
        const std::filesystem::path& path, WorkspaceSettings& settings, std::string& error)
    {
        error.clear();
        std::error_code statusError;
        const auto status = std::filesystem::status(path, statusError);
        if (statusError == std::errc::no_such_file_or_directory || (!statusError && !std::filesystem::exists(status)))
            return WorkspaceLoadResult::Missing;
        if (statusError) {
            error = "Cannot inspect workspace settings: " + statusError.message();
            return WorkspaceLoadResult::Failed;
        }

        std::string text;
        if (!ReadWorkspaceText(path, text, MaxWorkspaceBytes, error)) return WorkspaceLoadResult::Failed;
        if (!HasBoundedJsonDepth(text)) {
            error = "Workspace JSON has invalid structure, a null byte, or excessive nesting.";
            return WorkspaceLoadResult::Failed;
        }

        const auto document = nlohmann::json::parse(text, nullptr, false);
        if (!document.is_object() || document.size() != 6) {
            error = "Workspace settings require a JSON object with exactly six schema fields.";
            return WorkspaceLoadResult::Failed;
        }
        const auto version = document.find("schema_version");
        const auto language = document.find("language");
        const auto vsync = document.find("vsync");
        const auto panels = document.find("panels");
        const auto layout = document.find("layout");
        const auto checksum = document.find("layout_checksum");
        if (version == document.end() || !version->is_number_integer() || *version != 1) {
            error = "Unsupported workspace schema_version; expected integer 1.";
            return WorkspaceLoadResult::Failed;
        }
        if (language == document.end() || !language->is_string() || vsync == document.end() || !vsync->is_boolean() ||
            layout == document.end() || !layout->is_string() || checksum == document.end() || !checksum->is_string() ||
            panels == document.end() || !panels->is_object() || (panels->size() != 4 && panels->size() != 8)) {
            error = "Workspace language/layout/layout_checksum must be strings, vsync a boolean, and panels a supported four/eight-field object.";
            return WorkspaceLoadResult::Failed;
        }

        WorkspaceSettings parsed;
        parsed.language = *language->get_ptr<const std::string*>();
        parsed.vsync = *vsync->get_ptr<const bool*>();
        parsed.layout = *layout->get_ptr<const std::string*>();
        const auto readPanel = [&](const char* key, bool& value) {
            const auto entry = panels->find(key);
            if (entry == panels->end() || !entry->is_boolean()) {
                error = std::string("Workspace panels.") + key + " must be a boolean.";
                return false;
            }
            value = *entry->get_ptr<const bool*>();
            return true;
        };
        if (!readPanel("scene", parsed.showScene) || !readPanel("assets", parsed.showAssets) ||
            !readPanel("inspector", parsed.showInspector) || !readPanel("settings", parsed.showSettings) ||
            !ValidateSettings(parsed, error)) return WorkspaceLoadResult::Failed;
        // 旧四面板配置保持兼容；新配置一次保存全部八个稳定面板键。
        if (panels->size() == 8 &&
            (!readPanel("content", parsed.showContent) || !readPanel("build", parsed.showBuild) ||
             !readPanel("console", parsed.showConsole) ||
             !readPanel("dependencies", parsed.showDependencies)))
            return WorkspaceLoadResult::Failed;

        const auto& storedChecksum = *checksum->get_ptr<const std::string*>();
        if (storedChecksum.size() != 16 || storedChecksum != LayoutChecksum(parsed.layout)) {
            error = "Workspace layout checksum mismatch; keep the application-generated layout and checksum unchanged.";
            return WorkspaceLoadResult::Failed;
        }

        settings = std::move(parsed);
        return WorkspaceLoadResult::Loaded;
    }

    bool SaveWorkspaceSettings(const std::filesystem::path& path, const WorkspaceSettings& settings, std::string& error)
    {
        error.clear();
        if (!ValidateSettings(settings, error)) return false;
        const nlohmann::json document{{"schema_version", 1},
                                      {"language", settings.language},
                                      {"vsync", settings.vsync},
                                      {"panels",
                                       {{"scene", settings.showScene},
                                        {"assets", settings.showAssets},
                                        {"inspector", settings.showInspector},
                                        {"settings", settings.showSettings},
                                        {"content", settings.showContent},
                                        {"build", settings.showBuild},
                                        {"console", settings.showConsole},
                                        {"dependencies", settings.showDependencies}}},
                                      {"layout", settings.layout},
                                      {"layout_checksum", LayoutChecksum(settings.layout)}};
        const std::string text = document.dump(2) + '\n';
        if (text.size() > MaxWorkspaceBytes) {
            error = "Serialized workspace settings exceed the 1 MiB limit.";
            return false;
        }
        return WriteWorkspaceTextAtomic(path, text, error);
    }

}
