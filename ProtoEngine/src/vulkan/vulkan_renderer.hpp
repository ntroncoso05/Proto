#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "core/platform.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_swapchain.hpp"
#include "vulkan/vulkan_pipeline.hpp"
#include "vulkan/vulkan_command_buffer.hpp"
#include "vulkan/vulkan_image.hpp"

#include <algorithm>
#include <array>
#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

#include "core/resource_types.hpp"
#include "core/resource_manager.hpp"

namespace Proto
{
	class VulkanRenderer
	{
	public:
		Platform* platform = new Platform();
		VulkanDevice* vulkanDevice = nullptr;
		VulkanSwapChain* vulkanSwapChain = nullptr;
		VulkanPipeline* vulkanPipeline = nullptr;
		VulkanCommandBuffer* vulkanCommandBuffer = nullptr;
		VulkanImage* vulkanImage = nullptr;

		bool framebufferResized = false;

		// Create resource manager
		ResourceManager resourceManager;

		// Load resources
		Texture* texture;
		Mesh* mesh;

		VulkanRenderer();

		/**
		 * @brief Destructor.
		*/
		~VulkanRenderer();

		void initVulkan();

		void createSyncObjects();
		void drawFrame();
	private:
		std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
		std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
		std::vector<vk::raii::Fence>     inFlightFences;
	};
}