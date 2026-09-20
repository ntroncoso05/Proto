#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

//#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_VULKAN        // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

namespace Proto
{
	class Platform
	{
	public:
		GLFWwindow* window = nullptr;
		vk::raii::Context                context;
		vk::raii::Instance               instance = nullptr;
		vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
		vk::raii::SurfaceKHR             surface = nullptr;
		bool framebufferResized = false;
		std::vector<const char*> requiredExtensions;

		/**
		 * @brief Default constructor.
		*/
		Platform() = default;

		/**
		 * @brief Virtual destructor for proper cleanup.
		*/
		virtual ~Platform() = default;

		void initWindow();
		void CleanUp();

		void createInstance();
		void setupDebugMessenger();
		void createSurface();
	private:
	};
}