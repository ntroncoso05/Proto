#include "protopch.hpp"
#include "WindowsPlatform.hpp"

#include "Events/ApplicationEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Events/KeyEvent.hpp"

#include "Platform/OpenGL/OpenGLContext.hpp"

#include "Platform/Vulkan/VK_Context.hpp"
//#include "Renderer/Camera.hpp"
//#include "Core/Core.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"

namespace Proto {

	static void framebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto* windowData = reinterpret_cast<WindowsWindow::WindowData*>(glfwGetWindowUserPointer(window));
		if (windowData)
		{
			windowData->Width = width;
			windowData->Height = height;

			WindowResizeEvent event(width, height);
			windowData->EventCallback(event);
		}
	}

	void WindowsWindow::createSurface()
	{
		VulkanContext* context = static_cast<VulkanContext*>(m_Context);
		VkSurfaceKHR _surface;
		if (glfwCreateWindowSurface(*context->GetInstance(), m_Window, nullptr, &_surface) != 0)
		{
			throw std::runtime_error("failed to create window surface!");
		}
		context->SetSurface(vk::raii::SurfaceKHR(context->GetInstance(), _surface));
	}

	static bool s_GLFWInitialized = false;

	static void GLFWErrorCallback(int error, const char* description)
	{
		PROTO_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
	}

	Window* Window::Create(const WindowProps& props)
	{
		return new WindowsWindow(props);
	}

	WindowsWindow::WindowsWindow(const WindowProps& props)
	{
		Init(props);
	}

	WindowsWindow::~WindowsWindow()
	{
		Shutdown();
	}

	std::vector<const char*> getRequiredInstanceExtensions()
	{
		uint32_t glfwExtensionCount = 0;
		auto     glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		return extensions;
	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		PROTO_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

		if (!s_GLFWInitialized)
		{
			// TODO: glfwTerminate on system shutdown
			int success = glfwInit();
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

			PROTO_CORE_ASSERT(success, "Could not intialize GLFW!");
			glfwSetErrorCallback(GLFWErrorCallback);
			s_GLFWInitialized = true;
		}


		m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, m_Data.Title.c_str(), nullptr, nullptr);
		glfwSetWindowUserPointer(m_Window, &m_Data);
		glfwSetFramebufferSizeCallback(m_Window, framebufferResizeCallback);

		m_Context = new VulkanContext(m_Window, getRequiredInstanceExtensions()); //new OpenGLContext(m_Window);
		m_Context->Init();
		createSurface();


		//SetVSync(true);

		// Set GLFW callbacks
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
			{
				WindowResizeEvent event(width, height);
				Application::Get().OnEvent(event);
			});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				WindowCloseEvent event;
				Application::Get().OnEvent(event);
			});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				switch (action)
				{
					case GLFW_PRESS:
					{
						KeyPressEvent event(key, 0);
						Application::Get().OnEvent(event);
						break;
					}
					case GLFW_RELEASE:
					{
						KeyReleasedEvent event(key);
						Application::Get().OnEvent(event);
						break;
					}
					case GLFW_REPEAT:
					{
						KeyPressEvent event(key, 1);
						Application::Get().OnEvent(event);
						break;
					}
				}
			});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode)
			{
				KeyTypedEvent event(keycode);
				Application::Get().OnEvent(event);
			});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
			{
				switch (action)
				{
					case GLFW_PRESS:
					{
						MouseButtonPressedEvent event(button);
						Application::Get().OnEvent(event);
						break;
					}
					case GLFW_RELEASE:
					{
						MouseButtonReleasedEvent event(button);
						Application::Get().OnEvent(event);
						break;
					}
				}
			});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
			{		
				MouseScrolledEvent event((float)xOffset, (float)yOffset);
				Application::Get().OnEvent(event);
			});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
			{
				// Broadcast the raw platform float positions cleanly up to the application event dispatcher loop
				MouseMovedEvent event(static_cast<float>(xPos), static_cast<float>(yPos));
				Application::Get().OnEvent(event);
			});
	}

	void WindowsWindow::Shutdown()
	{
		glfwDestroyWindow(m_Window);
	}

	void WindowsWindow::OnUpdate()
	{
		glfwPollEvents();
		//m_Context->SwapBuffers();
	}

	void WindowsWindow::SetVSync(bool enabled)
	{
		//if (enabled)
		//	glfwSwapInterval(1);
		//else
		//	glfwSwapInterval(0);
		//
		//m_Data.VSync = enabled;
	}

	bool WindowsWindow::IsVSync() const
	{
		return m_Data.VSync;
	}

	void WindowsWindow::SetCursorMode(bool locked)
	{
		if (locked)
		{
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			// This flag disables mouse acceleration for precise, raw 3D looking controls
			if (glfwRawMouseMotionSupported())
				glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
		}
		else
		{
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

}