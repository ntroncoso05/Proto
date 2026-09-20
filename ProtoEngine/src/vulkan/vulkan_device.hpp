#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

namespace Proto
{
	class VulkanDevice
	{
	public:
		std::vector<const char*> requiredDeviceExtension = {
		vk::KHRSwapchainExtensionName };

		// Vulkan instance and surface
		vk::raii::Instance& instance;
		vk::raii::SurfaceKHR& surface;

		// Vulkan device
		vk::raii::PhysicalDevice physicalDevice = nullptr;
		vk::raii::Device         logicalDevice = nullptr;
		uint32_t                 queueIndex = ~0;

		// Vulkan queues
		vk::raii::Queue graphicsQueue = nullptr;
		vk::raii::Queue presentQueue = nullptr;
		vk::raii::Queue computeQueue = nullptr;

		vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

		/**
		 * @brief Constructor.
		 * @param instance The Vulkan instance.
		 * @param surface The Vulkan surface.
		 * @param requiredExtensions The required device extensions.
		 * @param optionalExtensions The optional device extensions.
		*/
		VulkanDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface,
			const std::vector<const char*>& requiredExtensions,
			const std::vector<const char*>& optionalExtensions = {});

		/**
		 * @brief Destructor.
		*/
		~VulkanDevice();

		void pickPhysicalDevice();
		void createLogicalDevice();
		uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
	private:

		// Device extensions
		std::vector<const char*> requiredExtensions;
		std::vector<const char*> optionalExtensions;
		std::vector<const char*> deviceExtensions;

		// Private methods
		bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice);
		vk::SampleCountFlagBits getMaxUsableSampleCount();
	};
}