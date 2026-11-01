#pragma once

#ifdef CATALPA_PLATFORM_WINDOW

int main(int argc, char** argv) {
	Catalpa::Log::Init();
	auto app = Catalpa::CreateApplication();
	app->Run();
	delete app;
}

#endif