/*#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include <assert.h>
#include <vector>

#include "VK_Device.hpp"

namespace Proto
{
	class VulkanImageView
	{
	public:
		static void createImageViews(std::vector<vk::raii::ImageView>& swapChainImageViews, std::vector<vk::Image> swapChainImages, vk::SurfaceFormatKHR swapChainSurfaceFormat, VulkanDevice& device);
		static vk::raii::ImageView createImageView(vk::Image const& image, vk::Format format, VulkanDevice& device, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels);
	};
}*/