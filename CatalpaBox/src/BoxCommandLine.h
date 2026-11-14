#pragma once
#include "Core/Application.h"
#include "catpch.h"
namespace Catalpa {
	/* 宿主只保存启动选择和项目路径，不承载工具算法、资产格式或测试断言。 */
	struct BoxLaunchOptions {
		bool reversedZ = false;
		bool errorMaterial = false;
		bool testEditorThumbnails = false;
		bool testEditorGrove = false;
		std::filesystem::path testLanguageDirectory;
		bool testEditorDocking = false;
		bool testEditorPlatformWindows = false;
		std::filesystem::path project, package;
	};
	class BoxCommandLine final {
	public:
		/**
		 * @brief 解析宿主启动参数并分派到工具、验收或编辑器启动路径。
		 *
		 * @param[in] arguments 要解析的命令行参数，不含可执行文件名。
		 * @param[out] options 接收由命令行指定的项目、渲染和验收开关；解析前重置为默认值。
		 *
		 * @return 启动分派结果。已处理的工具或测试命令携带退出码，仍需创建应用时返回未处理状态。
		 */
		static ApplicationStartupResult Dispatch(std::span<const std::string> arguments, BoxLaunchOptions&);
	};
}
