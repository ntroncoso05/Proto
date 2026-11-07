#include "protopch.hpp"

#include "Platform/Vulkan/VK_Device.hpp"
#include "Platform/Vulkan/VK_Image.hpp"
#include "Platform/Vulkan/VK_CommandBuffer.hpp"
#include "Platform/Vulkan/VK_Swapchain.hpp"
#include "Core/ServiceLocator.hpp"
#include "Platform/Vulkan/VK_BufferUtils.hpp"

namespace Proto
{

	vk::raii::ImageView VulkanImage::CreateImageView(const vk::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels)
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		vk::ImageViewCreateInfo viewInfo{
			.image = image,
			.viewType = vk::ImageViewType::e2D,
			.format = format,
			.subresourceRange = {.aspectMask = aspectFlags, .baseMipLevel = 0, .levelCount = mipLevels, .baseArrayLayer = 0, .layerCount = 1}
		};
		return vk::raii::ImageView(device.logicalDevice, viewInfo);
	}

	void VulkanImage::CreateSwapChainImageViews(std::vector<vk::raii::ImageView>& targetViews, const std::vector<vk::Image>& sourceImages, vk::Format format)
	{
		targetViews.clear();
		targetViews.reserve(sourceImages.size());
		for (const auto& image : sourceImages)
		{
			targetViews.emplace_back(CreateImageView(image, format, vk::ImageAspectFlagBits::eColor, 1));
		}
	}

	void VulkanImage::transition_image_layout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess, vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage, vk::ImageAspectFlags aspect)
	{
		auto frameIndex = ServiceLocator::Get<VulkanSwapChain>().GetFrameIndex();
		auto& cmdSubsystem = ServiceLocator::Get<VulkanCommandBuffer>();

		vk::ImageMemoryBarrier2 barrier{
			.srcStageMask = srcStage,
			.srcAccessMask = srcAccess,
			.dstStageMask = dstStage,
			.dstAccessMask = dstAccess,
			.oldLayout = oldLayout,
			.newLayout = newLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = image,
			.subresourceRange = {.aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
		};
		vk::DependencyInfo dependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
		cmdSubsystem.commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
	}

	void VulkanImage::TransitionImageLayout(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels)
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
		else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::ePresentSrcKHR)
		{
			barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eNone;
	
			sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			destinationStage = vk::PipelineStageFlagBits::eBottomOfPipe;
		}
		else
		{
			throw std::invalid_argument("unsupported layout transition!");
		}
		commandBuffer.pipelineBarrier(sourceStage, destinationStage, {}, {}, {}, barrier);
	}

	void VulkanImage::CreateTextureImage(/*unsigned char* pixels*/ktxTexture* kTexture, int width, int height, int channels)
	{

		// Get texture dimensions and data
		uint32_t texWidth = kTexture->baseWidth;
		uint32_t texHeight = kTexture->baseHeight;
		ktx_size_t imageSize = ktxTexture_GetImageSize(kTexture, 0);
		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(kTexture);

		mipLevels = kTexture->numLevels;//static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

		if (!kTexture)
		{
			throw std::runtime_error("failed to load texture image!");
		}
		auto& cmdBuffer = ServiceLocator::Get<VulkanCommandBuffer>();
		// Create staging buffer
		auto [stagingBuffer, stagingBufferMemory] =
			cmdBuffer.CreateBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* data = stagingBufferMemory.mapMemory(0, imageSize);

		memcpy(data, ktxTextureData, imageSize);
		stagingBufferMemory.unmapMemory();

		// Create the texture image
		std::tie(textureImage, textureImageMemory) = CreateImage(texWidth,
			texHeight,
			mipLevels,
			vk::SampleCountFlagBits::e1,
			vk::Format::eR8G8B8A8Srgb,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			vk::MemoryPropertyFlagBits::eDeviceLocal);

		// Copy data from staging buffer to texture image
		vk::raii::CommandBuffer commandBuffer = cmdBuffer.BeginSingleTimeCommands();
		TransitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
		BufferUtils::CopyBufferToImage(commandBuffer, stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
		GenerateMipmaps(commandBuffer, textureImage, vk::Format::eR8G8B8A8Srgb, texWidth, texHeight, mipLevels, kTexture);
		TransitionImageLayout(commandBuffer, textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
		cmdBuffer.EndSingleTimeCommands(std::move(commandBuffer));
	}

	void VulkanImage::CreateTextureImageView()
	{
		textureImageView = CreateImageView(*textureImage, vk::Format::eR8G8B8A8Srgb, vk::ImageAspectFlagBits::eColor, mipLevels);
	}

	void VulkanImage::CreateTextureSampler()
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		vk::PhysicalDeviceProperties properties = device.physicalDevice.getProperties();
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
		textureSampler = vk::raii::Sampler(device.logicalDevice, samplerInfo);
	}

	std::pair<vk::raii::Image, vk::raii::DeviceMemory> VulkanImage::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, vk::SampleCountFlagBits numSamples, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties)
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		vk::ImageCreateInfo imageInfo{ .imageType = vk::ImageType::e2D,
									  .format = format,
									  .extent = {width, height, 1},
									  .mipLevels = mipLevels,
									  .arrayLayers = 1,
									  .samples = numSamples,
									  .tiling = tiling,
									  .usage = usage,
									  .sharingMode = vk::SharingMode::eExclusive };

		vk::raii::Image image = vk::raii::Image(device.logicalDevice, imageInfo);

		vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size,
										 .memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, properties) };
		vk::raii::DeviceMemory imageMemory = vk::raii::DeviceMemory(device.logicalDevice, allocInfo);
		image.bindMemory(imageMemory, 0);

		return { std::move(image), std::move(imageMemory) };
	}

	void VulkanImage::CreateDepthResources()
	{
		auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();
		auto& device = ServiceLocator::Get<VulkanDevice>();
		vk::Format depthFormat = FindDepthFormat();

		std::tie(depthImage, depthImageMemory) = CreateImage(swapChain.swapChainExtent.width, swapChain.swapChainExtent.height, 1, device.msaaSamples,
			depthFormat, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::MemoryPropertyFlagBits::eDeviceLocal);
		depthImageView = CreateImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
	}

	vk::Format VulkanImage::FindSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features)
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		for (const auto format : candidates) {
			vk::FormatProperties props = device.physicalDevice.getFormatProperties(format);

			if (((tiling == vk::ImageTiling::eLinear) && ((props.linearTilingFeatures & features) == features)) ||
				((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features)))
			{
				return format;
			}
		}

		throw std::runtime_error("failed to find supported format!");
	}

	vk::Format VulkanImage::FindDepthFormat()
	{
		return FindSupportedFormat({ vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint },
			vk::ImageTiling::eOptimal,
			vk::FormatFeatureFlagBits::eDepthStencilAttachment);
	}

	void VulkanImage::GenerateMipmaps(vk::raii::CommandBuffer& commandBuffer,
		vk::raii::Image& image,
		vk::Format               imageFormat,
		int32_t                  texWidth,
		int32_t                  texHeight,
		uint32_t                 mipLevels,
		ktxTexture* kTexture)
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		// Check if image format supports linear blitting
		vk::FormatProperties formatProperties = device.physicalDevice.getFormatProperties(imageFormat);
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
			ktx_size_t offset;
			KTX_error_code result = ktxTexture_GetImageOffset(kTexture, i, 0, 0, &offset);

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

	void VulkanImage::CreateColorResources()
	{
		auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();
		auto& device = ServiceLocator::Get<VulkanDevice>();
		vk::Format colorFormat = swapChain.swapChainSurfaceFormat.format;

		std::tie(colorImage, colorImageMemory) = CreateImage(swapChain.swapChainExtent.width,
			swapChain.swapChainExtent.height,
			1,
			device.msaaSamples,
			colorFormat,
			vk::ImageTiling::eOptimal,
			vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eColorAttachment,
			vk::MemoryPropertyFlagBits::eDeviceLocal);
		colorImageView = CreateImageView(colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, 1);
	}

}