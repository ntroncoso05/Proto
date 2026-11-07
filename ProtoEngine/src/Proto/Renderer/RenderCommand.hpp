#pragma once

#include "RendererAPI.hpp"
#include "Camera.hpp"
namespace Proto {

	class RenderCommand
	{
	public:
		inline static void Init()
		{
			//s_RendererAPI->Init();
			RendererAPI::GetInstance()->Init();
		}

		inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
		{
			RendererAPI::GetInstance()->SetViewport(x, y, width, height);
		}

		inline static void SetClearColor(const glm::vec4& color)
		{
			RendererAPI::GetInstance()->SetClearColor(color);
		}

		inline static void Clear()
		{
			RendererAPI::GetInstance()->Clear();
		}

		inline static void DrawIndexed(Camera& camera, float timestep)
		{
			RendererAPI::GetInstance()->DrawIndexed(camera, timestep);
		}
	private:
		static RendererAPI* s_RendererAPI;
	};

}