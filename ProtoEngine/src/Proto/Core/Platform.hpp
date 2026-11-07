#pragma once

#include "Platform/Vulkan/VK_Context.hpp"
#define GLFW_INCLUDE_VULKAN        // REQUIRED only for GLFW CreateWindowSurface.
#include <GLFW/glfw3.h>

namespace Proto
{
	class Platform
	{
	public:
		GLFWwindow* window = nullptr;
		bool framebufferResized = false;

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

		std::vector<const char*> getRequiredInstanceExtensions();
		void createSurface(VulkanContext& vulkanContext);
	private:
	};
}