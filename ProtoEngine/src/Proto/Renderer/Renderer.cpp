#include "protopch.hpp"
#include "Renderer.hpp"

namespace Proto {

	//Renderer::SceneData* Renderer::s_SceneData = new Renderer::SceneData;

	// Cache properties locally to survive cross-layer scope jumps safely
	static Camera* s_ActiveCamera = nullptr;
	static float s_CachedTimestep = 0.0f;

	void Renderer::Init() { RenderCommand::Init(); }
	void Renderer::OnWindowResize(uint32_t width, uint32_t height) { RenderCommand::SetViewport(0, 0, width, height); }

	void Renderer::BeginScene() {
		s_ActiveCamera = nullptr;
	}

	void Renderer::EndScene() {}

	void Renderer::Submit(Camera& camera, float timestep)
	{
		// 1. Cache the reference mapping safely instead of triggering drawing commands prematurely
		s_ActiveCamera = &camera;
		s_CachedTimestep = timestep;
	}

	void Renderer::EndFrame()
	{
		// 2. Unify the draw pipeline triggers safely inside the explicit rendering window scope
		if (s_ActiveCamera)
		{
			RenderCommand::DrawIndexed(*s_ActiveCamera, s_CachedTimestep);
		}
	}

}