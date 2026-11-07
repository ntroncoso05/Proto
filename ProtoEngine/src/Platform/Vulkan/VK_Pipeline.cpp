#include "protopch.hpp"
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include "Platform/Vulkan/VK_Pipeline.hpp"
#include "Platform/Vulkan/VK_Device.hpp"
#include "Platform/Vulkan/VK_Swapchain.hpp"
#include "Platform/Vulkan/VK_CommandBuffer.hpp"
#include "Platform/Vulkan/VK_Image.hpp"
#include "Platform/Vulkan/VK_Buffer.hpp"

#include "Core/ServiceLocator.hpp"
#include "ShaderConfig.hpp"
#include <filesystem>
#include <fstream>

namespace Proto
{

    std::vector<char> VulkanPipeline::readFile(const std::string& filename)
    {
        std::string finalPath = filename;
        if (!std::filesystem::exists(finalPath)) {
            finalPath = std::string(ROOT_WORKSPACE_DIR) + filename;
        }
        std::ifstream file(finalPath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + finalPath);
        }
        std::vector<char> buffer(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        return buffer;
    }

    vk::raii::ShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code)
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        vk::ShaderModuleCreateInfo createInfo{ .codeSize = code.size(), .pCode = reinterpret_cast<const uint32_t*>(code.data()) };
        return vk::raii::ShaderModule(device.logicalDevice, createInfo);
    }

    void VulkanPipeline::createGraphicsPipeline()
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();
        auto& imgSubsystem = ServiceLocator::Get<VulkanImage>();

        vk::raii::ShaderModule shaderModule = createShaderModule(readFile("assets/shaders/base.spv"));

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = shaderModule, .pName = "vertMain" };
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = shaderModule, .pName = "fragMain" };
        vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDescription,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
            .pVertexAttributeDescriptions = attributeDescriptions.data()
        };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
        vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
        vk::PipelineRasterizationStateCreateInfo rasterizer{ .depthClampEnable = vk::False, .rasterizerDiscardEnable = vk::False, .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eBack, .frontFace = vk::FrontFace::eCounterClockwise, .lineWidth = 1.0f };
        vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = device.msaaSamples };
        vk::PipelineDepthStencilStateCreateInfo depthStencil{ .depthTestEnable = vk::True, .depthWriteEnable = vk::True, .depthCompareOp = vk::CompareOp::eLess };
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{ .blendEnable = vk::False, .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA };
        vk::PipelineColorBlendStateCreateInfo colorBlending{ .logicOpEnable = vk::False, .attachmentCount = 1, .pAttachments = &colorBlendAttachment };

        std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data() };

        // 🟩 STEP 1: MANUALLY DEFINE THE BINDINGS REQUIRED BY BASE.SLANG
        std::array<vk::DescriptorSetLayoutBinding, 2> baseBindings = {
            vk::DescriptorSetLayoutBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex
            },
            vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            }
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = static_cast<uint32_t>(baseBindings.size()),
            .pBindings = baseBindings.data()
        };

        // Compile and store the local layout within our class variable instance tracking
        baseDescriptorSetLayout = vk::raii::DescriptorSetLayout(device.logicalDevice, layoutInfo);

        vk::DescriptorSetLayout activeSetLayouts[] = { *baseDescriptorSetLayout };

        // 🟩 2. MAP THE EXACT 64-BYTE ALIGNED MATERIAL CONSTANTS BLOCK
        // This fixes the validation error regarding missing push constant ranges!
        vk::PushConstantRange basePushConstantsRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
            .offset = 0,
            .size = sizeof(float) * 16 // Explicit 16-byte range configuration perfectly clears the validation error!
        };

        // Compile layout tracking handle cleanly using the base range
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            .setLayoutCount = 1,
            .pSetLayouts = activeSetLayouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &basePushConstantsRange // 🟩 Map the base range, NOT the PBR struct block!
        };


        pipelineLayout = vk::raii::PipelineLayout(device.logicalDevice, pipelineLayoutInfo);

        VkFormat vkColorFormat = static_cast<VkFormat>(swapChain.swapChainSurfaceFormat.format);
        VkFormat vkDepthFormat = static_cast<VkFormat>(imgSubsystem.FindDepthFormat());

        VkPipelineRenderingCreateInfo renderingCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &vkColorFormat,
            .depthAttachmentFormat = vkDepthFormat
        };

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &renderingCreateInfo,
            .stageCount = 2,
            .pStages = reinterpret_cast<const VkPipelineShaderStageCreateInfo*>(shaderStages),
            .pVertexInputState = reinterpret_cast<const VkPipelineVertexInputStateCreateInfo*>(&vertexInputInfo),
            .pInputAssemblyState = reinterpret_cast<const VkPipelineInputAssemblyStateCreateInfo*>(&inputAssembly),
            .pViewportState = reinterpret_cast<const VkPipelineViewportStateCreateInfo*>(&viewportState),
            .pRasterizationState = reinterpret_cast<const VkPipelineRasterizationStateCreateInfo*>(&rasterizer),
            .pMultisampleState = reinterpret_cast<const VkPipelineMultisampleStateCreateInfo*>(&multisampling),
            .pDepthStencilState = reinterpret_cast<const VkPipelineDepthStencilStateCreateInfo*>(&depthStencil),
            .pColorBlendState = reinterpret_cast<const VkPipelineColorBlendStateCreateInfo*>(&colorBlending),
            .pDynamicState = reinterpret_cast<const VkPipelineDynamicStateCreateInfo*>(&dynamicState),
            .layout = static_cast<VkPipelineLayout>(*pipelineLayout)
        };

        VkPipeline rawPipeline = VK_NULL_HANDLE;
        if (device.logicalDevice.getDispatcher()->vkCreateGraphicsPipelines(static_cast<VkDevice>(*device.logicalDevice), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &rawPipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to compile graphics pipeline!");
        }

        graphicsPipeline = vk::raii::Pipeline(device.logicalDevice, rawPipeline);
    }
}