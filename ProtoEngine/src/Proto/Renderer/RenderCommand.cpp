#include "protopch.hpp"
//#include "RenderCommand.hpp"

//#include "Platform/OpenGL/OpenGLRendererAPI.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"

namespace Proto {
	//auto windows = static_cast<WindowsWindow&>(Application::Get().GetWindow());
	//VulkanContext* context = static_cast<VulkanContext*>(windows.GetGraphicsContext());
	//RendererAPI* RenderCommand::s_RendererAPI = new VulkanRenderer; //new OpenGLRendererAPI;
	RendererAPI* RendererAPI::s_RendererAPI = new VulkanRenderer;
}