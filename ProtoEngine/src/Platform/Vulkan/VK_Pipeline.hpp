#pragma once
#include <vulkan/vulkan_raii.hpp>

namespace Proto
{
	class VulkanCommandBuffer;
	class VulkanPipeline
	{
	public:
		vk::raii::Pipeline            graphicsPipeline{ nullptr };
		vk::raii::PipelineLayout      pipelineLayout{ nullptr };

		VulkanPipeline() = default;
		/**
		 * @brief Destructor.
		*/
		~VulkanPipeline() = default;

		//bool createPBRPipeline();
		void createGraphicsPipeline();
		vk::raii::ShaderModule createShaderModule(const std::vector<char>& code);
		std::vector<char> readFile(const std::string& filename);

		vk::raii::DescriptorSetLayout baseDescriptorSetLayout{ nullptr };

		// PBR pipeline
		vk::raii::PipelineLayout pbrPipelineLayout = nullptr;
		vk::raii::Pipeline pbrPipeline = nullptr;
	private:
		// Storing this here guarantees its memory address remains valid long after createGraphicsPipeline() returns!
		vk::PipelineRenderingCreateInfo m_PersistentRenderingInfo{};
		vk::Format m_CachedDepthFormat = vk::Format::eUndefined;
		vk::Format m_CachedColorFormat = vk::Format::eUndefined;
	};
}