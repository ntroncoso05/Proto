#include "vulkan_image_view.hpp"
#
namespace Proto
{
	vk::raii::ImageView generateImageView(vk::Image const& image, vk::Format format, VulkanDevice& device, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
	{
		vk::ImageViewCreateInfo viewInfo{
			.image = image,
			.viewType = vk::ImageViewType::e2D,
			.format = format,
			.subresourceRange = {.aspectMask = aspectFlags, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 1} };
		return vk::raii::ImageView(device.logicalDevice, viewInfo);
	}

	void VulkanImageView::createImageViews(std::vector<vk::raii::ImageView>& swapChainImageViews, std::vector<vk::Image> swapChainImages, vk::SurfaceFormatKHR swapChainSurfaceFormat, VulkanDevice& device)
	{
		assert(swapChainImageViews.empty());

		swapChainImageViews.reserve(swapChainImages.size());
		for (auto& image : swapChainImages)
		{
			swapChainImageViews.emplace_back(generateImageView(image, swapChainSurfaceFormat.format, device, vk::ImageAspectFlagBits::eColor, 1));
		}
	}

	vk::raii::ImageView VulkanImageView::createImageView(vk::Image const& image, vk::Format format, VulkanDevice& device, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
	{
		return generateImageView(image, format, device, aspectFlags, mipLevels);
	}
}