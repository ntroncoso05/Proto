#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
	#	include <vulkan/vulkan_raii.hpp>
#else
	import vulkan_hpp;
#endif

#include "Platform/Vulkan/VK_Context.hpp"
#include "Platform/Vulkan/VK_Device.hpp"
#include "Platform/Vulkan/VK_Swapchain.hpp"
#include "Platform/Vulkan/VK_Pipeline.hpp"
#include "Platform/Vulkan/VK_CommandBuffer.hpp"
#include "Platform/Vulkan/VK_Image.hpp"
#ifndef PROTO_PLATFORM_ANDROID
	#include "Platform/Windows/WindowsPlatform.hpp"
#endif

#include "Core/ResourceTypes.hpp"
#include "Core/ResourceManager.hpp"
#include "Core/ImGuiVulkanUtil.hpp"
#include "Renderer/RendererAPI.hpp"

namespace Proto
{
	class RenderPassManager;
	class SceneRenderer;
	class Rendergraph;

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
		void drawFrame(float timestep);

		virtual void Init() override;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;

		virtual void SetClearColor(const glm::vec4& color) override;
		virtual void Clear() override;

		virtual void DrawIndexed(float timestep) override;

		void initImGui();

		bool m_RequestScreenshot = false;
		std::string m_PendingScreenshotPath = "";
		void RequestScreenshot(const std::string& outputPath = "") {
			m_RequestScreenshot = true;
			m_PendingScreenshotPath = outputPath;
		}
	private:
		std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
		std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
		std::vector<vk::raii::Fence>     inFlightFences;

#ifndef PROTO_PLATFORM_ANDROID
		std::unique_ptr< WindowsWindow>windows;
#endif
		std::unique_ptr<RenderPassManager> m_RenderPassManager;
		std::unique_ptr<SceneRenderer> m_3DRenderer;
		std::unique_ptr<Rendergraph> m_Rendergraph;

		void RecreateRendergraph(uint32_t _width, uint32_t _height);
	public:
		// Expose a clean, directly bound tracking pointer
		static VulkanRenderer* s_VulkanInstance;
		inline static VulkanRenderer& Get() { return *s_VulkanInstance; }

		// Expose structural component pointers
		inline VulkanCommandBuffer* GetCommandBuffer() { return vulkanCommandBuffer; }
		inline VulkanImage* GetVulkanImageSubsystem() { return vulkanImage; }
		inline vk::raii::Device& GetLogicalDevice() { return vulkanContext->GetDevice().logicalDevice; }

		inline RenderPassManager* GetRenderPassManager() { return m_RenderPassManager.get(); }
		inline Rendergraph* GetRendergraph() { return m_Rendergraph.get(); }
	};

}