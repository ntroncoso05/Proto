#pragma once

#ifdef PROTO_DEBUG
	#define PROTO_ENABLE_ASSERTS
#endif

#ifdef PROTO_ENABLE_ASSERTS
	#ifdef PROTO_PLATFORM_WINDOWS
		#define PROTO_DEBUGBREAK() __debugbreak()
	#else
		#define PROTO_DEBUGBREAK() __builtin_trap()
	#endif
		#define PROTO_ASSERT(x, ...) { if(!(x)) { PROTO_ERROR("Assertion Failed: {0}", __VA_ARGS__); PROTO_DEBUGBREAK(); } }
		#define PROTO_CORE_ASSERT(x, ...) { if(!(x)) { PROTO_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); PROTO_DEBUGBREAK(); } }
#else
	#define PROTO_ASSERT(x, ...)
	#define PROTO_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << x) // 1 shifted by x places

#define PROTO_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

#include <memory>

namespace Proto {

	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

}