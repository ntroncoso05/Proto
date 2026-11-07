#pragma once

#include "Core.hpp"
#include "Log.hpp"
#include <filesystem>

#ifdef PROTO_ENABLE_ASSERTS
	// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
	// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
	#define PROTO_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { PROTO##type##ERROR(msg, __VA_ARGS__); PROTO_DEBUGBREAK(); } }
	#define PROTO_INTERNAL_ASSERT_WITH_MSG(type, check, ...) PROTO_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
	#define PROTO_INTERNAL_ASSERT_NO_MSG(type, check) PROTO_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", PROTO_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)
	
	#define PROTO_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
	#define PROTO_INTERNAL_ASSERT_GET_MACRO(...) PROTO_EXPAND_MACRO( PROTO_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, PROTO_INTERNAL_ASSERT_WITH_MSG, PROTO_INTERNAL_ASSERT_NO_MSG) )

// Currently accepts at least the condition and one additional parameter (the message) being optional
	#define PROTO_ASSERT(...) PROTO_EXPAND_MACRO( PROTO_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
	#define PROTO_CORE_ASSERT(...) PROTO_EXPAND_MACRO( PROTO_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
	#define PROTO_ASSERT(...)
	#define PROTO_CORE_ASSERT(...)
#endif