#include "vulkan_swapchain.hpp"
#include "vulkan_image_view.hpp"
#include "vulkan_image.hpp"

#include <GLFW/glfw3.h>

namespace Proto
{
	VulkanSwapChain::VulkanSwapChain(Platform& platform, VulkanDevice& device) : platform(platform), vulkanDevice(device)
	{}

	// Destructor
	VulkanSwapChain::~VulkanSwapChain() {
		// RAII will handle destruction
	}

	static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
	{
		assert(!availableFormats.empty());
		const auto formatIt = std::ranges::find_if(
			availableFormats,
			[](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
	}

	static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
	{
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D VulkanSwapChain::chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(platform.window, &width, &height);

		return {
			std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
	}

	void VulkanSwapChain::createSwapChain()
	{
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = vulkanDevice.physicalDevice.getSurfaceCapabilitiesKHR(*platform.surface);
		swapChainExtent = chooseSwapExtent(surfaceCapabilities);
		uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

		std::vector<vk::SurfaceFormatKHR> availableFormats = vulkanDevice.physicalDevice.getSurfaceFormatsKHR(*platform.surface);
		swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = vulkanDevice.physicalDevice.getSurfacePresentModesKHR(*platform.surface);
		vk::PresentModeKHR              presentMode = chooseSwapPresentMode(availablePresentModes);

		vk::SwapchainCreateInfoKHR swapChainCreateInfo{ .surface = *platform.surface,
													   .minImageCount = minImageCount,
													   .imageFormat = swapChainSurfaceFormat.format,
													   .imageColorSpace = swapChainSurfaceFormat.colorSpace,
													   .imageExtent = swapChainExtent,
													   .imageArrayLayers = 1,
													   .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
													   .imageSharingMode = vk::SharingMode::eExclusive,
													   .preTransform = surfaceCapabilities.currentTransform,
													   .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
													   .presentMode = presentMode,
													   .clipped = true };

		swapChain = vk::raii::SwapchainKHR(vulkanDevice.logicalDevice, swapChainCreateInfo);
		swapChainImages = swapChain.getImages();
	}

	void VulkanSwapChain::cleanupSwapChain()
	{
		swapChainImageViews.clear();
		swapChain = nullptr;
	}

	vk::raii::ImageView VulkanSwapChain::createImageView(vk::Image const& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
	{
		return VulkanImageView::createImageView(image, format, vulkanDevice, aspectFlags, mipLevels);
	}

	void VulkanSwapChain::createImageViews()
	{
		VulkanImageView::createImageViews(swapChainImageViews, swapChainImages, swapChainSurfaceFormat, vulkanDevice);
	}

	void VulkanSwapChain::recreateSwapChain(GLFWwindow* window, VulkanImage* image)
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		while ((width == 0 || height == 0) && !glfwWindowShouldClose(window))
		{
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}
		if (glfwWindowShouldClose(window))
		{
			return;
		}

		vulkanDevice.logicalDevice.waitIdle();

		cleanupSwapChain();
		createSwapChain();
		VulkanImageView::createImageViews(swapChainImageViews, swapChainImages, swapChainSurfaceFormat, vulkanDevice);
		image->createColorResources();
		image->createDepthResources();
	}
}