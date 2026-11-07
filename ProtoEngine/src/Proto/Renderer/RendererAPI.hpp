#pragma once

#include <glm/glm.hpp>

#include "VertexArray.hpp"
#include "Camera.hpp"
namespace Proto {

	class RendererAPI
	{
	public:
		enum class API
		{
			None = 0, OpenGL = 1, Vulkan = 2
		};
	public:
		virtual void Init() = 0;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;

		virtual void DrawIndexed(Camera& camera, float timestep/*const std::shared_ptr<VertexArray>& vertexArray*/) = 0;

		inline static API GetAPI() { return s_API; }
	
		// 🟩 ADD THIS SIMPLE INLINE METRIC GETTER
		inline static RendererAPI* GetInstance() {
			// Returns the global static tracking instance allocated at startup
			return s_RendererAPI;
		}

	private:
		static API s_API; //
		static RendererAPI* s_RendererAPI; // Link tracking field matching your allocation source file!
	};

}