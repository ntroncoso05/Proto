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

#endif