#pragma once

#include "RenderCommand.hpp"

namespace Proto {

	class Renderer
	{
	public:
		static void Init();
		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene();
		static void EndScene(); // Updates structure layout matrices

		// Restore tracking interface
		static void Submit(float timestep);

		// NEW: Absolute frame termination hook
		static void EndFrame();

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
	};

}