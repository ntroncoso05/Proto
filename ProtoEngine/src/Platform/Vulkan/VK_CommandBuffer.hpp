#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

namespace Proto
{
	class ImGuiVulkanUtil;
	class Camera;

	class VulkanCommandBuffer
	{
	public:
		VulkanCommandBuffer() = default;
		~VulkanCommandBuffer() = default;

		std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateUniformBuffers();

		vk::raii::CommandBuffer BeginSingleTimeCommands();
		void EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer);

		void CreateDescriptorSetLayout();
		void CreateDescriptorSets();

		vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr };
		vk::raii::DescriptorPool             descriptorPool{ nullptr };
		std::vector<vk::raii::DescriptorSet> descriptorSets;

		vk::raii::CommandPool                commandPool{ nullptr };
		std::vector<vk::raii::CommandBuffer> commandBuffers;

		vk::raii::Buffer       vertexBuffer{ nullptr };
		vk::raii::DeviceMemory vertexBufferMemory{ nullptr };
		vk::raii::Buffer       indexBuffer{ nullptr };
		vk::raii::DeviceMemory indexBufferMemory{ nullptr };

		std::vector<vk::raii::Buffer>       uniformBuffers;
		std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
		std::vector<void*>                  uniformBuffersMapped;
	};

}