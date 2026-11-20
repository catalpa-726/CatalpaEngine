#include "catpch.h"

#include "WorkspaceSettings.h"

#include "../Platform/WorkspaceFiles.h"

#include <nlohmann/json.hpp>

namespace Catalpa::Editor {

	namespace {

		constexpr size_t MaxWorkspaceBytes = 1024 * 1024;

		/* 读取、范围校验和保存共用同一组字段，避免混用距离、角度与缩放比例。 */
		struct ViewportNumber {
			const char* key;
			float ViewportSettings::* member;
			float minimum, maximum;
		};
		constexpr ViewportNumber ViewportNumbers[]{
		    {"camera_speed", &ViewportSettings::cameraSpeed, ViewportSettings::MinCameraSpeed,
		        ViewportSettings::MaxCameraSpeed},
		    {"translation_step", &ViewportSettings::translationStep, ViewportSettings::MinTranslationStep,
		        ViewportSettings::MaxTranslationStep},
		    {"rotation_step", &ViewportSettings::rotationStep, ViewportSettings::MinRotationStep,
		        ViewportSettings::MaxRotationStep},
		    {"scale_step_percent", &ViewportSettings::scaleStepPercent, ViewportSettings::MinScaleStepPercent,
		        ViewportSettings::MaxScaleStepPercent}};

		/**
		 * @brief 为序列化的布局字节计算固定格式的校验值。
		 * 对解码后的布局原始字节做 64 位 FNV-1a，固定输出 16 位小写十六进制。
		 * 只检测应用生成的 opaque layout 是否意外变化，不是安全签名或第二份 ini 解析器。
		 *
		 * @param[in] layout 解码后的 ImGui 布局字节；逐字节参与校验。
		 *
		 * @return 以 16 位小写十六进制表示的 FNV-1a 校验值。
		 */
		std::string LayoutChecksum(std::string_view layout) {
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

		/**
		 * @brief 扫描 JSON 文本，检查括号、引号与嵌套深度。
		 * 限制解析结构深度；跳过字符串内的括号，非法 JSON 仍由解析器报告。
		 *
		 * @param[in] text 待检查嵌套深度的原始设置文件 JSON 文本。
		 *
		 * @return 引号与括号闭合且未超过嵌套限制时为 true；完整的 JSON 语法仍由解析器检查。
		 */
		bool HasBoundedJsonDepth(std::string_view text) {
			size_t depth = 0;
			bool quoted = false, escaped = false;
			for (const char value : text) {
				if (value == '\0')
					return false;
				if (quoted) {
					if (escaped)
						escaped = false;
					else if (value == '\\')
						escaped = true;
					else if (value == '"')
						quoted = false;
				} else if (value == '"')
					quoted = true;
				else if (value == '{' || value == '[') {
					if (++depth > 8)
						return false;
				} else if (value == '}' || value == ']') {
					if (depth == 0)
						return false;
					--depth;
				}
			}
			return depth == 0 && !quoted;
		}

		/**
		 * @brief 检查 UTF-8 字节序列的合法性。
		 * dump 使用严格 UTF-8；预先拒绝非法编码，避免正常错误路径依赖异常或替换字符。
		 *
		 * @param[in] text 待逐字节验证 UTF-8 编码的文本。
		 *
		 * @return 文本由合法的 UTF-8 字节序列构成时为 true。
		 */
		bool IsValidUtf8(std::string_view text) {
			size_t index = 0;
			while (index < text.size()) {
				const auto first = static_cast<unsigned char>(text[index++]);
				if (first < 0x80)
					continue;
				uint32_t codepoint = 0, minimum = 0;
				size_t continuation = 0;
				if (first >= 0xc2 && first <= 0xdf) {
					codepoint = first & 0x1f;
					minimum = 0x80;
					continuation = 1;
				} else if (first >= 0xe0 && first <= 0xef) {
					codepoint = first & 0x0f;
					minimum = 0x800;
					continuation = 2;
				} else if (first >= 0xf0 && first <= 0xf4) {
					codepoint = first & 0x07;
					minimum = 0x10000;
					continuation = 3;
				} else
					return false;
				if (continuation > text.size() - index)
					return false;
				for (size_t i = 0; i < continuation; ++i) {
					const auto value = static_cast<unsigned char>(text[index++]);
					if ((value & 0xc0) != 0x80)
						return false;
					codepoint = (codepoint << 6) | (value & 0x3f);
				}
				if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
					return false;
			}
			return true;
		}

		/**
		 * @brief 校验语言、字体大小、布局及其他工作区设置。
		 * 语言键使用固定白名单，布局和作者命名保持独立。
		 *
		 * @param[in] settings 待校验的语言标识、字号、工作区布局等设置。
		 * @param[out] error 失败时接收原因说明。
		 *
		 * @return 全部工作区字段满足保存约束时为 true，校验失败时为 false。
		 */
		bool ValidateSettings(const WorkspaceSettings& settings, std::string& error) {
			for (const auto& field : ViewportNumbers) {
				const float value = settings.viewport.*field.member;
				if (!(value >= field.minimum && value <= field.maximum)) {
					error = std::string("Workspace viewport.") + field.key + " is outside the supported range.";
					return false;
				}
			}
			if (settings.language != "en-US" && settings.language != "zh-CN")
				error = "Unsupported workspace language.";
			else if (settings.scriptEditor != "auto" && settings.scriptEditor != "visual-studio" &&
			    settings.scriptEditor != "vscode" && (settings.scriptEditor.find('\0') != std::string::npos ||
			    !IsValidUtf8(settings.scriptEditor) || !std::filesystem::u8path(settings.scriptEditor).is_absolute()))
				error = "Invalid external script editor path.";
			else if (settings.editorFontSize < WorkspaceSettings::MinFontSize ||
			    settings.editorFontSize > WorkspaceSettings::MaxFontSize)
				error = "Workspace editor_font_size must be between 12 and 36 pixels.";
			else if (settings.layout.size() > MaxWorkspaceBytes)
				error = "Workspace layout exceeds the 1 MiB limit.";
			else if (settings.layout.find('\0') != std::string::npos)
				error = "Workspace layout contains an embedded null byte.";
			else if (!IsValidUtf8(settings.layout))
				error = "Workspace layout is not valid UTF-8.";
			else {
				if (settings.recentProjects.size() > WorkspaceSettings::MaxRecentProjects) {
					error = "Too many recent workspace projects.";
					return false;
				}
				for (const auto& name : settings.recentProjects) {
					if (name.empty() || name.find('\0') != name.npos || !IsValidUtf8(name)) {
						error = "Recent project path is not valid UTF-8.";
						return false;
					}
					const auto path = std::filesystem::u8path(name);
					if (!path.is_absolute() || path.lexically_normal() != path) {
						error = "Recent project must use a normalized absolute path.";
						return false;
					}
				}
				return true;
			}
			return false;
		}

	}

	WorkspaceLoadResult LoadWorkspaceSettings(
	    const std::filesystem::path& path, WorkspaceSettings& settings, std::string& error) {
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
		if (!ReadWorkspaceText(path, text, MaxWorkspaceBytes, error))
			return WorkspaceLoadResult::Failed;
		if (!HasBoundedJsonDepth(text)) {
			error = "Workspace JSON has invalid structure, a null byte, or excessive nesting.";
			return WorkspaceLoadResult::Failed;
		}

		/* 先确认配置版本和字段类型，再据此解码对应版本的工作区布局。 */
		const auto document = nlohmann::json::parse(text, nullptr, false);
		if (!document.is_object()) {
			error = "Workspace settings must be a JSON object.";
			return WorkspaceLoadResult::Failed;
		}
		const auto version = document.find("schema_version");
		if (version == document.end() || !version->is_number_integer() ||
		    (*version != 1 && *version != 2 && *version != 3 && *version != 4)) {
			error = "Unsupported workspace schema_version; expected integer from 1 to 4.";
			return WorkspaceLoadResult::Failed;
		}
		/* v2 起保存字号，v3 起保存完整的视口偏好；旧配置沿用相应字段的默认值。 */
		const bool hasFontSize = *version != 1;
		const bool hasViewport = *version >= 3;
		const bool hasScriptEditor = *version >= 4;
		const auto recent = document.find("recent_projects");
		if (document.size() != 6u + hasFontSize + hasViewport + hasScriptEditor + (recent != document.end())) {
			error = "Workspace field count does not match its schema_version.";
			return WorkspaceLoadResult::Failed;
		}
		const auto language = document.find("language");
		const auto vsync = document.find("vsync");
		const auto panels = document.find("panels");
		const auto layout = document.find("layout");
		const auto checksum = document.find("layout_checksum");
		if (language == document.end() || !language->is_string() || vsync == document.end() || !vsync->is_boolean() ||
		    layout == document.end() || !layout->is_string() || checksum == document.end() || !checksum->is_string() ||
		    panels == document.end() || !panels->is_object() ||
		    (panels->size() != 4 && panels->size() != 8 && panels->size() != 9 && panels->size() != 11)) {
			error =
			    "Workspace language/layout/layout_checksum must be strings, vsync a boolean, and panels a supported panel object.";
			return WorkspaceLoadResult::Failed;
		}

		WorkspaceSettings parsed;
		if (hasScriptEditor) {
			const auto editor = document.find("script_editor");
			if (editor == document.end() || !editor->is_string()) {
				error = "Workspace script_editor must be a string.";
				return WorkspaceLoadResult::Failed;
			}
			parsed.scriptEditor = editor->get<std::string>();
		}
		if (hasViewport) {
			const auto viewport = document.find("viewport");
			if (viewport == document.end() || !viewport->is_object() ||
			    viewport->size() != std::size(ViewportNumbers) + 1) {
				error = "Workspace viewport must contain the navigation speed and all transform snap settings.";
				return WorkspaceLoadResult::Failed;
			}
			const auto snap = viewport->find("snap_enabled");
			if (snap == viewport->end() || !snap->is_boolean()) {
				error = "Workspace viewport.snap_enabled must be a boolean.";
				return WorkspaceLoadResult::Failed;
			}
			parsed.viewport.snapEnabled = snap->get<bool>();
			for (const auto& field : ViewportNumbers) {
				const auto entry = viewport->find(field.key);
				if (entry == viewport->end() || !entry->is_number() ||
				    std::abs(entry->get<double>()) > std::numeric_limits<float>::max()) {
					error = std::string("Workspace viewport.") + field.key + " must be a representable number.";
					return WorkspaceLoadResult::Failed;
				}
				parsed.viewport.*field.member = entry->get<float>();
			}
		}
		/* 老布局里没有保存的开关沿用当时的默认值；新安装和显式重置使用新默认布局。 */
		parsed.showBuild = true;
		parsed.showConsole = false;
		parsed.showHistory = true;
		if (recent != document.end()) {
			if (!recent->is_array() || recent->size() > WorkspaceSettings::MaxRecentProjects) {
				error = "Invalid recent project list.";
				return WorkspaceLoadResult::Failed;
			}
			for (const auto& entry : *recent) {
				const auto* name = entry.get_ptr<const std::string*>();
				if (!name) {
					error = "Recent project paths must be strings.";
					return WorkspaceLoadResult::Failed;
				}
				parsed.recentProjects.push_back(*name);
			}
		}
		if (hasFontSize) {
			const auto size = document.find("editor_font_size");
			if (size == document.end() || !size->is_number_integer() || *size < WorkspaceSettings::MinFontSize ||
			    *size > WorkspaceSettings::MaxFontSize) {
				error = "Workspace editor_font_size must be an integer between 12 and 36 pixels.";
				return WorkspaceLoadResult::Failed;
			}
			parsed.editorFontSize = size->get<int>();
		}
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
		    !ValidateSettings(parsed, error))
			return WorkspaceLoadResult::Failed;
		/* 面板字段随版本递增；旧配置未记录的面板使用各自的默认值。 */
		if (panels->size() >= 8 &&
		    (!readPanel("content", parsed.showContent) || !readPanel("build", parsed.showBuild) ||
		        !readPanel("console", parsed.showConsole) || !readPanel("dependencies", parsed.showDependencies)))
			return WorkspaceLoadResult::Failed;

		if (panels->size() >= 9 && !readPanel("history", parsed.showHistory))
			return WorkspaceLoadResult::Failed;
		if (panels->size() == 11 &&
		    (!readPanel("audio_mixer", parsed.showAudioMixer) || !readPanel("network", parsed.showNetwork)))
			return WorkspaceLoadResult::Failed;

		const auto& storedChecksum = *checksum->get_ptr<const std::string*>();
		/* 只有布局字节与保存的校验值一致，才能用候选配置替换当前设置。 */
		if (storedChecksum.size() != 16 || storedChecksum != LayoutChecksum(parsed.layout)) {
			error = "Workspace layout checksum mismatch; keep the application-generated layout and checksum unchanged.";
			return WorkspaceLoadResult::Failed;
		}

		settings = std::move(parsed);
		return WorkspaceLoadResult::Loaded;
	}

	bool SaveWorkspaceSettings(
	    const std::filesystem::path& path, const WorkspaceSettings& settings, std::string& error) {
		error.clear();
		if (!ValidateSettings(settings, error))
			return false;
		nlohmann::json viewport{{"snap_enabled", settings.viewport.snapEnabled}};
		for (const auto& field : ViewportNumbers)
			viewport[field.key] = settings.viewport.*field.member;
		const nlohmann::json document{{"schema_version", 4}, {"language", settings.language},
		    {"script_editor", settings.scriptEditor},
		    {"editor_font_size", settings.editorFontSize}, {"viewport", std::move(viewport)}, {"vsync", settings.vsync},
		    {"recent_projects", settings.recentProjects},
		    {"panels",
		        {{"scene", settings.showScene}, {"assets", settings.showAssets}, {"history", settings.showHistory},
		            {"inspector", settings.showInspector}, {"settings", settings.showSettings},
		            {"content", settings.showContent}, {"build", settings.showBuild}, {"console", settings.showConsole},
		            {"dependencies", settings.showDependencies}, {"audio_mixer", settings.showAudioMixer},
		            {"network", settings.showNetwork}}},
		    {"layout", settings.layout}, {"layout_checksum", LayoutChecksum(settings.layout)}};
		const std::string text = document.dump(2) + '\n';
		if (text.size() > MaxWorkspaceBytes) {
			error = "Serialized workspace settings exceed the 1 MiB limit.";
			return false;
		}
		return WriteWorkspaceTextAtomic(path, text, error);
	}

}
