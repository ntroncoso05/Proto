#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include "vulkan_command_buffer.hpp"

namespace Proto
{
	class VulkanImage
	{
	public:
		VulkanDevice* vulkanDevice = nullptr;
		VulkanSwapChain* vulkanSwapChain = nullptr;
		VulkanCommandBuffer* vulkanCommandBuffer = nullptr;

		uint32_t               mipLevels = 0;
		vk::raii::Image        textureImage = nullptr;
		vk::raii::DeviceMemory textureImageMemory = nullptr;
		vk::raii::ImageView    textureImageView = nullptr;
		vk::raii::Sampler      textureSampler = nullptr;

		vk::raii::Image        depthImage = nullptr;
		vk::raii::DeviceMemory depthImageMemory = nullptr;
		vk::raii::ImageView    depthImageView = nullptr;

		// Multisampling(msaa)
		vk::raii::Image        colorImage = nullptr;
		vk::raii::DeviceMemory colorImageMemory = nullptr;
		vk::raii::ImageView    colorImageView = nullptr;

		//const std::string  TEXTURE_PATH = "textures/viking_room.png";

		VulkanImage(VulkanDevice* device, VulkanSwapChain* swapChain, VulkanCommandBuffer* commandBuffer);

		/**
		 * @brief Destructor.
		*/
		~VulkanImage();

		void transition_image_layout(
			vk::Image               image,
			vk::ImageLayout         old_layout,
			vk::ImageLayout         new_layout,
			vk::AccessFlags2        src_access_mask,
			vk::AccessFlags2        dst_access_mask,
			vk::PipelineStageFlags2 src_stage_mask,
			vk::PipelineStageFlags2 dst_stage_mask,
			vk::ImageAspectFlags    image_aspect_flags);

		std::pair<vk::raii::Image, vk::raii::DeviceMemory> createImage(uint32_t width,
			uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);
		void createTextureImage(unsigned char* pixels, int width, int height, int channels);
		void createTextureImageView();
		void createTextureSampler();

		void createDepthResources();
		vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features);
		vk::Format findDepthFormat();
		void generateMipmaps(vk::raii::CommandBuffer& commandBuffer,
			vk::raii::Image& image,
			vk::Format               imageFormat,
			int32_t                  texWidth,
			int32_t                  texHeight,
			uint32_t                 mipLevels);

		void createColorResources();
	};
}