#pragma once

#include <filesystem>
#include <string>

namespace Catalpa::Editor {

	/* 编辑器工作区配置；布局文本与场景/资产数据分开保存，内部面板 ID 保持稳定。 */
	struct WorkspaceSettings {
		static constexpr int DefaultFontSize = 24;
		static constexpr int MinFontSize = 12;
		static constexpr int MaxFontSize = 36;

		int editorFontSize = DefaultFontSize;
		bool vsync = true;
		bool showScene = true;
		bool showHistory = true;
		bool showAssets = true;
		bool showInspector = true;
		bool showSettings = false;
		bool showContent = true;
		bool showBuild = true;
		bool showConsole = false;
		bool showDependencies = false;
		bool showAudioMixer = false;
		bool showNetwork = false;
		std::string language = "en-US";
		/*
		 * 仅保存应用生成的 opaque ImGui 布局；人工编辑 JSON 时不要改 layout 或 layout_checksum。
		 * 校验值用于检测意外变化，不是安全签名，也不验证 ImGui ini 的语义。
		 */
		std::string layout;
	};

	enum class WorkspaceLoadResult { Missing, Loaded, Failed };

	/**
	 * @brief 加载并校验工作区配置，成功后一次替换现有设置。
	 * 只读加载；所有字段通过校验后才替换 settings，缺失或失败均保留调用方状态。
	 *
	 * @param[in] path 待读取的工作区设置文件路径。
	 * @param[out] settings 完整解析并校验成功后接收新设置；文件缺失或无效时保留原值。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return Loaded 表示设置已替换，Missing 表示文件不存在，Failed 表示读取或校验失败。
	 */
	[[nodiscard]] WorkspaceLoadResult LoadWorkspaceSettings(
	    const std::filesystem::path& path, WorkspaceSettings& settings, std::string& error);

	/**
	 * @brief 校验工作区设置并原子保存完整配置。
	 * 校验并保存完整工作区；平台层负责临时文件、备份与替换，失败不截断已有配置。
	 *
	 * @param[in] path 要原子替换的工作区设置文件路径。
	 * @param[in] settings 要校验并序列化的完整工作区设置。
	 * @param[out] error 失败时接收原因说明。
	 *
	 * @return 全部内容通过校验并写入配置文件时为 true。
	 */
	[[nodiscard]] bool SaveWorkspaceSettings(
	    const std::filesystem::path& path, const WorkspaceSettings& settings, std::string& error);

}
