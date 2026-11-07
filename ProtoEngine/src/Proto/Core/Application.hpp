#pragma once

#include "Core.hpp"

#include "Window.hpp"
#include "LayerStack.hpp"
#include "Events/Event.hpp"
#include "Events/ApplicationEvent.hpp"

#include "Core/Timestep.hpp"

#include "ResourceManager.hpp"

namespace Proto {
	class ImGuiPipeline;
	class SceneRenderer;

	class Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		inline Window& GetWindow() { return *m_Window; }

		inline static Application& Get() { return *s_Instance; }

		inline ResourceManager& GetResourceManager() { return m_ResourceManager; }
	private:
		bool OnWindowClose(const WindowCloseEvent& e);
		bool OnWindowResize(const WindowResizeEvent& e);
	private:
		std::unique_ptr<Window> m_Window;
		//ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		bool m_Minimized = false;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;
		std::unique_ptr<ImGuiPipeline> m_ImGuiPipeline;
		std::unique_ptr<SceneRenderer> m_3DRenderer;
	private:
		static Application* s_Instance;
	protected:
		ResourceManager m_ResourceManager; // Global Asset Engine Hub
	};

	// To be defined in CLIENT
	Application* CreateApplication();

}