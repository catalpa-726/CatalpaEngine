#include "catpch.h"
#include "Log.h"

#include "spdlog/sinks/stdout_color_sinks.h"

namespace Catalpa {

	std::shared_ptr<spdlog::logger> Log::m_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::m_AppLogger;

	void Log::Init() {
		SetConsoleOutputCP(CP_UTF8);

		spdlog::set_pattern("%^[%T] %n: %v%$");
		m_CoreLogger = spdlog::stdout_color_mt("Catalpa");
		m_CoreLogger->set_level(spdlog::level::trace);

		m_AppLogger = spdlog::stdout_color_mt("CatalpaBox");
		m_AppLogger->set_level(spdlog::level::trace);
	}

}