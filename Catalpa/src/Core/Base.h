#pragma once

#include <memory>

#ifdef CATALPA_PLATFORM_WINDOW
	#ifdef CATALPA_BUILD_DLL
		#define CATALPA_API __declspec(dllexport)
	#else
		#define CATALPA_API __declspec(dllimport)
	#endif
#endif

#ifdef CATALPA_ENABLE_ASSERTS
	#define CAT_ASSERT(x,  ...) { if(!(x)) { CAT_CRITICAL("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
	#define CAT_CORE_ASSERT(x, ...) { if(!(x)) { CAT_CORE_CRITICAL("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); }}
#else
	#define HZ_ASSERT(x, ...)
	#define HZ_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << x)

#define CAT_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }


