#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include "Platform/Vulkan/VK_CommandBuffer.hpp"
#include "Core/ServiceLocator.hpp"

namespace Proto::BufferUtils
{
	inline void CopyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
	{
		auto& cmdSubsystem = ServiceLocator::Get<VulkanCommandBuffer>();
		vk::raii::CommandBuffer commandCopyBuffer = cmdSubsystem.BeginSingleTimeCommands();
		commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
		cmdSubsystem.EndSingleTimeCommands(std::move(commandCopyBuffer));
	}

	inline void CopyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height)
	{
		vk::BufferImageCopy region{ .bufferOffset = 0,
								   .bufferRowLength = 0,
								   .bufferImageHeight = 0,
								   .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
								   .imageOffset = {0, 0, 0},
								   .imageExtent = {width, height, 1} };
		commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
	}
}