#include "Log.hpp"

#ifdef PROTO_PLATFORM_ANDROID
	#include <spdlog/sinks/android_sink.h>
#else
    #include <spdlog/sinks/stdout_color_sinks.h>
    #include <spdlog/sinks/basic_file_sink.h>
#endif

namespace Proto {

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
        std::vector<spdlog::sink_ptr> logSinks;
#ifdef PROTO_PLATFORM_ANDROID
        logSinks.emplace_back(std::make_shared<spdlog::sinks::android_sink_mt>("ProtoEngine"));
#else
        logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Proto.log", true));
#endif

        logSinks.front()->set_pattern("%^[%T] %n: %v%$");
        if (logSinks.size() > 1) {
            logSinks.back()->set_pattern("[%T] [%l] %n: %v");
        }

        s_CoreLogger = std::make_shared<spdlog::logger>("PROTO", begin(logSinks), end(logSinks));
        spdlog::register_logger(s_CoreLogger);
        s_CoreLogger->set_level(spdlog::level::trace);
        s_CoreLogger->flush_on(spdlog::level::trace);

        s_ClientLogger = std::make_shared<spdlog::logger>("APP", begin(logSinks), end(logSinks));
        spdlog::register_logger(s_ClientLogger);
        s_ClientLogger->set_level(spdlog::level::trace);
        s_ClientLogger->flush_on(spdlog::level::trace);
	}

}
