#include "protopch.hpp"
#include "Renderer.hpp"

namespace Proto {

	//Renderer::SceneData* Renderer::s_SceneData = new Renderer::SceneData;

	// Cache properties locally to survive cross-layer scope jumps safely
	static bool s_SceneSubmitted = false;
	static float s_CachedTimestep = 0.0f;

	void Renderer::Init() { RenderCommand::Init(); }
	void Renderer::OnWindowResize(uint32_t width, uint32_t height) { RenderCommand::SetViewport(0, 0, width, height); }

	void Renderer::BeginScene() { s_SceneSubmitted = false; }
	void Renderer::EndScene() {}

	void Renderer::Submit(float timestep) {
		s_SceneSubmitted = true;
		s_CachedTimestep = timestep;
	}

	void Renderer::EndFrame() {
		if (s_SceneSubmitted) {
			RenderCommand::DrawIndexed(s_CachedTimestep);
		}
	}

}