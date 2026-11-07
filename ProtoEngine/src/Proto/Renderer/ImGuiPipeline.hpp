#pragma once
#include <vulkan/vulkan_raii.hpp>
//#include "Core/Entity.hpp"
#include "Core/ImGuiVulkanUtil.hpp"

namespace Proto {

	class ImGuiPipeline
	{
	public:
		ImGuiPipeline() = default;
		~ImGuiPipeline() = default;

		void Init();

		// The central command execution entry point per frame tick
		void ExecuteFrame(vk::raii::CommandBuffer& cmd, uint32_t imageIndex, ImGuiVulkanUtil* imGui);
	};
}