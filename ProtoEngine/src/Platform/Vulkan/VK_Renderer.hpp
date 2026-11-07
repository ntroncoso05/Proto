#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include "Core/Platform.hpp"
#include "Platform/Vulkan/VK_Context.hpp"
#include "Platform/Vulkan/VK_Device.hpp"
#include "Platform/Vulkan/VK_Swapchain.hpp"
#include "Platform/Vulkan/VK_Pipeline.hpp"
#include "Platform/Vulkan/VK_CommandBuffer.hpp"
#include "Platform/Vulkan/VK_Image.hpp"

#include "Core/ResourceTypes.hpp"
#include "Core/ResourceManager.hpp"


#include "Platform/Windows/WindowsPlatform.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Renderer/Camera.hpp"
#include "Core/DataObject.hpp"
#include "Core/ImGuiVulkanUtil.hpp"

namespace Proto
{
	class RenderPassManager;

	class VulkanRenderer : public RendererAPI
	{
	public:
		ImGuiVulkanUtil* imGui;

		std::unique_ptr<VulkanContext> vulkanContext;
		VulkanSwapChain* vulkanSwapChain = nullptr;
		VulkanPipeline* vulkanPipeline = nullptr;
		VulkanCommandBuffer* vulkanCommandBuffer = nullptr;
		VulkanImage* vulkanImage = nullptr;

		bool framebufferResized = false;

		// Create resource manager
		ResourceManager resourceManager;

		VulkanRenderer();

		/**
		 * @brief Destructor.
		*/
		~VulkanRenderer();

		void initVulkan();

		void createSyncObjects();
		void drawFrame(Camera& camera, float timestep);

		virtual void Init() override;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		virtual void SetClearColor(const glm::vec4& color) override;
		virtual void Clear() override;

		virtual void DrawIndexed(Camera& camera, float timestep) override;

		void initImGui();
	private:
		std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
		std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
		std::vector<vk::raii::Fence>     inFlightFences;

		std::unique_ptr< WindowsWindow>windows;
		std::unique_ptr<RenderPassManager> m_RenderPassManager;
	public:
		// Expose a clean, directly bound tracking pointer
		static VulkanRenderer* s_VulkanInstance;
		inline static VulkanRenderer& Get() { return *s_VulkanInstance; }

		// Expose structural component pointers
		inline VulkanCommandBuffer* GetCommandBuffer() { return vulkanCommandBuffer; }
		inline VulkanImage* GetVulkanImageSubsystem() { return vulkanImage; }
		inline vk::raii::Device& GetLogicalDevice() { return vulkanContext->GetDevice().logicalDevice; }
	};

}