#pragma once

//#include "Core.h"
#include <memory>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

// This ignores all warnings raised inside External headers
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#pragma warning(pop)

namespace Proto {

	class Log
	{
	public:
		static void Init();

		static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClientLogger;
	};

}


/*template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
}*/
template <>
struct fmt::formatter<glm::vec3> {
	// Parses format specs like "{:.2f}"
	constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

	// Formats the vec3 into the output range
	template <typename FormatContext>
	auto format(const glm::vec3& v, FormatContext& ctx) const {
		return fmt::format_to(ctx.out(), "vec3({}, {}, {})", v.x, v.y, v.z);
	}
};

/*template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
	return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
	return os << glm::to_string(quaternion);
}*/

// Core log macros
#define PROTO_CORE_TRACE(...)		::Proto::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define PROTO_CORE_INFO(...)		::Proto::Log::GetCoreLogger()->info(__VA_ARGS__)
#define PROTO_CORE_WARN(...)		::Proto::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define PROTO_CORE_ERROR(...)		::Proto::Log::GetCoreLogger()->error(__VA_ARGS__)
#define PROTO_CORE_CRITICAL(...)	::Proto::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define PROTO_TRACE(...)			::Proto::Log::GetClientLogger()->trace(__VA_ARGS__)
#define PROTO_INFO(...)				::Proto::Log::GetClientLogger()->info(__VA_ARGS__)
#define PROTO_WARN(...)				::Proto::Log::GetClientLogger()->warn(__VA_ARGS__)
#define PROTO_ERROR(...)			::Proto::Log::GetClientLogger()->error(__VA_ARGS__)
#define PROTO_CRITICAL(...)			::Proto::Log::GetClientLogger()->critical(__VA_ARGS__)
