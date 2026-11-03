#pragma once

#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

#include "Core/Base.h"

namespace Catalpa {

	class CATALPA_API Log {
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger> GetCoreLogger() { return m_CoreLogger; }
		inline static std::shared_ptr<spdlog::logger> GetAppLogger() { return m_AppLogger; }

	private:
		static std::shared_ptr<spdlog::logger> m_CoreLogger;
		static std::shared_ptr<spdlog::logger> m_AppLogger;
	};

}

// 引擎日志入口。
#define CAT_CORE_INFO(...)	   ::Catalpa::Log::GetCoreLogger()->info(__VA_ARGS__)
#define CAT_CORE_TRACE(...)    ::Catalpa::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define CAT_CORE_DEBUG(...)    ::Catalpa::Log::GetCoreLogger()->debug(__VA_ARGS__)
#define CAT_CORE_WARN(...)	   ::Catalpa::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define CAT_CORE_ERROR(...)	   ::Catalpa::Log::GetCoreLogger()->error(__VA_ARGS__)
#define CAT_CORE_CRITICAL(...) ::Catalpa::Log::GetCoreLogger()->critical(__VA_ARGS__)

// 应用日志入口。
#define CAT_INFO(...)		   ::Catalpa::Log::GetAppLogger()->info(__VA_ARGS__)
#define CAT_TRACE(...)		   ::Catalpa::Log::GetAppLogger()->trace(__VA_ARGS__)
#define CAT_DEBUG(...)		   ::Catalpa::Log::GetAppLogger()->debug(__VA_ARGS__)
#define CAT_WARN(...)		   ::Catalpa::Log::GetAppLogger()->warn(__VA_ARGS__)
#define CAT_ERROR(...)		   ::Catalpa::Log::GetAppLogger()->error(__VA_ARGS__)
#define CAT_CRITICAL(...)	   ::Catalpa::Log::GetAppLogger()->critical(__VA_ARGS__)