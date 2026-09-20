#pragma once

#include <vulkan/vulkan_raii.hpp>

#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"

namespace Proto
{
	class VulkanCommandBuffer;
	class VulkanPipeline
	{
	public:
		VulkanDevice* vulkanDevice = nullptr;
		VulkanSwapChain* vulkanSwapChain = nullptr;
		VulkanCommandBuffer* vulkanCommandBuffer = nullptr;

		vk::raii::PipelineLayout      pipelineLayout = nullptr;
		vk::raii::Pipeline            graphicsPipeline = nullptr;

		VulkanPipeline(VulkanDevice* device, VulkanSwapChain* swapChain);

		/**
		 * @brief Destructor.
		*/
		~VulkanPipeline();

		void createGraphicsPipeline();
		vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
	};
}