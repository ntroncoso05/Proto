#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include <ktx.h>

namespace Proto
{
	class VulkanImage
	{
	public:
		VulkanImage() = default;
		~VulkanImage() = default;

		// Static factory helper replacing VK_ImageView
		static vk::raii::ImageView CreateImageView(const vk::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels);
		static void CreateSwapChainImageViews(std::vector<vk::raii::ImageView>& targetViews, const std::vector<vk::Image>& sourceImages, vk::Format format);

		void transition_image_layout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspect);
		void TransitionImageLayout(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t levels);

		std::pair<vk::raii::Image, vk::raii::DeviceMemory> CreateImage(uint32_t width, uint32_t height, uint32_t levels, vk::SampleCountFlagBits numSamples, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

		void CreateTextureImage(ktxTexture* kTexture, int width, int height, int channels);
		void CreateTextureImageView();
		void CreateTextureSampler();
		void CreateDepthResources();
		void CreateColorResources();

		vk::Format FindDepthFormat();
		vk::Format FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);
		void GenerateMipmaps(vk::raii::CommandBuffer& commandBuffer, vk::raii::Image& image, vk::Format format, int32_t width, int32_t height, uint32_t levels, ktxTexture* kTexture);

		uint32_t               mipLevels = 0;
		vk::raii::Image        textureImage = nullptr;
		vk::raii::DeviceMemory textureImageMemory = nullptr;
		vk::raii::ImageView    textureImageView = nullptr;
		vk::raii::Sampler      textureSampler = nullptr;

		vk::raii::Image        depthImage = nullptr;
		vk::raii::DeviceMemory depthImageMemory = nullptr;
		vk::raii::ImageView    depthImageView = nullptr;

		vk::raii::Image        colorImage = nullptr;
		vk::raii::DeviceMemory colorImageMemory = nullptr;
		vk::raii::ImageView    colorImageView = nullptr;
	};

}