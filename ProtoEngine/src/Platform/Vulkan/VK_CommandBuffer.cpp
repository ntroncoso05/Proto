#include "protopch.hpp"
#include "VK_CommandBuffer.hpp"
#include "VK_Device.hpp"
#include "VK_Swapchain.hpp"
#include "VK_Image.hpp"
#include "VK_Renderer.hpp"
#include "Core/ServiceLocator.hpp"
#include "Core/ComponentTypes.hpp"
#include "Renderer/RendererPipeline.hpp"

namespace Proto
{

    vk::raii::CommandBuffer VulkanCommandBuffer::BeginSingleTimeCommands()
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 };
        vk::raii::CommandBuffer commandBuffer = std::move(vk::raii::CommandBuffers(device.logicalDevice, allocInfo).front());

        vk::CommandBufferBeginInfo beginInfo{ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit };
        commandBuffer.begin(beginInfo);
        return commandBuffer;
    }

    void VulkanCommandBuffer::EndSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer)
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        commandBuffer.end();
        // 2. 🟩 CREATE A TEMPORARY FENCE TO TRACK ONLY THIS WORKLOAD Fences are lightweight and let us wait on single execution tasks without queue stalls!
        vk::raii::Fence synchronizationFence(device.logicalDevice, vk::FenceCreateInfo{});

        vk::SubmitInfo submitInfo{
            .commandBufferCount = 1,
            .pCommandBuffers = &*commandBuffer
        };

        // 3. 🟩 SUBMIT TO QUEUE WITH THE ASSOCIATED FENCE
        device.graphicsQueue.submit(submitInfo, *synchronizationFence);

        // 4. 🟩 WAIT ONLY FOR THIS SPECIFIC FENCE TO BE SIGNALED BY THE GPU This protects you from the vk::DeviceLostError driver timeout crashes completely!
        auto waitResult = device.logicalDevice.waitForFences(*synchronizationFence, vk::True, UINT64_MAX);
        if (waitResult != vk::Result::eSuccess) {
            std::cerr << "[CRITICAL]: Staging command execution sync timeout!" << std::endl;
        }
    }

    void VulkanCommandBuffer::CreateDescriptorSetLayout()
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();

        vk::DescriptorSetLayoutBinding uboBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        };

        vk::DescriptorSetLayoutCreateInfo layoutInfo{
            .bindingCount = 1,
            .pBindings = &uboBinding
        };

        descriptorSetLayout = device.logicalDevice.createDescriptorSetLayout(layoutInfo);
    }

    void VulkanCommandBuffer::CreateDescriptorSets()
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();

        std::vector<vk::DescriptorSetLayout> layouts(VulkanSwapChain::MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool = *descriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
            .pSetLayouts = layouts.data()
        };

        descriptorSets.clear();
        descriptorSets = device.logicalDevice.allocateDescriptorSets(allocInfo);

        // FIXED 1: WRITE TO UNIFORM BLOCK DESCRIPTORS This links the uniform buffers directly to Set 0 and stops the validation errors!
        for (size_t i = 0; i < VulkanSwapChain::MAX_FRAMES_IN_FLIGHT; i++)
        {
            // Safeguard check to ensure our allocation parallel tracks are valid
            if (i >= uniformBuffers.size()) continue;

            vk::DescriptorBufferInfo bufferInfo{
                .buffer = *uniformBuffers[i], // Dereference the RAII wrapper handle safely
                .offset = 0,
                .range = sizeof(PBRUniformBufferObject)
            };

            vk::WriteDescriptorSet descriptorWrite{
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &bufferInfo
            };

            device.logicalDevice.updateDescriptorSets({ descriptorWrite }, {});
        }

        std::cout << "[ENGINE DEBUG]: Global Set 0 allocated and bound successfully." << std::endl;
    }

    void VulkanCommandBuffer::CreateUniformBuffers()
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        vk::DeviceSize bufferSize = sizeof(PBRUniformBufferObject);

        // FIXED 1: Use clear & reserve instead of resize() to avoid move/copy constraints!
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();

        uniformBuffers.reserve(VulkanSwapChain::MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory.reserve(VulkanSwapChain::MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped.reserve(VulkanSwapChain::MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < VulkanSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            // Create the individual resource handles safely
            auto [buffer, memory] = CreateBuffer(
                bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );

            // FIXED 2: Map the memory pointer BEFORE moving the handle out of local scope!
            void* mappedPtr = memory.mapMemory(0, bufferSize);

            // FIXED 3: Use push_back with std::move to transfer ownership safely into the arrays
            uniformBuffers.push_back(std::move(buffer));
            uniformBuffersMemory.push_back(std::move(memory));
            uniformBuffersMapped.push_back(mappedPtr);
        }

        std::cout << "[ENGINE DEBUG]: Parallel uniform buffers allocated and mapped safely." << std::endl;
    }

    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory> VulkanCommandBuffer::CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();

        vk::BufferCreateInfo bufferInfo{
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };

        vk::raii::Buffer buffer(device.logicalDevice, bufferInfo);

        vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, properties)
        };

        vk::raii::DeviceMemory bufferMemory(device.logicalDevice, allocInfo);
        buffer.bindMemory(*bufferMemory, 0);

        return { std::move(buffer), std::move(bufferMemory) };
    }

    void VulkanCommandBuffer::CreateCommandBuffers() {
        auto& device = ServiceLocator::Get<VulkanDevice>();
        vk::CommandBufferAllocateInfo allocInfo{ .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = VulkanSwapChain::MAX_FRAMES_IN_FLIGHT };
        commandBuffers = vk::raii::CommandBuffers(device.logicalDevice, allocInfo);
    }

    void VulkanCommandBuffer::CreateCommandPool()
    {
        auto& device = ServiceLocator::Get<VulkanDevice>();

        // 1. Create the standard graphics command pool...
        vk::CommandPoolCreateInfo poolInfo{
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = device.queueIndex
        };
        commandPool = vk::raii::CommandPool(device.logicalDevice, poolInfo);

        // 2. FIX: ALLOCATE THE MASTER DESCRIPTOR POOL HERE Configure sizes for both your global dynamic uniform buffers and texture samplers
        std::array<vk::DescriptorPoolSize, 2> poolSizes = {
            vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 10 },
            vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 100 }
        };

        vk::DescriptorPoolCreateInfo descPoolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, // Crucial for asset reloads
            .maxSets = 50, // Total maximum allocated set count
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data()
        };

        // Initialize your member variable descriptorPool handle!
        descriptorPool = vk::raii::DescriptorPool(device.logicalDevice, descPoolInfo);

        std::cout << "[ENGINE DEBUG]: Unified Global Descriptor Pool allocated successfully." << std::endl;
    }
}