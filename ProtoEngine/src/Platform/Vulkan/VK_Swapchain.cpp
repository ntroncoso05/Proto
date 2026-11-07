#include "protopch.hpp"
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "Platform/Vulkan/VK_Context.hpp"
#include "Platform/Vulkan/VK_Device.hpp"
#include "Platform/Vulkan/VK_Swapchain.hpp"
#include "Platform/Vulkan/VK_ImageView.hpp"
#include "Platform/Vulkan/VK_Image.hpp"
#include "Core/ServiceLocator.hpp"

#include <GLFW/glfw3.h>

namespace Proto
{

	static uint32_t ChooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
	{
		auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
		if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount))
		{
			minImageCount = surfaceCapabilities.maxImageCount;
		}
		return minImageCount;
	}

	static vk::SurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
	{
		assert(!availableFormats.empty());
		const auto formatIt = std::ranges::find_if(
			availableFormats,
			[](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
	}

	static vk::PresentModeKHR ChooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes)
	{
		assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	vk::Extent2D VulkanSwapChain::ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
	{
		auto& context = ServiceLocator::Get<VulkanContext>();
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
		int width, height;
		glfwGetFramebufferSize(context.GetWindow(), &width, &height);

		return {
			std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
	}

	void VulkanSwapChain::CreateSwapChain()
	{
		auto& context = ServiceLocator::Get<VulkanContext>();
		auto& device = ServiceLocator::Get<VulkanDevice>();

		auto& _surface = context.GetSurface();
		vk::SurfaceCapabilitiesKHR surfaceCapabilities = device.physicalDevice.getSurfaceCapabilitiesKHR(_surface);
		swapChainExtent = ChooseSwapExtent(surfaceCapabilities);
		uint32_t minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);

		std::vector<vk::SurfaceFormatKHR> availableFormats = device.physicalDevice.getSurfaceFormatsKHR(_surface);
		swapChainSurfaceFormat = ChooseSwapSurfaceFormat(availableFormats);

		std::vector<vk::PresentModeKHR> availablePresentModes = device.physicalDevice.getSurfacePresentModesKHR(_surface);
		vk::PresentModeKHR              presentMode = ChooseSwapPresentMode(availablePresentModes);

		vk::SwapchainCreateInfoKHR swapChainCreateInfo{ .surface = _surface,
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

		swapChain = vk::raii::SwapchainKHR(device.logicalDevice, swapChainCreateInfo);
		swapChainImages = swapChain.getImages();
	}

	void VulkanSwapChain::CleanupSwapChain()
	{
		swapChainImageViews.clear();
		swapChain = nullptr;
	}

	void VulkanSwapChain::CreateImageViews()
	{
		auto& image = ServiceLocator::Get<VulkanImage>();
		image.CreateSwapChainImageViews(swapChainImageViews, swapChainImages, swapChainSurfaceFormat.format);
	}

	void VulkanSwapChain::RecreateSwapChain()
	{
		auto& context = ServiceLocator::Get<VulkanContext>();
		auto& device = ServiceLocator::Get<VulkanDevice>();
		auto& image = ServiceLocator::Get<VulkanImage>();

		auto window = context.GetWindow();
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

		device.logicalDevice.waitIdle();

		CleanupSwapChain();
		CreateSwapChain();
		image.CreateSwapChainImageViews(swapChainImageViews, swapChainImages, swapChainSurfaceFormat.format);
		image.CreateColorResources();
		image.CreateDepthResources();
	}
}