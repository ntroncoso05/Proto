#include "vulkan_renderer.hpp"

namespace Proto
{
	VulkanRenderer::VulkanRenderer()
	{
		platform->initWindow();
		vulkanDevice = new VulkanDevice(platform->instance, platform->surface, platform->requiredExtensions);
		vulkanSwapChain = new VulkanSwapChain(*platform, *vulkanDevice);
		vulkanPipeline = new VulkanPipeline(vulkanDevice, vulkanSwapChain);
		vulkanCommandBuffer = new VulkanCommandBuffer(vulkanDevice, vulkanSwapChain, vulkanPipeline);
		vulkanImage = new VulkanImage(vulkanDevice, vulkanSwapChain, vulkanCommandBuffer);
		vulkanPipeline->vulkanCommandBuffer = vulkanCommandBuffer;
		vulkanCommandBuffer->vulkanImage = vulkanImage;
	}

	VulkanRenderer::~VulkanRenderer()
	{}

	void resourceManager()
	{
		// Load resources
		//texture = resourceManager.Load<Texture>("viking_room").Get();
		//texture.Get().
		/*auto mesh = resourceManager.Load<Mesh>("cube");
		auto vertexShader = resourceManager.Load<Shader>("basic", vk::ShaderStageFlagBits::eVertex);
		auto fragmentShader = resourceManager.Load<Shader>("basic", vk::ShaderStageFlagBits::eFragment);

		// Use resources
		if (texture && mesh && vertexShader && fragmentShader) {
			// Create material using shaders
			Material material(vertexShader, fragmentShader);

			// Set texture in material
			material.SetTexture("diffuse", texture);

			// Create entity with mesh and material
			Entity entity("MyEntity");
			auto meshComponent = entity.AddComponent<MeshComponent>(mesh.Get(), &material);
		}
		
		// Resources will be automatically released when handles go out of scope
		// or you can explicitly release them
		resourceManager.Release(texture.GetId());*/
	}
	void VulkanRenderer::initVulkan()
	{
		platform->createInstance();
		platform->setupDebugMessenger();
		platform->createSurface();
		vulkanDevice->pickPhysicalDevice();
		vulkanDevice->createLogicalDevice();
		vulkanSwapChain->createSwapChain();
		vulkanSwapChain->createImageViews();
		vulkanCommandBuffer->createDescriptorSetLayout();
		vulkanPipeline->createGraphicsPipeline();
		vulkanCommandBuffer->createCommandPool();
		vulkanImage->createColorResources();
		vulkanImage->createDepthResources();
		texture = resourceManager.Load<Texture>("viking_room").Get();
		vulkanImage->createTextureImage(texture->pixels, texture->width, texture->height, texture->height);
		vulkanImage->createTextureImageView();
		vulkanImage->createTextureSampler();
		mesh = resourceManager.Load<Mesh>("viking_room").Get();
		vulkanCommandBuffer->createVertexBuffer(mesh->GetVertices());
		vulkanCommandBuffer->createIndexBuffer(mesh->GetIndices());
		vulkanCommandBuffer->createUniformBuffers();
		vulkanCommandBuffer->createDescriptorPool();
		vulkanCommandBuffer->createDescriptorSets();
		vulkanCommandBuffer->createCommandBuffers();
		createSyncObjects();
	}

	void VulkanRenderer::createSyncObjects()
	{
		assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

		for (size_t i = 0; i < vulkanSwapChain->swapChainImages.size(); i++)
		{
			renderFinishedSemaphores.emplace_back(vulkanDevice->logicalDevice, vk::SemaphoreCreateInfo());
		}

		for (size_t i = 0; i < vulkanCommandBuffer->MAX_FRAMES_IN_FLIGHT; i++)
		{
			presentCompleteSemaphores.emplace_back(vulkanDevice->logicalDevice, vk::SemaphoreCreateInfo());
			inFlightFences.emplace_back(vulkanDevice->logicalDevice, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
		}
	}

	void VulkanRenderer::drawFrame()
	{
		// Note: inFlightFences, presentCompleteSemaphores, and commandBuffers are indexed by frameIndex,
		//       while renderFinishedSemaphores is indexed by imageIndex
		auto fenceResult = vulkanDevice->logicalDevice.waitForFences(*inFlightFences[vulkanCommandBuffer->frameIndex], vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess)
		{
			throw std::runtime_error("failed to wait for fence!");
		}

		auto [result, imageIndex] = vulkanSwapChain->swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[vulkanCommandBuffer->frameIndex], nullptr);

		// Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
		// here and does not need to be caught by an exception.
		if (result == vk::Result::eErrorOutOfDateKHR)
		{
			vulkanSwapChain->recreateSwapChain(platform->window, vulkanImage);
			return;
		}
		// On other success codes than eSuccess and eSuboptimalKHR we just throw an exception.
		// On any error code, aquireNextImage already threw an exception.
		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
		{
			assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
			throw std::runtime_error("failed to acquire swap chain image!");
		}
		vulkanCommandBuffer->updateUniformBuffer(vulkanCommandBuffer->frameIndex);

		// Only reset the fence if we are submitting work
		vulkanDevice->logicalDevice.resetFences(*inFlightFences[vulkanCommandBuffer->frameIndex]);

		vulkanCommandBuffer->commandBuffers[vulkanCommandBuffer->frameIndex].reset();
		vulkanCommandBuffer->recordCommandBuffer(imageIndex, mesh->GetIndices());

		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo   submitInfo{ .waitSemaphoreCount = 1,
										  .pWaitSemaphores = &*presentCompleteSemaphores[vulkanCommandBuffer->frameIndex],
										  .pWaitDstStageMask = &waitDestinationStageMask,
										  .commandBufferCount = 1,
										  .pCommandBuffers = &*vulkanCommandBuffer->commandBuffers[vulkanCommandBuffer->frameIndex],
										  .signalSemaphoreCount = 1,
										  .pSignalSemaphores = &*renderFinishedSemaphores[imageIndex] };
		vulkanDevice->graphicsQueue.submit(submitInfo, *inFlightFences[vulkanCommandBuffer->frameIndex]);

		const vk::PresentInfoKHR presentInfoKHR{ .waitSemaphoreCount = 1,
												.pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
												.swapchainCount = 1,
												.pSwapchains = &*vulkanSwapChain->swapChain,
												.pImageIndices = &imageIndex };
		result = vulkanDevice->graphicsQueue.presentKHR(presentInfoKHR);
		// Due to VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS being defined, eErrorOutOfDateKHR can be checked as a result
		// here and does not need to be caught by an exception.
		if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR) || platform->framebufferResized)
		{
			platform->framebufferResized = false;
			vulkanSwapChain->recreateSwapChain(platform->window, vulkanImage);
		}
		else
		{
			// There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
			assert(result == vk::Result::eSuccess);
		}
		vulkanCommandBuffer->frameIndex = (vulkanCommandBuffer->frameIndex + 1) % vulkanCommandBuffer->MAX_FRAMES_IN_FLIGHT;
	}
}