#pragma once
#include "Core/Log.hpp"

#ifdef PROTO_PLATFORM_WINDOWS

	extern Proto::Application* Proto::CreateApplication();

	int main(int argc, char** argv)
	{
		Proto::Log::Init();
		PROTO_CORE_WARN("Initialized Log!");
		int a = 5;
		PROTO_INFO("Hello! Var={0}", a);

		auto app = Proto::CreateApplication();
		app->Run();
		delete app;
	}
#elif defined(PROTO_PLATFORM_ANDROID)
    #include <game-activity/native_app_glue/android_native_app_glue.h>
    #include "Platform/Android/AndroidWindow.hpp"

    extern "C" void android_main(struct android_app* state)
    {
        while (!state->window && !state->destroyRequested) {
            int events = 0;
            android_poll_source* source = nullptr;
            if (ALooper_pollOnce(10, nullptr, &events, (void**)&source) >= 0 && source) {
                source->process(state, source);
            }
        }
        if (state->destroyRequested) return;

        Proto::AndroidWindow::SetAndroidApp(state);
        Proto::Log::Init();
        auto app = Proto::CreateApplication();
        app->Run();
        delete app;
    }
#endif