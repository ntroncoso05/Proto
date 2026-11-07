#include "protopch.hpp"
#include "VK_Context.hpp"
#include "VK_Device.hpp"

#include <iostream>

const std::vector<char const*> validationLayers = {
	"VK_LAYER_KHRONOS_validation" };

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace Proto
{
	VulkanContext::VulkanContext(GLFWwindow* windowHandle, const std::vector<const char*>& _requiredExtensions) : m_Window(windowHandle), m_RequiredExtensions(_requiredExtensions)
	{}

	void VulkanContext::Init()
	{
		m_VulkanDevice = std::make_unique<VulkanDevice>(m_RequiredExtensions);
		CreateInstance();
		SetupDebugMessenger();
	}

	void VulkanContext::SwapBuffers()
	{}

	static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
	{
		if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
		{
			std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
		}

		return vk::False;
	}

	void VulkanContext::CreateInstance()
	{
		constexpr vk::ApplicationInfo appInfo{ .pApplicationName = "Proto",
											  .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
											  .pEngineName = "Proto Engine",
											  .engineVersion = VK_MAKE_VERSION(1, 0, 0),
											  .apiVersion = vk::ApiVersion14 };

		// Get the required layers
		std::vector<char const*> requiredLayers;
		if (enableValidationLayers)
		{
			requiredLayers.assign(validationLayers.begin(), validationLayers.end());
		}

		// Check if the required layers are supported by the Vulkan implementation.
		auto layerProperties = m_Context.enumerateInstanceLayerProperties();
		auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
			[&layerProperties](auto const& requiredLayer) {
				return std::ranges::none_of(layerProperties,
					[requiredLayer](auto const& layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
			});
		if (unsupportedLayerIt != requiredLayers.end())
		{
			throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
		}

		if (enableValidationLayers)
		{
			m_RequiredExtensions.push_back(vk::EXTDebugUtilsExtensionName);
		}

		// Check if the required extensions are supported by the Vulkan implementation.
		auto extensionProperties = m_Context.enumerateInstanceExtensionProperties();
		auto unsupportedPropertyIt =
			std::ranges::find_if(m_RequiredExtensions,
				[&extensionProperties](auto const& requiredExtension) {
					return std::ranges::none_of(extensionProperties,
						[requiredExtension](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
				});
		if (unsupportedPropertyIt != m_RequiredExtensions.end())
		{
			throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
		}

		vk::InstanceCreateInfo createInfo{ .pApplicationInfo = &appInfo,
										  .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
										  .ppEnabledLayerNames = requiredLayers.data(),
										  .enabledExtensionCount = static_cast<uint32_t>(m_RequiredExtensions.size()),
										  .ppEnabledExtensionNames = m_RequiredExtensions.data() };
		m_Instance = vk::raii::Instance(m_Context, createInfo);
	}

	void VulkanContext::SetupDebugMessenger()
	{
		if (!enableValidationLayers)
			return;

		vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
		vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
			vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{ .messageSeverity = severityFlags,
																			  .messageType = messageTypeFlags,
																			  .pfnUserCallback = &DebugCallback };
		m_DebugMessenger = m_Instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void VulkanContext::CreateDevice()
	{
		m_VulkanDevice->pickPhysicalDevice(m_Instance, m_Surface);
		m_VulkanDevice->createLogicalDevice(m_Surface);
	}

}