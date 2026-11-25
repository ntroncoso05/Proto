#include "protopch.hpp"
#include "VK_Renderer.hpp"

#include "Core/Application.hpp"
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Core/InputSystem.hpp"
#include "Core/ServiceLocator.hpp" // Ensure ImGuiPipeline is visible
#include "Renderer/ImGuiPipeline.hpp"
#include "Renderer/RendererPipeline.hpp"
#include "Core/Log.hpp"

#ifdef PROTO_PLATFORM_ANDROID
	#include "Platform/Android/AndroidWindow.hpp"
#endif

#include <stb_image_write.h>
#include <filesystem>
#include <chrono>

namespace Proto
{
	// Define the instance tracking allocation
	VulkanRenderer* VulkanRenderer::s_VulkanInstance = nullptr;

	void VulkanRenderer::initImGui() {
		// Initialize ImGui directly
		
		auto& device = vulkanContext->GetDevice();

		// DEFINE A COMPLETELY ISOLATED POOL FOR IMGUI INTERFACES
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

		// ASSIGN THIS HANDSHAKE SEPARATELY TO IMGUI. This stops ImGui from colliding with Model::doLoad material handles!
		imGui = new ImGuiVulkanUtil(
			vulkanContext->GetDevice().logicalDevice,
			vulkanContext->GetDevice().physicalDevice,
			vulkanContext->GetDevice().graphicsQueue,
			VK_QUEUE_GRAPHICS_BIT,
			vulkanCommandBuffer,
			vulkanImage
		);

		// If ImGuiVulkanUtil exposes a descriptor pool pointer or requires a separate handle pass, make sure to transfer ownership or store this handle cleanly within your layer contexts!
		imGui->SetColorFormat(vulkanSwapChain->swapChainSurfaceFormat.format);
		imGui->init(vulkanSwapChain->swapChainExtent.width, vulkanSwapChain->swapChainExtent.height);
		imGui->initResources(); // No renderPass needed with dynamic rendering

		// Bind FinalColor to ImGui's Viewport descriptor set now that BOTH m_Rendergraph and imGui exist!
		if (m_Rendergraph) {
			if (auto* finalColor = m_Rendergraph->GetResource("FinalColor")) {
				imGui->SetSceneViewportTexture(*finalColor->view);
			}
		}
	}

	void VulkanRenderer::RecreateRendergraph(uint32_t _width, uint32_t _height)
	{
		uint32_t width = _width;
		uint32_t height = _height;

		if (width == 0 || height == 0) return;

		// Wait for GPU to finish using old rendergraph images before destroying them
		vulkanContext->GetDevice().logicalDevice.waitIdle();

		// Reset the persistent G-Buffer descriptor set so LightingPass updates it with the new views
		if (m_3DRenderer) {
			m_3DRenderer->ResetRendergraphDescriptorSets();
		}

		// Rebuild the rendergraph at the new window resolution
		m_Rendergraph = std::make_unique<Rendergraph>(GetLogicalDevice());
		m_Rendergraph->SetupDeferredRenderer(*m_Rendergraph, width, height);
		imGui->SetSceneViewportTexture(*m_Rendergraph->GetResource("FinalColor")->view);
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

#ifndef PROTO_PLATFORM_ANDROID
		windows.reset(static_cast<WindowsWindow*>(&Application::Get().GetWindow()));
		VulkanContext* contextPtr = static_cast<VulkanContext*>(windows->GetGraphicsContext());
#else
		VulkanContext* contextPtr = &ServiceLocator::Get<VulkanContext>();
#endif
		// Maintain context ownership inside root unique tracker
		vulkanContext.reset(contextPtr);
		vulkanContext->CreateDevice();

		// Register core subsystems to the unified engine ServiceLocator
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
		vulkanCommandBuffer->CreateCommandPool();
		vulkanImage->CreateColorResources();
		vulkanImage->CreateDepthResources();
		vulkanCommandBuffer->CreateUniformBuffers();
		vulkanCommandBuffer->CreateDescriptorSets();

		const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
		auto defaultWhiteTex = std::make_shared<Texture>("default_white");
		if (defaultWhiteTex->LoadFromMemory(whitePixel, 1, 1)) {
			resourceManager.RegisterExternal<Texture>("default_white", defaultWhiteTex);
		}

		vulkanCommandBuffer->CreateCommandBuffers();
		createSyncObjects();

		m_3DRenderer = std::make_unique<SceneRenderer>(vulkanContext->GetDevice().logicalDevice, vulkanContext->GetDevice().graphicsQueue);
		ServiceLocator::Register<SceneRenderer>(m_3DRenderer.get());

		// INITIALIZE THE SYSTEM RENDERGRAPH CONTEXT. Note: We leave it uncompiled until resources are cleanly declared during initVulkan
		m_Rendergraph = std::make_unique<Rendergraph>(GetLogicalDevice());

		// REGISTER DEFERRED MRT ATTACHMENTS AND PASSTHROUGH LAMBDAS TO THE GRAPH ENGINE
		uint32_t width = vulkanSwapChain->swapChainExtent.width;
		uint32_t height = vulkanSwapChain->swapChainExtent.height;

		// Configures GBuffer_Position, GBuffer_Normal, GBuffer_Albedo, Depth, and FinalColor maps automatically
		m_Rendergraph->SetupDeferredRenderer(*m_Rendergraph, width, height);
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

	void VulkanRenderer::drawFrame(float timestep)
	{
		//PROTO_CORE_INFO("drawFrame start");
		auto& device = ServiceLocator::Get<VulkanDevice>();
		auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();
		auto& cmdSubsystem = ServiceLocator::Get<VulkanCommandBuffer>();

		uint32_t frameIdx = swapChain.GetFrameIndex();
		glm::vec2 vpSize = imGui->GetViewportSize();

		if (vulkanContext->framebufferResized)
		{
			vulkanContext->framebufferResized = false;
			swapChain.RecreateSwapChain();
			RecreateRendergraph(vpSize.x, vpSize.y);
			return;
		}

		auto fenceResult = device.logicalDevice.waitForFences(*inFlightFences[frameIdx], vk::True, UINT64_MAX);
		if (fenceResult != vk::Result::eSuccess) throw std::runtime_error("failed to wait for fence!");

		vk::Result result = vk::Result::eSuccess;
		uint32_t imageIndex = 0;
		try {
			std::tie(result, imageIndex) = swapChain.swapChain.acquireNextImage(
				100'000'000ULL, *presentCompleteSemaphores[frameIdx], nullptr);
			if (result == vk::Result::eTimeout || result == vk::Result::eNotReady) {
				return;
			}
		}
		catch (const vk::OutOfDateKHRError&) {
			swapChain.RecreateSwapChain();
			RecreateRendergraph(vpSize.x, vpSize.y);
			return;
		}
		catch (const vk::SurfaceLostKHRError&) {
#ifdef PROTO_PLATFORM_ANDROID
			device.logicalDevice.waitIdle();
			swapChain.CleanupSwapChain();
			static_cast<AndroidWindow&>(Application::Get().GetWindow()).RecreateSurface();
#endif
			swapChain.RecreateSwapChain();
			if (vpSize.x > 0 && vpSize.y > 0) {
				RecreateRendergraph(vpSize.x, vpSize.y);
			}
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
		imGui->drawGUI();
		imGui->updateBuffers(frameIdx);

		// SCENE HANDOFF VIA GLOBAL SERVICE LOCATOR
		auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
		CameraComponent* activeCam = nullptr;
		for (Entity* ent : Entity::GetActiveSceneEntities()) {
			if (ent && ent->IsActive()) {
				if (auto* cam = ent->GetComponent<CameraComponent>()) {
					activeCam = cam;
					break;
				}
			}
		}
		if (activeCam) {
			pbrRenderer.SetCamera(activeCam);
		}

		auto* finalColorResource = m_Rendergraph->GetResource("FinalColor");
		uint32_t vpWidth = static_cast<uint32_t>(vpSize.x);
		uint32_t vpHeight = static_cast<uint32_t>(vpSize.y);

		if (vpWidth > 0 && vpHeight > 0 && m_Rendergraph) {
			if (finalColorResource && (finalColorResource->extent.width != vpWidth || finalColorResource->extent.height != vpHeight)) {
				vulkanContext->GetDevice().logicalDevice.waitIdle();

				if (m_3DRenderer) {
					m_3DRenderer->ResetRendergraphDescriptorSets();
				}

				m_Rendergraph = std::make_unique<Rendergraph>(GetLogicalDevice());
				m_Rendergraph->SetupDeferredRenderer(*m_Rendergraph, vpWidth, vpHeight);
				finalColorResource = m_Rendergraph->GetResource("FinalColor");
				imGui->SetSceneViewportTexture(*m_Rendergraph->GetResource("FinalColor")->view);
			}
		}

		// MASTER RECORDING INITIALIZATION
		auto& frameCmdBuffer = cmdSubsystem.commandBuffers[frameIdx];
		frameCmdBuffer.reset();
		frameCmdBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit }); // Open frame stream once

		// EXECUTE THE COMPILED DAG ENGINE. This records resource transitions, geometries, lighting setups, and submissions automatically!, Execute the compiled DAG graph engine to populate G-Buffers and "FinalColor"
		m_Rendergraph->Execute(frameCmdBuffer, device.graphicsQueue, frameIdx);

		if (finalColorResource)
		{
			vk::Image srcGraphImage = *finalColorResource->image;
			vk::Image dstSwapchainImage = swapChain.swapChainImages[imageIndex];

			// Transition swapchain layout to Color Attachment state for ImGui overlay writing
			vk::ImageMemoryBarrier barrierToUI{
				.srcAccessMask = vk::AccessFlagBits::eNone,
				.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
				.oldLayout = vk::ImageLayout::eUndefined,
				.newLayout = vk::ImageLayout::eColorAttachmentOptimal,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = dstSwapchainImage,
				.subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
			};

			frameCmdBuffer.pipelineBarrier(
				vk::PipelineStageFlagBits::eTopOfPipe,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				{}, {}, {}, { barrierToUI }
			);
		}

		// SEPARATE IMGUI PIPELINE TIMELINE RECORDING:
		// We allocate a secondary, fresh command buffer slice specifically for the UI overlay pass.
		// This guarantees it never collides with or attempts to append to the finalized rendergraph buffer!
		// Note: Adjust the pool lookup index or buffer mapping depending on how your container tracks allocations.
		auto& uiPipeline = ServiceLocator::Get<ImGuiPipeline>();
		uiPipeline.ExecuteFrame(frameCmdBuffer, imageIndex, imGui);

		frameCmdBuffer.end();

		vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		const vk::SubmitInfo submitInfo{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &*presentCompleteSemaphores[frameIdx],
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &*frameCmdBuffer, // Unified, dependency-safe command stream submission
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]
		};

		device.graphicsQueue.submit(submitInfo, *inFlightFences[frameIdx]);

		if (m_RequestScreenshot && finalColorResource && *finalColorResource->image) {
			m_RequestScreenshot = false;
			device.logicalDevice.waitIdle();

			uint32_t w = finalColorResource->extent.width;
			uint32_t h = finalColorResource->extent.height;
			vk::DeviceSize byteSize = static_cast<vk::DeviceSize>(w) * h * 4;

			auto [stagingBuf, stagingMem] = cmdSubsystem.CreateBuffer(
				byteSize,
				vk::BufferUsageFlagBits::eTransferDst,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
			);

			vk::raii::CommandBuffer copyCmd = cmdSubsystem.BeginSingleTimeCommands();

			vk::ImageMemoryBarrier toSrc{
				.srcAccessMask = vk::AccessFlagBits::eShaderRead,
				.dstAccessMask = vk::AccessFlagBits::eTransferRead,
				.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.newLayout = vk::ImageLayout::eTransferSrcOptimal,
				.image = *finalColorResource->image,
				.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
			};
			copyCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { toSrc });

			vk::BufferImageCopy region{
				.bufferOffset = 0,
				.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
				.imageOffset = { 0, 0, 0 },
				.imageExtent = { w, h, 1 }
			};
			copyCmd.copyImageToBuffer(*finalColorResource->image, vk::ImageLayout::eTransferSrcOptimal, *stagingBuf, { region });

			vk::ImageMemoryBarrier toRead{
				.srcAccessMask = vk::AccessFlagBits::eTransferRead,
				.dstAccessMask = vk::AccessFlagBits::eShaderRead,
				.oldLayout = vk::ImageLayout::eTransferSrcOptimal,
				.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.image = *finalColorResource->image,
				.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
			};
			copyCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { toRead });
			cmdSubsystem.EndSingleTimeCommands(std::move(copyCmd));

			// Map, swizzle BGRA -> RGBA, and save PNG
			const uint8_t* src = static_cast<const uint8_t*>(stagingMem.mapMemory(0, byteSize));
			std::vector<uint8_t> rgba(w * h * 4);
			for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
				rgba[i * 4 + 0] = src[i * 4 + 2]; // R
				rgba[i * 4 + 1] = src[i * 4 + 1]; // G
				rgba[i * 4 + 2] = src[i * 4 + 0]; // B
				rgba[i * 4 + 3] = 255;            // Opaque Alpha
			}
			stagingMem.unmapMemory();

			std::filesystem::path outDir = std::filesystem::exists("assets") ? "renders" : "../../../renders";
			std::filesystem::create_directories(outDir);

			static int shotCounter = 1;
			std::string savePath = m_PendingScreenshotPath.empty()
				? (outDir / std::format("render_{:03d}.png", shotCounter++)).generic_string()
				: m_PendingScreenshotPath;

			if (stbi_write_png(savePath.c_str(), static_cast<int>(w), static_cast<int>(h), 4, rgba.data(), static_cast<int>(w * 4))) {
				std::cout << "[RENDER EXPORT]: Saved clean viewport render to: " << savePath << std::endl;
			}
		}

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
			RecreateRendergraph(vpSize.x, vpSize.y);
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
	void VulkanRenderer::DrawIndexed(float timestep)
	{		
		drawFrame(timestep);
	}

}