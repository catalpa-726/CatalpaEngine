#pragma once

#include "Core/Application.h"

#ifdef CATALPA_PLATFORM_WINDOW

/**
 * @brief 从命令行启动应用并返回进程退出码。
 *
 * @param[in] argc 命令行参数个数。
 * @param[in] argv 命令行参数字符串数组。
 *
 * @return 交给操作系统的启动或运行退出码。
 */
int main(int argc, char** argv) {
	Catalpa::Log::Init();
	const auto startup = Catalpa::HandleApplicationArguments(argc, argv);
	if (startup.handled)
		return startup.exitCode;
	auto app = Catalpa::CreateApplication();
	app->Run();
	const int exitCode = app->GetExitCode();
	delete app;
	return exitCode;
}

#endif
