#include "protopch.hpp"
#include "VK_Renderer.hpp"

#include "Core/Application.hpp"
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Core/InputSystem.hpp"
#include "Core/ServiceLocator.hpp" // Ensure ImGuiPipeline is visible
#include "Renderer/ImGuiPipeline.hpp"
#include "Renderer/RendererPipeline.hpp"

namespace Proto
{
	// Define the instance tracking allocation
	VulkanRenderer* VulkanRenderer::s_VulkanInstance = nullptr;

	void VulkanRenderer::initImGui() {
		// Initialize ImGui directly
		
		auto& device = vulkanContext->GetDevice();

		// STEP 1: DEFINE A COMPLETELY ISOLATED POOL FOR IMGUI INTERFACES
		std::array<vk::DescriptorPoolSize, 2> imguiPoolSizes = {
			vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 10 },
			vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 10 }
		};

		vk::DescriptorPoolCreateInfo imguiPoolInfo{
			.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
			.maxSets = 20,
			.poolSizeCount = static_cast<uint32_t>(imguiPoolSizes.size()),
			.pPoolSizes = imguiPoolSizes.data()
		};

		// Allocate the standalone pool handle onto the local device tree
		vk::raii::DescriptorPool imguiDedicatedPool(device.logicalDevice, imguiPoolInfo);

		// STEP 2: ASSIGN THIS HANDSHAKE SEPARATELY TO IMGUI. This stops ImGui from colliding with Model::doLoad material handles!
		imGui = new ImGuiVulkanUtil(
			vulkanContext->GetDevice().logicalDevice,
			vulkanContext->GetDevice().physicalDevice,
			vulkanContext->GetDevice().graphicsQueue,
			VK_QUEUE_GRAPHICS_BIT,
			vulkanCommandBuffer,
			vulkanImage
		);

		// If ImGuiVulkanUtil exposes a descriptor pool pointer or requires a separate handle pass, 
		// make sure to transfer ownership or store this handle cleanly within your layer contexts!
		imGui->init(vulkanSwapChain->swapChainExtent.width, vulkanSwapChain->swapChainExtent.height);
		imGui->initResources(); // No renderPass needed with dynamic rendering
	}
	VulkanRenderer::VulkanRenderer()
	{
		s_VulkanInstance = this; // Bind instance handle immediately on creation
		m_RenderPassManager = std::make_unique<RenderPassManager>();
	}

	VulkanRenderer::~VulkanRenderer()
	{
		// Clean up the uniform buffers directly from the updated command buffer container context
		if (vulkanCommandBuffer) {
			for (size_t i = 0; i < vulkanCommandBuffer->uniformBuffersMapped.size(); i++) {
				if (vulkanCommandBuffer->uniformBuffersMapped[i] != nullptr) {
					vulkanCommandBuffer->uniformBuffersMemory[i].unmapMemory();
				}
			}
		}
		delete imGui;
	}

	void VulkanRenderer::Init()
	{
		s_VulkanInstance = this;
		ServiceLocator::Register<RendererAPI>(this);

		ServiceLocator::Register<ResourceManager>(&resourceManager);

		windows.reset(static_cast<WindowsWindow*>(&Application::Get().GetWindow()));
		VulkanContext* contextPtr = static_cast<VulkanContext*>(windows->GetGraphicsContext());
		// 1. Maintain context ownership inside root unique tracker
		vulkanContext.reset(contextPtr);
		vulkanContext->CreateDevice();

		// 2. Register core subsystems to the unified engine ServiceLocator
		ServiceLocator::Register<VulkanContext>(vulkanContext.get());
		ServiceLocator::Register<VulkanDevice>(&vulkanContext->GetDevice());

		// Allocate structural subsystems using safe unique owners
		auto swapChainAlloc = std::make_unique<VulkanSwapChain>();
		swapChainAlloc->CreateSwapChain();
		ServiceLocator::Register<VulkanSwapChain>(swapChainAlloc.get());
		vulkanSwapChain = swapChainAlloc.release(); // Relinquish raw handle out to lifecycle managers

		auto cmdAlloc = std::make_unique<VulkanCommandBuffer>();
		ServiceLocator::Register<VulkanCommandBuffer>(cmdAlloc.get());
		vulkanCommandBuffer = cmdAlloc.release();

		auto imgAlloc = std::make_unique<VulkanImage>();
		ServiceLocator::Register<VulkanImage>(imgAlloc.get());
		vulkanImage = imgAlloc.release();

		auto pipelineAlloc = std::make_unique<VulkanPipeline>();
		ServiceLocator::Register<VulkanPipeline>(pipelineAlloc.get());
		vulkanPipeline = pipelineAlloc.release();

		// Finish building drivers
		initVulkan();
		initImGui();
		InputSystem::Initialize(vulkanContext->GetWindow());
	}

	void VulkanRenderer::initVulkan()
	{
		vulkanSwapChain->CreateImageViews();
		vulkanCommandBuffer->CreateDescriptorSetLayout();
		vulkanPipeline->createGraphicsPipeline();
		vulkanCommandBuffer->CreateCommandPool();
		vulkanImage->CreateColorResources();
		vulkanImage->CreateDepthResources();
		vulkanCommandBuffer->CreateUniformBuffers();
		vulkanCommandBuffer->CreateDescriptorSets();

		auto modelTextureHandle = resourceManager.Load<Texture>("viking_room"); //TODO: temp change order in Sandbox.cpp ar Application.cpp
		if (!modelTextureHandle.IsValid()) {
			std::cerr << "[CRITICAL]: Failed to pre-load 'viking_room' texture view asset during initVulkan!" << std::endl;
		}
		vulkanCommandBuffer->CreateCommandBuffers();
		createSyncObjects();
	}

	void VulkanRenderer::createSyncObjects()
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();

		assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() && inFlightFences.empty());

		for (size_t i = 0; i < swapChain.swapChainImages.size(); i++)
		{
			renderFinishedSemaphores.emplace_back(device.logicalDevice, vk::SemaphoreCreateInfo());
		}

		for (size_t i = 0; i < VulkanSwapChain::MAX_FRAMES_IN_FLIGHT; i++)
		{
			presentCompleteSemaphores.emplace_back(device.logicalDevice, vk::SemaphoreCreateInfo());
			inFlightFences.emplace_back(device.logicalDevice, vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
		}
	}

	void VulkanRenderer::drawFrame(Camera& camera, float timestep)
	{
		auto& device = ServiceLocator::Get<VulkanDevice>();
		auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();
		auto& cmdSubsystem = ServiceLocator::Get<VulkanCommandBuffer>();

		uint32_t frameIdx = swapChain.GetFrameIndex();

		if (vulkanContext->framebufferResized)
		{
			vulkanContext->framebufferResized = false;
			swapChain.RecreateSwapChain();
			return;
		}

		auto fenceResult = device.logicalDevice.waitForFences(*inFlightFences[frameIdx], vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess) throw std::runtime_error("failed to wait for fence!");

		auto [result, imageIndex] = swapChain.swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[frameIdx], nullptr);

		if (result == vk::Result::eErrorOutOfDateKHR)
		{
			swapChain.RecreateSwapChain();
			return;
		}

		// On other success codes than eSuccess and eSuboptimalKHR we just throw an exception. On any error code, aquireNextImage already threw an exception.
		if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
		{
			assert(result == vk::Result::eTimeout || result == vk::Result::eNotReady);
			throw std::runtime_error("failed to acquire swap chain image!");
		}

		device.logicalDevice.resetFences(*inFlightFences[frameIdx]);

		imGui->newFrame(swapChain.swapChainExtent);
		imGui->drawGUI(camera);
		imGui->updateBuffers();

		auto& cmdBuffer = cmdSubsystem.commandBuffers[frameIdx];
		cmdBuffer.reset();
		cmdBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		// SCENE HANDOFF VIA GLOBAL SERVICE LOCATOR
		auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
		pbrRenderer.SetCamera(&camera);
		pbrRenderer.Render(Entity::GetActiveSceneEntities(), cmdBuffer, imageIndex);

		auto& uiPipeline = ServiceLocator::Get<ImGuiPipeline>();
		uiPipeline.ExecuteFrame(cmdBuffer, imageIndex, imGui);

		cmdBuffer.end();

		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*presentCompleteSemaphores[frameIdx],
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*cmdBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]
		};

		device.graphicsQueue.submit(submitInfo, *inFlightFences[frameIdx]);

		const vk::PresentInfoKHR presentInfoKHR{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
			.swapchainCount = 1,
			.pSwapchains = &*swapChain.swapChain,
			.pImageIndices = &imageIndex
		};

		result = device.graphicsQueue.presentKHR(presentInfoKHR);
		if ((result == vk::Result::eSuboptimalKHR) || (result == vk::Result::eErrorOutOfDateKHR))
		{
			swapChain.RecreateSwapChain();
		}
		else
		{
			// There are no other success codes than eSuccess; on any error code, presentKHR already threw an exception.
			assert(result == vk::Result::eSuccess);
		}

		// Pacing advanced systematically on the tracking layout
		swapChain.AdvanceFrame();

	}

	void VulkanRenderer::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{}
	void VulkanRenderer::SetClearColor(const glm::vec4 & color)
	{}
	void VulkanRenderer::Clear()
	{}
	void VulkanRenderer::DrawIndexed(Camera& camera, float timestep)
	{		
		drawFrame(camera, timestep);
	}

}