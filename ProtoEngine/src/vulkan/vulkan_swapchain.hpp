#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include "core/platform.hpp"
#include "vulkan/vulkan_device.hpp"

namespace Proto
{
	class VulkanImage;
	class VulkanSwapChain
	{
	public:
		vk::raii::SwapchainKHR           swapChain = nullptr;
		std::vector<vk::Image>           swapChainImages;
		vk::SurfaceFormatKHR             swapChainSurfaceFormat;
		vk::Extent2D                     swapChainExtent;
		std::vector<vk::raii::ImageView> swapChainImageViews;

		Platform& platform;
		VulkanDevice& vulkanDevice;

		VulkanSwapChain(Platform& platform, VulkanDevice& device);

		/**
		 * @brief Destructor.
		*/
		~VulkanSwapChain();

		void createSwapChain();
		void cleanupSwapChain();
		void recreateSwapChain(GLFWwindow* window, VulkanImage* image);
		void createImageViews();
		vk::raii::ImageView createImageView(vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels);
		vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities);
	};
}