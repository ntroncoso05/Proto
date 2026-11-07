#pragma once
#include <vulkan/vulkan_raii.hpp>

namespace Proto
{

	class VulkanSwapChain
	{
	public:
		VulkanSwapChain() = default;
		~VulkanSwapChain() = default;

		void CreateSwapChain();
		void CleanupSwapChain();
		void RecreateSwapChain();
		void CreateImageViews();

		// Pacing Synchronization Interface
		inline uint32_t GetFrameIndex() const { return m_FrameIndex; }
		inline void AdvanceFrame() { m_FrameIndex = (m_FrameIndex + 1) % MAX_FRAMES_IN_FLIGHT; }

		static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

		vk::raii::SwapchainKHR           swapChain = nullptr;
		std::vector<vk::Image>           swapChainImages;
		vk::SurfaceFormatKHR             swapChainSurfaceFormat;
		vk::Extent2D                     swapChainExtent;
		std::vector<vk::raii::ImageView> swapChainImageViews;

	private:
		vk::Extent2D ChooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities);
		uint32_t m_FrameIndex = 0;
	};

}