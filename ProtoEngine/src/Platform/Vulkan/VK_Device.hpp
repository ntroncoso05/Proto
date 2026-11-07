#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>

namespace Proto
{

	class VulkanDevice
	{
	public:
		VulkanDevice(const std::vector<const char*>& requiredExtensions, const std::vector<const char*>& optionalExtensions = {});
		~VulkanDevice() = default;

		vk::raii::PhysicalDevice physicalDevice = nullptr;
		vk::raii::Device         logicalDevice = nullptr;
		uint32_t                 queueIndex = ~0;

		vk::raii::Queue graphicsQueue = nullptr;
		vk::raii::Queue presentQueue = nullptr;
		vk::raii::Queue computeQueue = nullptr;

		vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;

		void pickPhysicalDevice(const vk::raii::Instance& instance, const vk::raii::SurfaceKHR& surface);
		void createLogicalDevice(const vk::raii::SurfaceKHR& surface);
		uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

	private:
		std::vector<const char*> m_RequiredExtensions;
		std::vector<const char*> m_OptionalExtensions;
		std::vector<const char*> m_DeviceExtensions;

		bool isDeviceSuitable(const vk::raii::PhysicalDevice& device, const vk::raii::SurfaceKHR& surface);
		vk::SampleCountFlagBits getMaxUsableSampleCount();
	};

}