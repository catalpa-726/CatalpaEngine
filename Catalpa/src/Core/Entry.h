#pragma once

#ifdef CATALPA_PLATFORM_WINDOW

int main(int argc, char** argv) {
	Catalpa::Log::Init();
	CAT_CORE_INFO("info");
	CAT_CORE_TRACE("trace");
	CAT_CORE_DEBUG("debug");
	CAT_CORE_WARN("warn");
	CAT_CORE_ERROR("error");
	CAT_CORE_CRITICAL("critical");
	auto app = Catalpa::CreateApplication();
	app->Run();
	delete app;
}

#endif