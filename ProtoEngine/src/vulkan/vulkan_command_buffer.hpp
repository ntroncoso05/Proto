#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

#include "vulkan_device.hpp"
#include "vulkan_swapchain.hpp"
#include "vulkan_pipeline.hpp"
#include "core/buffer.hpp"

namespace Proto
{
	class VulkanImage;
	class Mesh;

	class VulkanCommandBuffer
	{
	public:
		VulkanDevice* vulkanDevice = nullptr;
		VulkanSwapChain* vulkanSwapChain = nullptr;
		VulkanPipeline* vulkanPipeline = nullptr;
		VulkanImage* vulkanImage = nullptr;
		uint32_t frameIndex = 0;
		uint32_t MAX_FRAMES_IN_FLIGHT = 2;

		vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;

		vk::raii::Buffer       vertexBuffer = nullptr;
		vk::raii::DeviceMemory vertexBufferMemory = nullptr;
		vk::raii::Buffer       indexBuffer = nullptr;
		vk::raii::DeviceMemory indexBufferMemory = nullptr;

		vk::raii::DescriptorPool             descriptorPool = nullptr;
		std::vector<vk::raii::DescriptorSet> descriptorSets;

		vk::raii::CommandPool                commandPool = nullptr;
		std::vector<vk::raii::CommandBuffer> commandBuffers;

		std::vector<vk::raii::Buffer>       uniformBuffers;
		std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
		std::vector<void*>                  uniformBuffersMapped;

		//std::vector<Vertex> vertices;
		//std::vector<uint32_t> indices;
		//const std::string  MODEL_PATH = "models/viking_room.obj";

		VulkanCommandBuffer(VulkanDevice* device, VulkanSwapChain* swapChain, VulkanPipeline* pipeline);

		/**
		 * @brief Destructor.
		*/
		~VulkanCommandBuffer();

		void createCommandPool();
		void createCommandBuffers();
		void recordCommandBuffer(uint32_t imageIndex, const std::vector<uint32_t>& indices);

		std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
		void createVertexBuffer(const std::vector<Vertex>& vertices);
		void createIndexBuffer(const std::vector<uint32_t>& indices);

		void createDescriptorSetLayout();
		void createDescriptorPool();
		void createDescriptorSets();

		vk::raii::CommandBuffer beginSingleTimeCommands();
		void endSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer);

		void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size);
		void copyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height);

		void createUniformBuffers();
		void updateUniformBuffer(uint32_t currentImage);	

		void loadModel();
	};
}