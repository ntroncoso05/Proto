#include "vulkan_image.hpp"
#include "vulkan_image_view.hpp"
#include "ShaderConfig.hpp"

#include <format> // Required for std::format

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Proto
{
	VulkanImage::VulkanImage(VulkanDevice* device, VulkanSwapChain* swapChain, VulkanCommandBuffer* commandBuffer) : vulkanDevice(device), vulkanSwapChain(swapChain), vulkanCommandBuffer(commandBuffer)
	{}

	VulkanImage::~VulkanImage() {
		// RAII will handle destruction
	}

	void VulkanImage::transition_image_layout(
		vk::Image               image,
		vk::ImageLayout         old_layout,
		vk::ImageLayout         new_layout,
		vk::AccessFlags2        src_access_mask,
		vk::AccessFlags2        dst_access_mask,
		vk::PipelineStageFlags2 src_stage_mask,
		vk::PipelineStageFlags2 dst_stage_mask,
		vk::ImageAspectFlags    image_aspect_flags)
	{
		vk::ImageMemoryBarrier2 barrier = {
			.srcStageMask = src_stage_mask,
			.srcAccessMask = src_access_mask,
			.dstStageMask = dst_stage_mask,
			.dstAccessMask = dst_access_mask,
			.oldLayout = old_layout,
			.newLayout = new_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {
				.aspectMask = image_aspect_flags,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1} };
		vk::DependencyInfo dependency_info = {
			.dependencyFlags = {},
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &barrier };
		vulkanCommandBuffer->commandBuffers[vulkanCommandBuffer->frameIndex].pipelineBarrier2(dependency_info);
	}

	void transitionImageLayout(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels)
	{
		vk::ImageMemoryBarrier barrier{ .oldLayout = oldLayout,
									   .newLayout = newLayout,
									   .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
									   .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
									   .image = image,
									   .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = mipLevels, .layerCount = 1} };

		vk::PipelineStageFlags sourceStage;
		vk::PipelineStageFlags destinationStage;

		if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
		{
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

			sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
			destinationStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			sourceStage = vk::PipelineStageFlagBits::eTransfer;
			destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else
		{
			throw std::invalid_argument("unsupported layout transition!");
		}
		commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);
	}

	void VulkanImage::createTextureImage(unsigned char* pixels, int width, int height, int channels)
	{
		//std::string path = std::format("{}/assets/{}", SHADER_BINARY_DIR, TEXTURE_PATH);
		int            texWidth = width, texHeight = height, texChannels = channels;
		//stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		vk::DeviceSize imageSize = texWidth * texHeight * 4;
		mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

		if (!pixels)
		{
			throw std::runtime_error("failed to load texture image!");
		}

		auto [stagingBuffer, stagingBufferMemory] =
			vulkanCommandBuffer->createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* data = stagingBufferMemory.mapMemory(0, imageSize);
		memcpy(data, pixels, imageSize);
		stagingBufferMemory.unmapMemory();

		stbi_image_free(pixels);

		std::tie(textureImage, textureImageMemory) = createImage(texWidth,
			texHeight,
			mipLevels,
			vk::SampleCountFlagBits::e1,
			vk::Format::eR8G8B8A8Srgb,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		vk::raii::CommandBuffer commandBuffer = vulkanCommandBuffer->beginSingleTimeCommands();
		transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
		vulkanCommandBuffer->copyBufferToImage(commandBuffer, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
		//transitioned to vk::ImageLayout::eShaderReadOnlyOptimal while generating mipmaps
		generateMipmaps(commandBuffer, textureImage, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels);
		//transitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
		vulkanCommandBuffer->endSingleTimeCommands(std::move(commandBuffer));
	}

	void VulkanImage::createTextureImageView()
	{
		textureImageView = vulkanSwapChain->createImageView(*textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels);
	}

	void VulkanImage::createTextureSampler()
	{
		vk::PhysicalDeviceProperties properties = vulkanDevice->physicalDevice.getProperties();
		vk::SamplerCreateInfo        samplerInfo{
		    .magFilter        = vk::Filter::eLinear,
		    .minFilter        = vk::Filter::eLinear,
		    .mipmapMode       = vk::SamplerMipmapMode::eLinear,
		    .addressModeU     = vk::SamplerAddressMode::eRepeat,
		    .addressModeV     = vk::SamplerAddressMode::eRepeat,
		    .addressModeW     = vk::SamplerAddressMode::eRepeat,
		    .mipLodBias       = 0.0f,
		    .anisotropyEnable = vk::True,
		    .maxAnisotropy    = properties.limits.maxSamplerAnisotropy,
		    .compareEnable    = vk::False,
		    .compareOp        = vk::CompareOp::eAlways,
		    .minLod           = 0.0f,
		    .maxLod           = vk::LodClampNone};
		textureSampler = vk::raii::Sampler(vulkanDevice->logicalDevice, samplerInfo);
	}

	std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanImage::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties)
	{
		vk::ImageCreateInfo imageInfo{ .imageType = vk::ImageType::e2D,
									  .format = format,
									  .extent = {width, height, 1},
									  .mipLevels = mipLevels,
									  .arrayLayers = 1,
									  .samples = numSamples,
									  .tiling = tiling,
									  .usage = usage,
									  .sharingMode = vk::SharingMode::eExclusive };

		vk::raii::Image image = vk::raii::Image(vulkanDevice->logicalDevice, imageInfo);

		vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size,
										 .memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, properties) };
		vk::raii::DeviceMemory imageMemory = vk::raii::DeviceMemory(vulkanDevice->logicalDevice, allocInfo);
		image.bindMemory(imageMemory, 0);

		return { std::move(image), std::move(imageMemory) };
	}

	void VulkanImage::createDepthResources()
	{
		vk::Format depthFormat = findDepthFormat();

		std::tie(depthImage, depthImageMemory) = createImage(vulkanSwapChain->swapChainExtent.width, vulkanSwapChain->swapChainExtent.height, 1, vulkanDevice->msaaSamples,
			depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
		depthImageView = VulkanImageView::createImageView(depthImage, depthFormat, *vulkanDevice, vk::ImageAspectFlagBits::eDepth, 1);
	}

	vk::Format VulkanImage::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features)
	{
		for (const auto format : candidates) {
			vk::FormatProperties props = vulkanDevice->physicalDevice.getFormatProperties(format);

			if (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) ||
				((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)))
			{
				return format;
			}
		}

		throw std::runtime_error("failed to find supported format!");
	}

	vk::Format VulkanImage::findDepthFormat()
	{
		return findSupportedFormat({ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment);
	}

	void VulkanImage::generateMipmaps(vk::raii::CommandBuffer& commandBuffer,
		vk::raii::Image& image,
		vk::Format               imageFormat,
		int32_t                  texWidth,
		int32_t                  texHeight,
		uint32_t                 mipLevels)
	{
		// Check if image format supports linear blitting
		vk::FormatProperties formatProperties = vulkanDevice->physicalDevice.getFormatProperties(imageFormat);
		if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
		{
			throw std::runtime_error("texture image format does not support linear blitting!");
		}

		vk::ImageMemoryBarrier barrier = { .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
										  .dstAccessMask = vk::AccessFlagBits::eTransferRead,
										  .oldLayout = vk::ImageLayout::eTransferDstOptimal,
										  .newLayout = vk::ImageLayout::eTransferSrcOptimal,
										  .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
										  .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
										  .image = image,
										  .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1} };

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < mipLevels; i++)
		{
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

			commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);

			vk::ImageBlit blit = { .srcSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = i - 1, .layerCount = 1},
								  .srcOffsets = std::array<vk::Offset3D, 2>({{}, {mipWidth, mipHeight, 1}}),
								  .dstSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = i, .layerCount = 1},
								  .dstOffsets = std::array<vk::Offset3D, 2>({{}, {1 < mipWidth ? mipWidth / 2 : 1, 1 < mipHeight ? mipHeight / 2 : 1, 1}}) };

			commandBuffer.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

			barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);

			if (1 < mipWidth)
			{
				mipWidth /= 2;
			}
			if (1 < mipHeight)
			{
				mipHeight /= 2;
			}
		}

		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);
	}

	void VulkanImage::createColorResources()
	{
		vk::Format colorFormat = vulkanSwapChain->swapChainSurfaceFormat.format;

		std::tie(colorImage, colorImageMemory) = createImage(vulkanSwapChain->swapChainExtent.width,
			vulkanSwapChain->swapChainExtent.height,
			1,
			vulkanDevice->msaaSamples,
			colorFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal);
		colorImageView = VulkanImageView::createImageView(colorImage, colorFormat, *vulkanDevice, vk::ImageAspectFlagBits::eColor, 1);
	}
}