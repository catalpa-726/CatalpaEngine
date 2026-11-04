#pragma once

#ifdef CATALPA_PLATFORM_WINDOW

int main(int argc, char** argv) {
	Catalpa::Log::Init();
	const auto startup = Catalpa::HandleApplicationArguments(argc, argv);
	if (startup.handled) return startup.exitCode;
	auto app = Catalpa::CreateApplication();
	app->Run();
	const int exitCode = app->GetExitCode();
	delete app;
	return exitCode;
}

#endif
