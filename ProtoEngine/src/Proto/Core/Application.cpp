#include "protopch.hpp"
#include "Application.hpp"

//#include "Log.hpp"

#include "Renderer/Renderer.hpp"
#include "Core/ImGuiLayer.hpp" // Ensure this is included
#include <glfw/glfw3.h>
#include "Core/Log.hpp"
#include "Core/Core.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"

#include "Core/ServiceLocator.hpp"
#include "Core/ResourceManager.hpp"
#include "Renderer/ImGuiPipeline.hpp"
#include "Renderer/RendererPipeline.hpp"

namespace Proto {

//#define BIND_EVENT_FN(x) std::bind(&Application::x, this, std::placeholders::_1)

	Application* Application::s_Instance = nullptr;

	Application::Application()
	{
		PROTO_CORE_ASSERT(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = std::unique_ptr<Window>(Window::Create());
		auto func = [this](Event& e) { this->OnEvent(e); };
		m_Window->SetEventCallback(func);
		// 1. Register your active Window platform system into the locator
		ServiceLocator::Register<Window>(m_Window.get());

		//m_Window->SetEventCallback(BIND_EVENT_FN(OnEvent));
		Renderer::Init();

		// Since RenderCommand allocates a static instance of your concrete Vulkan context,
		// we fetch it through the abstract API wrapper and register it with the Service Locator!
		auto& apiContext = static_cast<VulkanRenderer&>(*RendererAPI::GetInstance());
		ServiceLocator::Register<VulkanRenderer>(&apiContext);
		ServiceLocator::Register<RendererAPI>(&apiContext);

		//m_3DRenderer = std::make_unique<SceneRenderer>();
		//ServiceLocator::Register<SceneRenderer>(m_3DRenderer.get());

		// 2. Initialize and allocate your new high-level scene ImGuiPipeline pass manager
		m_ImGuiPipeline = std::make_unique<ImGuiPipeline>();
		m_ImGuiPipeline->Init();

		// Push the input gatekeeper layer as an overlay on top of our layer stack
		PushOverlay(new ImGuiLayer());
	}

	Application::~Application()
	{}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>([this](const WindowCloseEvent& ev)	{ return this->OnWindowClose(ev); });
		dispatcher.Dispatch<WindowResizeEvent>([this](const WindowResizeEvent& ev)	{ return this->OnWindowResize(ev); });
		
		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
		{
			(*--it)->OnEvent(e);
			// If the UI layer (or any overlay) handles the input, break early
			if (e.Handled)
				break;
		}
		
	}

	void Application::Run()
	{
		while (m_Running)
		{
			float time = (float)glfwGetTime();
			float frameTime = time - m_LastFrameTime;
			m_LastFrameTime = time;
			Timestep timestep = std::min(frameTime, 0.1f);

			// This executes glfwPollEvents() cleanly, which systematically maps straight 
			// to your layer stack cascade pipeline.
			m_Window->OnUpdate();
			if (!m_Minimized)
			{
				// Updates layers (like ExampleLayer) which read keyboard state maps sequentially
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(timestep);

				// C. CRITICAL ENGINE PIECE: Explicitly instruct the graphics card to draw the frame!
				Renderer::EndFrame();
			}

			//m_ImGuiLayer->Begin();
			//for (Layer* layer : m_LayerStack)
			//	layer->OnImGuiRender();
			//m_ImGuiLayer->End();

		}
	}

	bool Application::OnWindowClose(const WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	bool Application::OnWindowResize(const WindowResizeEvent& e)
	{
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}
		
		m_Minimized = false;

		// 1. Fetch your active Vulkan context backend pointer smoothly
		auto* renderer = &VulkanRenderer::Get();
		if (renderer && renderer->vulkanContext)
		{
			// 2. ONLY set the flag here! Do not execute swapchain updates yet!
			renderer->vulkanContext->framebufferResized = true;
		}

		// 3. Update the global renderer width/height properties safely

		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

}