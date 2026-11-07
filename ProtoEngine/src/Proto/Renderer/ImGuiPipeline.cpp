#include "protopch.hpp"
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include "ImGuiPipeline.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include "Core/ServiceLocator.hpp"

namespace Proto {

	void ImGuiPipeline::Init()
	{
		// Safely expose this custom pipeline layer globally via our Service Locator
		ServiceLocator::Register<ImGuiPipeline>(this);
	}

	void ImGuiPipeline::ExecuteFrame(vk::raii::CommandBuffer& cmd, uint32_t imageIndex, ImGuiVulkanUtil* imGui)
	{
        if (!imGui) return;

        auto& renderer = VulkanRenderer::Get();
        auto* swapchainSubsystem = renderer.vulkanSwapChain;
        vk::Image activeSwapchainImage = swapchainSubsystem->swapChainImages[imageIndex];

        // 🟩 STEP 2: RUN THE ATTACHMENT PASS DRAW LOOPS
        vk::RenderingAttachmentInfo uiColorAttachment = {
            .imageView = swapchainSubsystem->swapChainImageViews[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,  // Properly preserves the 3D scene geometry rendered behind this pass!, Change to eClear to clear layout traces cleanly
            .storeOp = vk::AttachmentStoreOp::eStore,
        };

        vk::RenderingInfo uiRenderingInfo = {
            .renderArea = {.offset = {0, 0}, .extent = swapchainSubsystem->swapChainExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &uiColorAttachment,
            .pDepthAttachment = nullptr
        };

        cmd.beginRendering(uiRenderingInfo);

        // Execute ImGui render instructions completely
        imGui->drawFrame(cmd, imageIndex, swapchainSubsystem->swapChainExtent);

        cmd.endRendering();

        // STEP 3: TRANSITION BACKBUFFER TO PRESENT LAYOUT This flips the image target so your window platform interface context can display it on screen!
        vk::ImageMemoryBarrier barrierToPresent{
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eNone,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = activeSwapchainImage,
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
        };

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            vk::DependencyFlags(), {}, {}, { barrierToPresent }
        );
	}

}