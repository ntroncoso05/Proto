#include "vulkan_command_buffer.hpp"
#include "vulkan/vulkan_image.hpp"
#include "ShaderConfig.hpp"

#include <chrono>
#include <format> // Required for std::format

//#define TINYOBJLOADER_IMPLEMENTATION
//#include <tiny_obj_loader.h>

namespace Proto
{
	VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice* device, VulkanSwapChain* swapChain, VulkanPipeline* pipeline) : vulkanDevice(device), vulkanSwapChain(swapChain), vulkanPipeline(pipeline)
	{
		//Vertex vertex;
		//Index index;
		//vertices = vertex.GenerateDefaultVertexData();
		//indices = index.GenerateDefaultIndexData();
		//loadModel();
	}

	// Destructor
	VulkanCommandBuffer::~VulkanCommandBuffer() {
		// RAII will handle destruction
	}

	vk::raii::CommandBuffer VulkanCommandBuffer::beginSingleTimeCommands()
	{
		vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
		vk::raii::CommandBuffer       commandBuffer = std::move(vk::raii::CommandBuffers(vulkanDevice->logicalDevice, allocInfo).front());

		vk::CommandBufferBeginInfo beginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
		commandBuffer.begin(beginInfo);

		return std::move(commandBuffer); //commandBuffer;
	}

	void VulkanCommandBuffer::endSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer)
	{
		commandBuffer.end();

		vk::SubmitInfo submitInfo{ .commandBufferCount = 1, .pCommandBuffers = &*commandBuffer };
		vulkanDevice->graphicsQueue.submit(submitInfo, nullptr);
		vulkanDevice->graphicsQueue.waitIdle();
	}

	void VulkanCommandBuffer::copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
	{
		vk::raii::CommandBuffer commandCopyBuffer = beginSingleTimeCommands();
		commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{ .size = size });
		endSingleTimeCommands(std::move(commandCopyBuffer));
	}

	void VulkanCommandBuffer::copyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height)
	{
		vk::BufferImageCopy region{ .bufferOffset = 0,
								   .bufferRowLength = 0,
								   .bufferImageHeight = 0,
								   .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
								   .imageOffset = {0, 0, 0},
								   .imageExtent = {width, height, 1} };
		commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
	}

	void VulkanCommandBuffer::createDescriptorSetLayout()
	{
		std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
			{{.binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex},
			 {.binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment}} };

		vk::DescriptorSetLayoutCreateInfo layoutInfo{ .bindingCount = static_cast<uint32_t>(bindings.size()), .pBindings = bindings.data() };
		descriptorSetLayout = vk::raii::DescriptorSetLayout(vulkanDevice->logicalDevice, layoutInfo);
	}

	void VulkanCommandBuffer::createCommandPool()
	{
		vk::CommandPoolCreateInfo poolInfo{ .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
											  .queueFamilyIndex = vulkanDevice->queueIndex };
		commandPool = vk::raii::CommandPool(vulkanDevice->logicalDevice, poolInfo);
	}

	void VulkanCommandBuffer::createCommandBuffers()
	{
		commandBuffers.clear();
		vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
		commandBuffers = vk::raii::CommandBuffers(vulkanDevice->logicalDevice, allocInfo);
	}

	void VulkanCommandBuffer::createVertexBuffer(const std::vector<Vertex>& vertices)
	{
		vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

		auto [stagingBuffer, stagingBufferMemory] =
			createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
		memcpy(dataStaging, vertices.data(), bufferSize);
		stagingBufferMemory.unmapMemory();

		std::tie(vertexBuffer, vertexBufferMemory) =
			createBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

		copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
	}

	void VulkanCommandBuffer::createIndexBuffer(const std::vector<uint32_t>& indices)
	{
		vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();

		auto [stagingBuffer, stagingBufferMemory] =
			createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* data = stagingBufferMemory.mapMemory(0, bufferSize);
		memcpy(data, indices.data(), (size_t)bufferSize);
		stagingBufferMemory.unmapMemory();

		std::tie(indexBuffer, indexBufferMemory) =
			createBuffer(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

		copyBuffer(stagingBuffer, indexBuffer, bufferSize);
	}

	std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VulkanCommandBuffer::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
	{
		vk::BufferCreateInfo   bufferInfo{ .size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive };
		vk::raii::Buffer       buffer = vk::raii::Buffer(vulkanDevice->logicalDevice, bufferInfo);
		vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
		vk::MemoryAllocateInfo allocInfo{ .allocationSize = memRequirements.size, .memoryTypeIndex = vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, properties) };
		vk::raii::DeviceMemory bufferMemory = vk::raii::DeviceMemory(vulkanDevice->logicalDevice, allocInfo);
		buffer.bindMemory(*bufferMemory, 0);
		return { std::move(buffer), std::move(bufferMemory) };
	}

	void VulkanCommandBuffer::recordCommandBuffer(uint32_t imageIndex, const std::vector<uint32_t>& indices)
	{
		auto& commandBuffer = commandBuffers[frameIndex];
		commandBuffer.begin({});

		// Before starting rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
		vulkanImage->transition_image_layout(
			vulkanSwapChain->swapChainImages[imageIndex],
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			{},                                                        // srcAccessMask (no need to wait for previous operations)
			vk::AccessFlagBits2::eColorAttachmentWrite,                // dstAccessMask
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,         // dstStage
			vk::ImageAspectFlagBits::eColor
		);

		// Transition the multisampled color image to vk::ImageLayout::eColorAttachmentOptimal
		vulkanImage->transition_image_layout(
			*vulkanImage->colorImage,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::AccessFlagBits2::eColorAttachmentWrite,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			vk::ImageAspectFlagBits::eColor);
		
		// Transition depth image to depth attachment optimal layout
		vulkanImage->transition_image_layout(
			*vulkanImage->depthImage,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::eDepthAttachmentOptimal,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
			vk::ImageAspectFlagBits::eDepth);

		vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
		vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

		// Color attachment (multisampled) with resolve attachment
		vk::RenderingAttachmentInfo colorAttachmentInfo = {
			.imageView = vulkanImage->colorImageView,
			.imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.resolveMode = vk::ResolveModeFlagBits::eAverage,
			.resolveImageView = vulkanSwapChain->swapChainImageViews[imageIndex],
			.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eStore,
			.clearValue = clearColor };

		// Depth attachment
		vk::RenderingAttachmentInfo depthAttachmentInfo = {
			.imageView = vulkanImage->depthImageView,
			.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.storeOp = vk::AttachmentStoreOp::eDontCare,
			.clearValue = clearDepth };

		vk::RenderingInfo renderingInfo = {
			.renderArea = {.offset = {0, 0}, .extent = vulkanSwapChain->swapChainExtent},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
			.pDepthAttachment     = &depthAttachmentInfo};

		commandBuffer.beginRendering(renderingInfo);
		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *vulkanPipeline->graphicsPipeline);
		// Negative height flips Y for Vulkan; see Uniform buffers.
		commandBuffer.setViewport(0, vk::Viewport(0.0f, static_cast<float>(vulkanSwapChain->swapChainExtent.height), static_cast<float>(vulkanSwapChain->swapChainExtent.width), -static_cast<float>(vulkanSwapChain->swapChainExtent.height), 0.0f, 1.0f));
		commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), vulkanSwapChain->swapChainExtent));
		commandBuffer.bindVertexBuffers(0, *vertexBuffer, { 0 });

		using CleanType = std::decay_t<decltype(indices)>;
		commandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexTypeValue<CleanType::value_type>::value);
		commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, vulkanPipeline->pipelineLayout, 0, *descriptorSets[frameIndex], nullptr);
		commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
		commandBuffer.endRendering();

		// After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
		vulkanImage->transition_image_layout(
			vulkanSwapChain->swapChainImages[imageIndex],
			vk::ImageLayout::eColorAttachmentOptimal,
			vk::ImageLayout::ePresentSrcKHR,
			vk::AccessFlagBits2::eColorAttachmentWrite,                // srcAccessMask
			{},                                                        // dstAccessMask
			vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
			vk::PipelineStageFlagBits2::eBottomOfPipe,                  // dstStage
			vk::ImageAspectFlagBits::eColor
		);
		commandBuffer.end();
	}	

	void VulkanCommandBuffer::createUniformBuffers()
	{
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
			auto [buffer, bufferMem] = createBuffer(
				bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			uniformBuffers.emplace_back(std::move(buffer));
			uniformBuffersMemory.emplace_back(std::move(bufferMem));
			uniformBuffersMapped.emplace_back(uniformBuffersMemory.back().mapMemory(0, bufferSize));
		}
	}

	void VulkanCommandBuffer::updateUniformBuffer(uint32_t currentImage)
	{
		static auto startTime = std::chrono::high_resolution_clock::now();

		auto  currentTime = std::chrono::high_resolution_clock::now();
		float time = std::chrono::duration<float>(currentTime - startTime).count();

		UniformBufferObject ubo{};
		ubo.model = glm::mat4(1.0f);//rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		ubo.proj =
			glm::perspective(glm::radians(45.0f), static_cast<float>(vulkanSwapChain->swapChainExtent.width) / static_cast<float>(vulkanSwapChain->swapChainExtent.height), 0.1f, 10.0f);

		memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
	}

	void VulkanCommandBuffer::createDescriptorPool()
	{
		std::array<vk::DescriptorPoolSize, 2> poolSize{ {{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = MAX_FRAMES_IN_FLIGHT},
														{.type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = MAX_FRAMES_IN_FLIGHT}} };
		vk::DescriptorPoolCreateInfo          poolInfo{ .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
													   .maxSets = MAX_FRAMES_IN_FLIGHT,
													   .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
													   .pPoolSizes = poolSize.data() };
		descriptorPool = vk::raii::DescriptorPool(vulkanDevice->logicalDevice, poolInfo);
	}

	void VulkanCommandBuffer::createDescriptorSets()
	{
		std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
		vk::DescriptorSetAllocateInfo        allocInfo{
			.descriptorPool = descriptorPool,
			.descriptorSetCount = static_cast<uint32_t>(layouts.size()),
			.pSetLayouts = layouts.data() };

		descriptorSets.clear();
		descriptorSets = vulkanDevice->logicalDevice.allocateDescriptorSets(allocInfo);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			vk::DescriptorBufferInfo bufferInfo{ .buffer = uniformBuffers[i], .offset = 0, .range = sizeof(UniformBufferObject) };
			vk::DescriptorImageInfo  imageInfo{ .sampler = vulkanImage->textureSampler, .imageView = vulkanImage->textureImageView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };

			std::array<vk::WriteDescriptorSet, 2> descriptorWrites{ {{.dstSet = descriptorSets[i],
																	 .dstBinding = 0,
																	 .dstArrayElement = 0,
																	 .descriptorCount = 1,
																	 .descriptorType = vk::DescriptorType::eUniformBuffer,
																	 .pBufferInfo = &bufferInfo},
																	{.dstSet = descriptorSets[i],
																	 .dstBinding = 1,
																	 .dstArrayElement = 0,
																	 .descriptorCount = 1,
																	 .descriptorType = vk::DescriptorType::eCombinedImageSampler,
																	 .pImageInfo = &imageInfo}} };
			vulkanDevice->logicalDevice.updateDescriptorSets(descriptorWrites, {});
		}
	}

	/*void VulkanCommandBuffer::loadModel()
	{
		//std::string path = std::format("{}/assets/{}", SHADER_BINARY_DIR, MODEL_PATH);

		tinyobj::attrib_t                attrib;
		std::vector<tinyobj::shape_t>    shapes;
		std::vector<tinyobj::material_t> materials;
		std::string                      warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str()))
		{
			throw std::runtime_error(warn + err);
		}

		std::unordered_map<Vertex, uint32_t> uniqueVertices{};

		for (const auto& shape : shapes)
		{
			for (const auto& index : shape.mesh.indices)
			{
				Vertex vertex{};

				vertex.pos = {
					attrib.vertices[3 * index.vertex_index + 0],
					attrib.vertices[3 * index.vertex_index + 1],
					attrib.vertices[3 * index.vertex_index + 2] };

				vertex.texCoord = {
					attrib.texcoords[2 * index.texcoord_index + 0],
					1.0f - attrib.texcoords[2 * index.texcoord_index + 1] };

				vertex.color = { 1.0f, 1.0f, 1.0f };

#if 1
				auto [it, inserted] = uniqueVertices.insert({ vertex, static_cast<uint32_t>(vertices.size()) });
				if (inserted)
				{
					vertices.push_back(vertex);
				}

				indices.push_back(it->second);
#else
				vertices.push_back(vertex);
				indices.push_back(static_cast<uint32_t>(indices.size()));
#endif
			}
		}
	}*/
}