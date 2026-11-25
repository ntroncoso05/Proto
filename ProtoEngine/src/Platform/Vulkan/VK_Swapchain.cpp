#include "protopch.hpp"
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "Platform/Vulkan/VK_Context.hpp"
#include "Platform/Vulkan/VK_Device.hpp"
#include "Platform/Vulkan/VK_Swapchain.hpp"
#include "Platform/Vulkan/VK_Image.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include "Core/Application.hpp"
#include "Core/ServiceLocator.hpp"
#ifndef PROTO_PLATFORM_ANDROID
	#include <GLFW/glfw3.h>
#endif

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
#ifdef PROTO_PLATFORM_ANDROID
		return vk::PresentModeKHR::eFifo;
#endif
		return std::ranges::any_of(availablePresentModes,
			[](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
			vk::PresentModeKHR::eMailbox :
			vk::PresentModeKHR::eFifo;
	}

	void VulkanSwapChain::ReallocateDepthResources(uint32_t width, uint32_t height)
	{
		auto& renderer = VulkanRenderer::Get();
		auto* imgUtil = renderer.GetVulkanImageSubsystem();

		// Clear out old frames layouts safely
		depthImageView = nullptr;
		depthImageMemory = nullptr;
		depthImage = nullptr;

		vk::Format depthFormat = imgUtil->FindDepthFormat();

		auto [img, mem] = imgUtil->CreateImage(
			width, height, 1, vk::SampleCountFlagBits::e1, depthFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eDepthStencilAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal
		);

		depthImage = std::move(img);
		depthImageMemory = std::move(mem);
		depthImageView = imgUtil->CreateImageView(*depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
	}


	vk::Extent2D VulkanSwapChain::ChooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
	{
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
		{
			return capabilities.currentExtent;
		}
#ifndef PROTO_PLATFORM_ANDROID
		auto& context = ServiceLocator::Get<VulkanContext>();
		int width, height;
		glfwGetFramebufferSize((GLFWwindow*)context.GetWindow(), &width, &height);
#else
		int width = static_cast<int>(Application::Get().GetWindow().GetWidth());
		int height = static_cast<int>(Application::Get().GetWindow().GetHeight());
#endif
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

		vk::SurfaceTransformFlagBitsKHR preTransform =
			(surfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
			? vk::SurfaceTransformFlagBitsKHR::eIdentity
			: surfaceCapabilities.currentTransform;

		vk::SwapchainCreateInfoKHR swapChainCreateInfo{ .surface = _surface,
													   .minImageCount = minImageCount,
													   .imageFormat = swapChainSurfaceFormat.format,
													   .imageColorSpace = swapChainSurfaceFormat.colorSpace,
													   .imageExtent = swapChainExtent,
													   .imageArrayLayers = 1,
													   .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
													   .imageSharingMode = vk::SharingMode::eExclusive,
													   .preTransform = preTransform, //.preTransform = surfaceCapabilities.currentTransform,
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
		auto& device = ServiceLocator::Get<VulkanDevice>();
		auto& image = ServiceLocator::Get<VulkanImage>();

#ifndef PROTO_PLATFORM_ANDROID
		auto& context = ServiceLocator::Get<VulkanContext>();
		auto window = (GLFWwindow*)context.GetWindow();
		int width = 0, height = 0;
		glfwGetFramebufferSize(window, &width, &height);
		while ((width == 0 || height == 0) && !glfwWindowShouldClose(window))
		{
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}
		if (glfwWindowShouldClose(window)) return;
#else
		if (Application::Get().GetWindow().GetWidth() == 0 || Application::Get().GetWindow().GetHeight() == 0)
			return;
#endif

		device.logicalDevice.waitIdle();

		CleanupSwapChain();
		CreateSwapChain();
		image.CreateSwapChainImageViews(swapChainImageViews, swapChainImages, swapChainSurfaceFormat.format);
		image.CreateColorResources();
		image.CreateDepthResources();

		ReallocateDepthResources(swapChainExtent.width, swapChainExtent.height);
	}
}