#include "protopch.hpp"
#include "ImGuiVulkanUtil.hpp"

#include "Core/ServiceLocator.hpp"
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Core/Application.hpp"
#include "Core/SceneSerializer.hpp"
#include "Core/Scene.hpp"
#include "Core/PhysicsSystem.hpp"
#include "Platform/Vulkan/VK_Pipeline.hpp"
#include "Platform/Vulkan/VK_BufferUtils.hpp"
#include "Renderer/RendererPipeline.hpp"
#include "ShaderConfig.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <format>
#include <filesystem>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>

namespace Proto
{
    std::vector<char> readFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open file!");
        }
        std::vector<char> buffer(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        file.close();
        return buffer;
    }

    ImGuiVulkanUtil::ImGuiVulkanUtil(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
        vk::raii::Queue& graphicsQueue, uint32_t graphicsQueueFamily, VulkanCommandBuffer* commandBuffer, VulkanImage* image)
        : device(&device), physicalDevice(&physicalDevice),
        graphicsQueue(&graphicsQueue), graphicsQueueFamily(graphicsQueueFamily), vulkanCommandBuffer(commandBuffer), vulkanImage(image)
    {
        renderingInfo.colorAttachmentCount = 1;
        vk::Format formats[] = { colorFormat };
        renderingInfo.pColorAttachmentFormats = &colorFormat;
    }

    ImGuiVulkanUtil::~ImGuiVulkanUtil() {
        // Wait for device to finish operations before destroying resources
        // NOTE: waitIdle() is acceptable in destructors/cleanup code but should NEVER be used
        // in the main rendering loop as it causes severe performance issues. For frame
        // synchronization, use fences and semaphores instead.
        if (device) {
            device->waitIdle();
        }

        // All resources are automatically cleaned up by their destructors. No manual cleanup needed
        // ImGui context is destroyed separately
    }

    void ImGuiVulkanUtil::init(float width, float height) {
        // Initialize ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Configure ImGui
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable docking

        // Inform ImGui that we support the new texture update protocol (v1.92+)
        // This enables support for dynamic font textures and multiple texture atlases
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

        // Set display size
        io.DisplaySize = ImVec2(width, height);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

        // Set up style
        vulkanStyle = ImGui::GetStyle();
        vulkanStyle.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
        vulkanStyle.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
        vulkanStyle.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
        vulkanStyle.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
        vulkanStyle.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

        // Apply default style
        setStyle(0);
    }

    void ImGuiVulkanUtil::setStyle(uint32_t index) {
        ImGuiStyle& style = ImGui::GetStyle();

        switch (index) {
            case 0:
                // Custom Vulkan style
                style = vulkanStyle;
                break;
            case 1:
                // Classic style
                ImGui::StyleColorsClassic();
                break;
            case 2:
                // Dark style
                ImGui::StyleColorsDark();
                break;
            case 3:
                // Light style
                ImGui::StyleColorsLight();
                break;
        }
    }

    void ImGuiVulkanUtil::initResources() {
        // Configure texture sampling parameters for optimal text rendering
        // These settings directly impact text quality and performance
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;                    // Smooth scaling when magnified
        samplerInfo.minFilter = vk::Filter::eLinear;                    // Smooth scaling when minified
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;        // Smooth transitions between mip levels
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;  // Prevent texture wrapping
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;  // Clean edge handling
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;  // 3D consistency
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;   // White border for clamped areas

        sampler = device->createSampler(samplerInfo);                   // Create the GPU sampler object

        // Create descriptor pool for shader resource binding. Descriptors provide the interface between shaders and GPU resources
        vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 1 };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;     // Allow individual descriptor set freeing
        poolInfo.maxSets = 4;                                                      // Maximum number of descriptor sets
        poolInfo.poolSizeCount = 1;                                                // Number of pool size specifications
        poolInfo.pPoolSizes = &poolSize;                                           // Pool size configuration

        descriptorPool = device->createDescriptorPool(poolInfo);                   // Create descriptor pool

        // Create descriptor set layout defining shader resource interface. This layout must match the binding declarations in the ImGui shaders
        vk::DescriptorSetLayoutBinding binding{};
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;        // Combined texture and sampler
        binding.descriptorCount = 1;                                               // Single texture binding
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;                   // Used in fragment shader
        binding.binding = 0;                                                       // Shader binding point 0

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;                                               // Number of bindings in layout
        layoutInfo.pBindings = &binding;                                           // Binding configuration array

        descriptorSetLayout = device->createDescriptorSetLayout(layoutInfo);       // Create layout object

        // Allocate descriptor set from pool using the defined layout. This creates the actual binding that connects GPU resources to shaders
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = *descriptorPool;                                // Source pool for allocation
        allocInfo.descriptorSetCount = 1;                                          // Number of sets to allocate
        vk::DescriptorSetLayout layouts[] = { *descriptorSetLayout };                // Layout template array
        allocInfo.pSetLayouts = layouts;                                           // Layout configuration

        descriptorSet = std::move(device->allocateDescriptorSets(allocInfo).front()); // Allocate and store set

        // Create pipeline cache
        vk::PipelineCacheCreateInfo pipelineCacheInfo{};
        pipelineCache = device->createPipelineCache(pipelineCacheInfo);

        // Create pipeline layout
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PushConstBlock);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        vk::DescriptorSetLayout setLayouts[] = { *descriptorSetLayout };
        pipelineLayoutInfo.pSetLayouts = setLayouts;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device->createPipelineLayout(pipelineLayoutInfo);

        // 1. Build a root-relative path layout string cleanly
        std::string filePath = std::format("assets/shaders/ImGui.spv");

        // 2. ABSOLUTE PATH FALLBACK: If running from a build folder, prepend the generated root mapping path string
        if (!std::filesystem::exists(filePath)) {
            filePath = std::string(ROOT_WORKSPACE_DIR) + filePath;
        }

        auto& pipelineX = ServiceLocator::Get<VulkanPipeline>();
        vk::raii::ShaderModule shaderModule = pipelineX.createShaderModule(readFile(filePath));
        // 3. Define Vertex Input Attributes matching ImDrawVert
        vk::VertexInputBindingDescription bindingDesc{
            .binding = 0,
            .stride = sizeof(ImDrawVert),
            .inputRate = vk::VertexInputRate::eVertex
        };

        std::array<vk::VertexInputAttributeDescription, 3> attributeDescs = {
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos)},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)}
        };

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDesc,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescs.size()),
            .pVertexAttributeDescriptions = attributeDescs.data()
        };

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
            .topology = vk::PrimitiveTopology::eTriangleList
        };

        // 4. Viewport and Scissor states are managed dynamically in drawFrame
        vk::PipelineViewportStateCreateInfo viewportState{
            .viewportCount = 1,
            .scissorCount = 1
        };

        vk::PipelineRasterizationStateCreateInfo rasterizer{
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eNone, // Don't cull ImGui windows
            .frontFace = vk::FrontFace::eCounterClockwise,
            .lineWidth = 1.0f
        };

        // 5. CRITICAL FIX: Match the 8x MSAA requirements of your render pass
        vk::PipelineMultisampleStateCreateInfo multisampling{
            .rasterizationSamples = vk::SampleCountFlagBits::e1 // Must be e8 to match your 3D buffer target
        };

        // 6. UI Alpha Blending State
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = vk::True,
            .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        vk::PipelineColorBlendStateCreateInfo colorBlending{
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
            .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
            .pDynamicStates = dynamicStates.data()
        };

        // 7. Configure Dynamic Rendering Format Structure (No RenderPass architecture)
        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat // Formatted correctly as eB8G8R8A8Unorm
        };

        // 8. Pack Stage Infos using your compiled shader modules
        std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = *shaderModule,
                .pName = "vertexMain"
            },
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = *shaderModule,
                .pName = "fragmentMain"
            }
        };

        vk::GraphicsPipelineCreateInfo pipelineInfo{
            .pNext = &pipelineRenderingInfo, // Inject dynamic rendering parameters
            .stageCount = static_cast<uint32_t>(shaderStages.size()),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicStateInfo,
            .layout = *pipelineLayout
        };

        // 9. Construct the pipeline object safely
        pipeline = device->createGraphicsPipeline(pipelineCache, pipelineInfo);
    }

    void ImGuiVulkanUtil::updateTexture(ImTextureData* tex) {
        auto& image = ServiceLocator::Get<VulkanImage>();
        auto& cmdBuffer = ServiceLocator::Get<VulkanCommandBuffer>();

        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
            int texWidth = tex->Width;
            int texHeight = tex->Height;
            unsigned char* fontData = (unsigned char*)tex->Pixels;

            if (!fontData) return;

            vk::DeviceSize uploadSize = texWidth * texHeight * tex->BytesPerPixel;
            vk::Format format = (tex->BytesPerPixel == 4) ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8Unorm;

            if (tex->Status == ImTextureStatus_WantCreate) {
                // Create optimized GPU image for texture storage
                vk::Extent3D extent{ static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

                // Create the texture image
                std::tie(fontImage, fontImageMemory) = vulkanImage->CreateImage(extent.width,
                    extent.height,
                    1,
                    vk::SampleCountFlagBits::e1,
                    format,
                    vk::ImageTiling::eOptimal,
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);

                 // Capture and assign the returned ImageView to your class member variable
                fontImageView = image.CreateImageView(*fontImage, format, vk::ImageAspectFlagBits::eColor, 1);

                // Update the descriptor set NOW that fontImageView contains a valid handle
                vk::DescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                imageInfo.imageView = *fontImageView; // Valid, populated handle
                imageInfo.sampler = *sampler;

                vk::WriteDescriptorSet writeSet{};
                writeSet.dstSet = *descriptorSet;
                writeSet.descriptorCount = 1;
                writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                writeSet.pImageInfo = &imageInfo;
                writeSet.dstBinding = 0;

                device->updateDescriptorSets(writeSet, {});
            }

            // Create staging buffer
            auto [stagingBuffer, stagingBufferMemory] =
                cmdBuffer.CreateBuffer(uploadSize, vk::BufferUsageFlagBits::eTransferSrc,
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            // Copy data to staging buffer
            void* data = stagingBufferMemory.mapMemory(0, uploadSize);
            memcpy(data, fontData, uploadSize);
            stagingBufferMemory.unmapMemory();

            // Copy data from staging buffer to texture image
            vk::raii::CommandBuffer commandBuffer = vulkanCommandBuffer->BeginSingleTimeCommands();
            image.TransitionImageLayout(commandBuffer, fontImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
            BufferUtils::CopyBufferToImage(commandBuffer, stagingBuffer, fontImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
            image.TransitionImageLayout(commandBuffer, fontImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);
            cmdBuffer.EndSingleTimeCommands(std::move(commandBuffer));

            // Store descriptor set handle as the ImTextureID. In this implementation, we use a single descriptor set for the font atlas
            tex->SetTexID((ImTextureID)(intptr_t)(VkDescriptorSet)*descriptorSet);
            tex->SetStatus(ImTextureStatus_OK);
        }
    }

    bool ImGuiVulkanUtil::newFrame(vk::Extent2D swapChainExtent)
    {
        ImGuiIO& io = ImGui::GetIO();

        // Fetch current display boundaries from your running context window metrics
        float width = static_cast<float>(swapChainExtent.width);
        float height = static_cast<float>(swapChainExtent.height);

        // Update display sizes dynamically every single frame pass
        io.DisplaySize = ImVec2(width, height);

        // CRITICAL: Feed engine delta time updates directly into the ImGui state pipeline
        // Use the current frame time or calculate an absolute execution step
        float currentTime = (float)glfwGetTime();
        static float lastTime = 0.0f;
        io.DeltaTime = (lastTime > 0.0f) ? (currentTime - lastTime) : (1.0f / 60.0f);
        lastTime = currentTime;

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        return true;
    }

    void ImGuiVulkanUtil::updateBuffers(uint32_t frameIndex)
    {
        auto& cmdBuffer = ServiceLocator::Get<VulkanCommandBuffer>();

        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->CmdListsCount == 0) {
            return;
        }

        // 1. Resize this frame slot's Vertex Buffer if needed (No waitIdle needed!)
        if (drawData->TotalVtxCount > vertexCounts[frameIndex]) {
            int32_t newCount = drawData->TotalVtxCount + 500;
            vk::DeviceSize allocSize = newCount * sizeof(ImDrawVert);

            auto [vtxBuf, vtxMem] = cmdBuffer.CreateBuffer(
                allocSize, vk::BufferUsageFlagBits::eVertexBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            vertexBuffers[frameIndex] = std::move(vtxBuf);
            vertexBufferMemories[frameIndex] = std::move(vtxMem);
            vertexCounts[frameIndex] = newCount;
        }

        // 2. Resize this frame slot's Index Buffer if needed
        if (drawData->TotalIdxCount > indexCounts[frameIndex]) {
            int32_t newCount = drawData->TotalIdxCount + 1000;
            vk::DeviceSize allocSize = newCount * sizeof(ImDrawIdx);

            auto [idxBuf, idxMem] = cmdBuffer.CreateBuffer(
                allocSize, vk::BufferUsageFlagBits::eIndexBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

            indexBuffers[frameIndex] = std::move(idxBuf);
            indexBufferMemories[frameIndex] = std::move(idxMem);
            indexCounts[frameIndex] = newCount;
        }

        // 3. Upload data into this frame slot's buffers
        vk::DeviceSize vertexBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
        vk::DeviceSize indexBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

        ImDrawVert* vtxDst = static_cast<ImDrawVert*>(vertexBufferMemories[frameIndex].mapMemory(0, vertexBufferSize));
        ImDrawIdx* idxDst = static_cast<ImDrawIdx*>(indexBufferMemories[frameIndex].mapMemory(0, indexBufferSize));

        for (int n = 0; n < drawData->CmdListsCount; n++) {
            const ImDrawList* cmdList = drawData->CmdLists[n];
            memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmdList->VtxBuffer.Size;
            idxDst += cmdList->IdxBuffer.Size;
        }
        vertexBufferMemories[frameIndex].unmapMemory();
        indexBufferMemories[frameIndex].unmapMemory();
    }

    void ImGuiVulkanUtil::drawFrame(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex, vk::Extent2D swapChainExtent) {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->CmdListsCount == 0) {
            return;
        }

        // Process dynamic texture updates (v1.92+ RendererHasTextures protocol)
        // This handles font atlas regeneration and any user-provided textures
        if (drawData->Textures) {
            for (int n = 0; n < drawData->Textures->Size; n++) {
                ImTextureData* tex = (*drawData->Textures)[n];
                if (tex->Status != ImTextureStatus_OK) {
                    updateTexture(tex);
                }
            }
        }

        uint32_t renderWidth = swapChainExtent.width;
        uint32_t renderHeight = swapChainExtent.height;

        // Bind your pipeline, viewports, dynamic states, and draw indices below as usual...
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(renderWidth);
        viewport.height = static_cast<float>(renderHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        commandBuffer.setViewport(0, viewport);

        // Convert from ImGui coordinates into NDC via a simple scale/translate
        pushConstBlock.scale = glm::vec2(2.0f / drawData->DisplaySize.x, 2.0f / drawData->DisplaySize.y);
        pushConstBlock.translate = glm::vec2(-1.0f);
        commandBuffer.pushConstants(*pipelineLayout, vk::ShaderStageFlagBits::eVertex,
            0, sizeof(PushConstBlock), &pushConstBlock);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, { *descriptorSet }, {});
        commandBuffer.bindVertexBuffers(0, *vertexBuffers[frameIndex], {0});
        commandBuffer.bindIndexBuffer(indexBuffers[frameIndex], 0, vk::IndexType::eUint16);

        int vertexOffset = 0;
        int indexOffset = 0;

        for (int i = 0; i < drawData->CmdListsCount; i++) {
            const ImDrawList* cmdList = drawData->CmdLists[i];

            for (int j = 0; j < cmdList->CmdBuffer.Size; j++) {
                const ImDrawCmd* pcmd = &cmdList->CmdBuffer[j];

                // Clip per draw call
                vk::Rect2D scissor{};
                scissor.offset.x = std::max(static_cast<int32_t>(pcmd->ClipRect.x), 0);
                scissor.offset.y = std::max(static_cast<int32_t>(pcmd->ClipRect.y), 0);
                scissor.extent.width = static_cast<uint32_t>(pcmd->ClipRect.z - pcmd->ClipRect.x);
                scissor.extent.height = static_cast<uint32_t>(pcmd->ClipRect.w - pcmd->ClipRect.y);
                commandBuffer.setScissor(0, scissor);

                // Bind font (and any UI) textures for this draw
                // The TexID now stores the actual descriptor set handle (VkDescriptorSet)
                VkDescriptorSet texHandle = (VkDescriptorSet)pcmd->GetTexID();
                if (texHandle) {
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                        *pipelineLayout, 0, { vk::DescriptorSet(texHandle) }, {});
                }
                else {
                    // Fallback to default font if no specific texture is bound
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                        *pipelineLayout, 0, { *descriptorSet }, {});
                }

                // Issue indexed draw for this UI batch
                commandBuffer.drawIndexed(pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
                indexOffset += pcmd->ElemCount;
            }

            vertexOffset += cmdList->VtxBuffer.Size;
        }
    }

    void ImGuiVulkanUtil::drawGUI()
    {
        auto* win = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        static bool hKeyWasDown = false;
        bool hKeyDown = (win && glfwGetKey(win, GLFW_KEY_H) == GLFW_PRESS);
        if (hKeyDown && !hKeyWasDown && !ImGui::GetIO().WantTextInput) {
            showViewportOverlays = !showViewportOverlays;
        }
        hKeyWasDown = hKeyDown;

        //ImGui::Separator();
        static bool f11WasDown = false;
        bool f11Down = (win && glfwGetKey(win, GLFW_KEY_F11) == GLFW_PRESS);
        if (f11Down && !f11WasDown && !ImGui::GetIO().WantTextInput) {
            VulkanRenderer::Get().RequestScreenshot();
        }
        f11WasDown = f11Down;

        // ENABLE FULL-WINDOW TRANSPARENT DOCKSPACE
        if (ImGui::BeginMainMenuBar())
        {
            static bool openSaveAsModal = false;
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    SceneSerializer::Serialize(SceneSerializer::GetActiveScenePath());
                }
                if (ImGui::MenuItem("Save Scene As...")) {
                    openSaveAsModal = true;
                }
                if (ImGui::BeginMenu("Load Scene"))
                {
                    namespace fs = std::filesystem;
                    fs::path scenesDir = fs::exists("assets/scenes") ? "assets/scenes" : "../../../assets/scenes";
                    if (fs::exists(scenesDir)) {
                        for (const auto& entry : fs::directory_iterator(scenesDir)) {
                            if (entry.path().extension() != ".json") continue;
                            std::string stem = entry.path().stem().string();
                            if (stem.find("_temp_play_state") != std::string::npos) continue;

                            bool isCurrent = (entry.path().filename() == fs::path(SceneSerializer::GetActiveScenePath()).filename());
                            if (ImGui::MenuItem(stem.c_str(), nullptr, isCurrent)) {
                                SceneSerializer::Deserialize(entry.path().generic_string());
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Reload Current Scene", "Ctrl+O")) {
                    SceneSerializer::Deserialize(SceneSerializer::GetActiveScenePath());
                }
                ImGui::EndMenu();
            }

            if (openSaveAsModal) {
                ImGui::OpenPopup("Save Scene As");
                openSaveAsModal = false;
            }
            if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char sceneNameBuf[64] = "NewShowroom";
                ImGui::InputText("Scene Name", sceneNameBuf, sizeof(sceneNameBuf));
                if (ImGui::Button("Save", ImVec2(120, 0))) {
                    std::string fullPath = std::format("../../../assets/scenes/{}.json", sceneNameBuf);
                    SceneSerializer::Serialize(fullPath);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ServiceLocator::Contains<PhysicsSystem>() && ServiceLocator::Contains<Scene>()) {
                auto& physics = ServiceLocator::Get<PhysicsSystem>();
                auto& scene = ServiceLocator::Get<Scene>();

                ImGui::Separator();
                if (!physics.IsSimulating()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.20f, 1.0f));
                    if (ImGui::Button("Play Physics")) {
                        SceneSerializer::Serialize("../../../assets/scenes/_temp_play_state.json");
                        physics.OnSceneStart(scene);
                    }
                    ImGui::PopStyleColor();
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.18f, 0.18f, 1.0f));
                    if (ImGui::Button("Stop Physics")) {
                        physics.OnSceneStop(scene);
                        SceneSerializer::Deserialize("../../../assets/scenes/_temp_play_state.json");
                    }
                    ImGui::PopStyleColor();
                }
            }

            ImGui::Separator();
            ImGui::Checkbox("Show Gizmos (H)", &showViewportOverlays);
                        
            ImGui::Separator();
            ImGui::Checkbox("Auto-Turntable (Tab)", &autoTurntable);
            if (autoTurntable) {
                ImGui::SetNextItemWidth(80.0f);
                ImGui::SliderFloat("##SpinSpeed", &turntableSpeed, -90.0f, 90.0f, "%.0f deg/s");
            }

            ImGui::Separator();
            if (ImGui::Button("Render PNG (F11)")) {
                VulkanRenderer::Get().RequestScreenshot();
            }
            ImGui::EndMainMenuBar();
        }

        // Enable Docking across the main window
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // 3D Scene Viewport Window
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Viewport");

        // Track both hover and focus so mouse look, scroll, and WASD work whenever active in the Viewport
        ImVec2 minRegion = ImGui::GetCursorScreenPos();
        viewportMin = { minRegion.x, minRegion.y };

        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        this->viewportSize = { viewportSize.x, viewportSize.y };
        if (*sceneViewportDescriptorSet && viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            ImGui::Image(
                (ImTextureID)(intptr_t)(VkDescriptorSet)*sceneViewportDescriptorSet,
                viewportSize
            );
        }

        // Check BOTH window hover AND image item hover so dragging/scrolling inside Viewport always registers!
        isViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
            || ImGui::IsItemHovered();
        isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        // --- ImGuizmo Overlay ---
        static ImGuizmo::OPERATION currentGizmoOp = ImGuizmo::TRANSLATE;

        //auto* win = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (win && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
            if (glfwGetKey(win, GLFW_KEY_T) == GLFW_PRESS) currentGizmoOp = ImGuizmo::TRANSLATE;
            if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) currentGizmoOp = ImGuizmo::ROTATE;
            if (glfwGetKey(win, GLFW_KEY_Y) == GLFW_PRESS) currentGizmoOp = ImGuizmo::SCALE;
        }

        CameraComponent* activeCam = nullptr;
        for (auto* entity : Entity::GetActiveSceneEntities()) {
            if (auto* cam = entity->GetComponent<CameraComponent>()) {
                activeCam = cam;
                break;
            }
        }

        bool altPressed = (glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
        if (selectedEntity && selectedEntity->GetName() != "MainCamera" && !altPressed) {
            if (showViewportOverlays) {
                auto* targetTransform = selectedEntity->GetComponent<TransformComponent>();
                if (targetTransform && activeCam) {
                    ImGuizmo::SetOrthographic(false);
                    ImGuizmo::SetDrawlist();
                    ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);

                    glm::mat4 viewMatrix = activeCam->GetViewMatrix();
                    glm::mat4 projMatrix = activeCam->GetProjectionMatrix();
                    projMatrix[1][1] *= -1.0f; // Match Vulkan Y-flipped NDC space!
                    glm::mat4 modelMatrix = targetTransform->GetWorldTransformMatrix();

                    ImGuizmo::Manipulate(
                        glm::value_ptr(viewMatrix),
                        glm::value_ptr(projMatrix),
                        currentGizmoOp,
                        ImGuizmo::WORLD,
                        glm::value_ptr(modelMatrix)
                    );

                    if (ImGuizmo::IsUsing()) {
                        glm::vec3 translation, rotationEuler, scale;
                        ImGuizmo::DecomposeMatrixToComponents(
                            glm::value_ptr(modelMatrix),
                            glm::value_ptr(translation),
                            glm::value_ptr(rotationEuler),
                            glm::value_ptr(scale)
                        );

                        // If entity has a parent (like Moon_Child), convert world matrix back to local space
                        if (Entity* parent = selectedEntity->GetParent()) {
                            if (auto* parentTrans = parent->GetComponent<TransformComponent>()) {
                                glm::mat4 localMat = glm::inverse(parentTrans->GetWorldTransformMatrix()) * modelMatrix;
                                ImGuizmo::DecomposeMatrixToComponents(
                                    glm::value_ptr(localMat),
                                    glm::value_ptr(translation),
                                    glm::value_ptr(rotationEuler),
                                    glm::value_ptr(scale)
                                );
                            }
                        }

                        targetTransform->SetPosition(translation);
                        targetTransform->SetRotation(glm::quat(glm::radians(rotationEuler)));
                        targetTransform->SetScale(scale);
                    }
                }
            }
        }
        
        if (activeCam && viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            glm::mat4 proj = activeCam->GetProjectionMatrix();
            glm::mat4 viewProj = proj * activeCam->GetViewMatrix();

            auto worldToScreen = [&](const glm::vec3& worldPos, ImVec2& outScreen) -> bool {
                glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
                if (!(clip.w > 0.001f)) return false; // Behind camera
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                outScreen.x = viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
                outScreen.y = viewportMin.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;
                return true;
                };

            for (Entity* entity : Entity::GetActiveSceneEntities()) {
                auto* lightComp = entity->GetComponent<LightComponent>();
                auto* lightTrans = entity->GetComponent<TransformComponent>();
                if (!lightComp || !lightTrans) continue;

                glm::vec3 lightPos = glm::vec3(lightTrans->GetWorldTransformMatrix()[3]);
                ImVec2 screenPos;
                if (worldToScreen(lightPos, screenPos)) {
                    ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(lightComp->Color.r, lightComp->Color.g, lightComp->Color.b, 1.0f)
                    );
                    bool isSelected = (selectedEntity == entity);                    

                    if (isSelected) {
                        if (showViewportOverlays) {
                            // Draw glowing bulb circle + selection ring
                            drawList->AddCircleFilled(screenPos, 8.0f, iconColor);
                            drawList->AddCircle(
                                screenPos,
                                isSelected ? 12.0f : 9.0f,
                                isSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(20, 20, 20, 220),
                                16,
                                2.0f
                            );

                            // 1. Build an orthonormal basis from the light's forward direction
                            glm::vec3 lightForward = glm::normalize(lightTrans->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
                            glm::vec3 lightDir = glm::normalize(lightForward);
                            glm::vec3 worldUp = std::abs(lightDir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                            glm::vec3 right = glm::normalize(glm::cross(worldUp, lightDir));
                            glm::vec3 up = glm::cross(lightDir, right);

                            // Helper lambda to project and draw any 3D circle
                            auto DrawCircle3D = [&](const glm::vec3& center, float radius, ImU32 color, float thickness = 1.5f) {
                                constexpr int segments = 48;
                                ImVec2 points[segments];
                                int validCount = 0;

                                for (int i = 0; i < segments; ++i) {
                                    float angle = (static_cast<float>(i) / segments) * 2.0f * glm::pi<float>();
                                    glm::vec3 worldPt = center + (right * std::cos(angle) + up * std::sin(angle)) * radius;

                                    ImVec2 screenPt;
                                    if (worldToScreen(worldPt, screenPt)) {
                                        points[validCount++] = screenPt;
                                    }
                                }

                                if (validCount > 2) {
                                    drawList->AddPolyline(points, validCount, color, ImDrawFlags_Closed, thickness);
                                }
                                };
                            // ---------------------------------------------------------
                            // SPOT LIGHT: Outer Cone + Inner Cone Ring
                            // ---------------------------------------------------------
                            if (lightComp->Type == LightType::Spot)
                            {
                                constexpr bool CircularSpotLight = true;
                                if (CircularSpotLight) {
                                    float range = lightComp->Radius;
                                    glm::vec3 baseCenter = lightPos + lightDir * range;

                                    // Outer Cone
                                    float outerRadius = range * std::tan(glm::radians(lightComp->OuterConeAngle));
                                    DrawCircle3D(baseCenter, outerRadius, iconColor, 1.5f);

                                    // 4 Outer Side Lines
                                    glm::vec3 cardinalOffsets[4] = {
                                        right * outerRadius, -right * outerRadius,
                                        up * outerRadius,    -up * outerRadius
                                    };
                                    for (const auto& offset : cardinalOffsets) {
                                        ImVec2 edgeScreen;
                                        if (worldToScreen(baseCenter + offset, edgeScreen)) {
                                            drawList->AddLine(screenPos, edgeScreen, iconColor, 1.5f);
                                        }
                                    }

                                    // Inner Cone (Dimmed alpha to distinguish the hotspot from falloff)
                                    if (lightComp->InnerConeAngle > 0.0f && lightComp->InnerConeAngle < lightComp->OuterConeAngle) {
                                        float innerRadius = range * std::tan(glm::radians(lightComp->InnerConeAngle));
                                        ImU32 innerColor = (iconColor & 0x00FFFFFF) | 0x66000000; // ~40% opacity
                                        DrawCircle3D(baseCenter, innerRadius, innerColor, 1.2f);
                                    }
                                }
                                else
                                {
                                    glm::vec3 forward = glm::normalize(lightTrans->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
                                    glm::vec3 right = glm::normalize(lightTrans->GetRotation() * glm::vec3(1.0f, 0.0f, 0.0f));
                                    glm::vec3 up = glm::normalize(lightTrans->GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f));

                                    float coneLen = std::min(lightComp->Radius / 5.0f, 12.0f);
                                    float outerRad = coneLen * std::tan(glm::radians(lightComp->OuterConeAngle));
                                    glm::vec3 tipPos = lightPos + forward * coneLen;

                                    // Project 16 points around the cone's circular base and connect 4 rays from the light origin
                                    ImVec2 ringPts[16];
                                    bool allVisible = true;
                                    for (int s = 0; s < 16; ++s) {
                                        float angle = (static_cast<float>(s) / 16.0f) * glm::two_pi<float>();
                                        glm::vec3 rimWorld = tipPos + (right * std::cos(angle) + up * std::sin(angle)) * outerRad;
                                        if (!worldToScreen(rimWorld, ringPts[s])) {
                                            allVisible = false;
                                            break;
                                        }
                                    }

                                    if (allVisible) {
                                        ImU32 coneCol = IM_COL32(255, 220, 80, 180);
                                        for (int s = 0; s < 16; ++s) {
                                            drawList->AddLine(ringPts[s], ringPts[(s + 1) % 16], coneCol, 1.5f);
                                            if (s % 4 == 0) {
                                                drawList->AddLine(screenPos, ringPts[s], coneCol, 1.5f);
                                            }
                                        }
                                    }
                                }
                            }
                            // ---------------------------------------------------------
                            // DIRECTIONAL LIGHT: Emitter Ring + Parallel Light Rays
                            // ---------------------------------------------------------
                            else if (lightComp->Type == LightType::Directional) {
                                auto camPos = activeCam->GetPosition();
                                float gizmoRadius = glm::distance(camPos, lightPos) * 0.1f; // World-space radius of the sun disc
                                float rayLength = glm::distance(camPos, lightPos) * 0.1f;   // World-space length of the direction rays

                                // Draw the emitter disc at the light's origin
                                DrawCircle3D(lightPos, gizmoRadius, iconColor, 1.5f);

                                // Draw a central arrow + 4 perimeter rays pointing along lightDir
                                glm::vec3 rayOffsets[5] = {
                                    glm::vec3(0.0f),          // Center ray
                                    right * gizmoRadius,      // Right edge
                                    -right * gizmoRadius,     // Left edge
                                    up * gizmoRadius,         // Top edge
                                    -up * gizmoRadius         // Bottom edge
                                };

                                for (int i = 0; i < 5; ++i) {
                                    glm::vec3 startWorld = lightPos + rayOffsets[i];
                                    glm::vec3 endWorld = startWorld + lightDir * rayLength;

                                    ImVec2 startScreen, endScreen;
                                    if (worldToScreen(startWorld, startScreen) && worldToScreen(endWorld, endScreen)) {
                                        float thickness = (i == 0) ? 2.0f : 1.2f; // Make center ray slightly bolder
                                        drawList->AddLine(startScreen, endScreen, iconColor, thickness);

                                        // Add a 3D arrowhead tip on the center ray (index 0)
                                        if (i == 0) {
                                            float headSize = 0.25f;
                                            glm::vec3 headBase = endWorld - lightDir * headSize;
                                            glm::vec3 tipDirs[4] = { right, -right, up, -up };

                                            for (const auto& dir : tipDirs) {
                                                ImVec2 barbScreen;
                                                if (worldToScreen(headBase + dir * (headSize * 0.5f), barbScreen)) {
                                                    drawList->AddLine(endScreen, barbScreen, iconColor, 2.0f);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            // ---------------------------------------------------------
                            // Point LIGHT: Emitter Ring
                            // ---------------------------------------------------------
                            else if (lightComp->Type == LightType::Point) {
                                auto DrawWorldCircle = [&](const glm::vec3& axisA, const glm::vec3& axisB, float radius) {
                                    constexpr int segments = 48;
                                    ImVec2 points[segments];
                                    int validCount = 0;

                                    for (int i = 0; i < segments; ++i) {
                                        float angle = (static_cast<float>(i) / segments) * 2.0f * glm::pi<float>();
                                        glm::vec3 worldPt = lightPos + (axisA * std::cos(angle) + axisB * std::sin(angle)) * radius;

                                        ImVec2 screenPt;
                                        if (worldToScreen(worldPt, screenPt)) {
                                            points[validCount++] = screenPt;
                                        }
                                    }

                                    if (validCount > 2) {
                                        drawList->AddPolyline(points, validCount, iconColor, ImDrawFlags_Closed, 1.5f);
                                    }
                                    };

                                // Draw 3 orthogonal rings to form a wireframe sphere
                                DrawWorldCircle(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), lightComp->Radius); // XY plane
                                DrawWorldCircle(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), lightComp->Radius); // XZ plane
                                DrawWorldCircle(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), lightComp->Radius); // YZ plane
                            }
                        }
                    }

                    // Click on the light gizmo icon to select it in the Inspector
                    ImVec2 mousePos = ImGui::GetMousePos();
                    float dx = mousePos.x - screenPos.x;
                    float dy = mousePos.y - screenPos.y;
                    float distSq = (dx * dx) + (dy * dy);
                    if (isViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !(distSq > 144.0f)) {
                        selectedEntity = entity;
                    }
                }
            }

            if (selectedEntity) {
                if (showViewportOverlays) {
                    auto* boxComp = selectedEntity->GetComponent<BoxColliderComponent>();
                    auto* trans = selectedEntity->GetComponent<TransformComponent>();

                    if (boxComp && trans) {
                        glm::vec3 pos = trans->GetPosition();
                        glm::quat rot = trans->GetRotation();
                        glm::vec3 scale = trans->GetScale();

                        // Match PhysicsSystem's exact world center & scaled half-extents
                        glm::vec3 center = pos + (rot * boxComp->Offset);
                        glm::vec3 ext = boxComp->HalfExtents * scale;

                        glm::vec3 localCorners[8] = {
                            { -ext.x, -ext.y, -ext.z },{ ext.x, -ext.y, -ext.z },
                            { ext.x,  ext.y, -ext.z },{ -ext.x,  ext.y, -ext.z },
                            { -ext.x, -ext.y,  ext.z },{ ext.x, -ext.y,  ext.z },
                            { ext.x,  ext.y,  ext.z },{ -ext.x,  ext.y,  ext.z }
                        };

                        ImVec2 screenPts[8];
                        bool visible[8] = { false };
                        for (int i = 0; i < 8; ++i) {
                            glm::vec3 worldCorner = center + (rot * localCorners[i]);
                            visible[i] = worldToScreen(worldCorner, screenPts[i]);
                        }

                        // Green for solid physics colliders, Cyan for triggers
                        ImU32 boxCol = boxComp->IsTrigger
                            ? IM_COL32(0, 220, 255, 220)
                            : IM_COL32(60, 255, 90, 220);

                        constexpr int edges[12][2] = {
                            { 0,1 },{ 1,2 },{ 2,3 },{ 3,0 }, // Back face
                            { 4,5 },{ 5,6 },{ 6,7 },{ 7,4 }, // Front face
                            { 0,4 },{ 1,5 },{ 2,6 },{ 3,7 }  // Connecting struts
                        };

                        for (int e = 0; e < 12; ++e) {
                            int a = edges[e][0], b = edges[e][1];
                            if (visible[a] && visible[b]) {
                                drawList->AddLine(screenPts[a], screenPts[b], boxCol, 1.75f);
                            }
                        }
                    }
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        const auto& activeEntities = Entity::GetActiveSceneEntities();
        if (selectedEntity && std::find(activeEntities.begin(), activeEntities.end(), selectedEntity) == activeEntities.end()) {
            selectedEntity = nullptr;
        }

        // SCENE HIERARCHY PANEL
        ImGui::Begin("Scene Hierarchy");
        std::function<void(Entity*)> drawEntityNode = [&](Entity* entity) {
            if (!entity) return;

            ImGuiTreeNodeFlags flags = ((selectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0)

                | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

            if (entity->GetChildren().empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }

            bool opened = ImGui::TreeNodeEx((void*)(intptr_t)entity, flags, "%s", entity->GetName().c_str());

            if (ImGui::IsItemClicked()) {
                selectedEntity = entity;
            }

            std::string popupId = std::format("EntityContext_{}", (uintptr_t)entity);
            if (ImGui::BeginPopupContextItem(popupId.c_str()))
            {
                static int entityCounter = 1;
                auto& scene = ServiceLocator::Get<Scene>();

                if (ImGui::MenuItem("Create Child Entity"))
                {
                    Entity* child = scene.CreateEntity(std::format("Child_Entity_{}", entityCounter++), entity);
                    auto* transform = child->AddComponent<TransformComponent>();
                    transform->Initialize();
                    selectedEntity = child;
                }
                if (ImGui::MenuItem("Create Empty Entity"))
                {
                    Entity* empty = scene.CreateEntity(std::format("Empty_Entity_{}", entityCounter++));
                    auto* transform = empty->AddComponent<TransformComponent>();
                    transform->Initialize();
                    selectedEntity = empty;
                }
                if (ImGui::MenuItem("Delete Entity"))
                {
                    if (selectedEntity == entity) selectedEntity = nullptr;
                    scene.DestroyEntity(entity);
                }
                ImGui::EndPopup();
            }

            if (opened) {
                for (Entity* child : entity->GetChildren()) {
                    drawEntityNode(child);
                }
                ImGui::TreePop();
            }
        };

        for (Entity* entity : Entity::GetActiveSceneEntities()) {
            if (entity && entity->GetParent() == nullptr) { // Draw root nodes only; children recurse
                drawEntityNode(entity);                
            }
        }

        if (ImGui::BeginPopupContextWindow("HierarchyEmptyCtx", ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight))
        {
            static int emptyCounter = 1;
            auto& scene = ServiceLocator::Get<Scene>();
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                Entity* empty = scene.CreateEntity(std::format("Empty_Entity_{}", emptyCounter++));
                auto* transform = empty->AddComponent<TransformComponent>();
                transform->Initialize();
                selectedEntity = empty;
            }

            if (ImGui::MenuItem("Create Ground Plane"))
            {
                auto& resMgr = ServiceLocator::Get<ResourceManager>();
                if (!resMgr.GetResource<Model>("procedural_ground_plane")) {
                    if (auto gm = Model::CreateGroundPlane("procedural_ground_plane", 25.0f, 10.0f)) {
                        resMgr.RegisterExternal<Model>("procedural_ground_plane", gm);
                    }
                }
                auto handle = resMgr.Load<Model>("procedural_ground_plane");
                if (handle.IsValid()) {
                    Entity* plane = scene.CreateEntity(std::format("GroundPlane_{}", emptyCounter++));
                    auto* transform = plane->AddComponent<TransformComponent>();
                    transform->SetPosition(glm::vec3(0.0f, -1.0f, -2.0f));
                    plane->AddComponent<ModelComponent>(handle);
                    plane->Initialize();
                    selectedEntity = plane;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::End();

        // COMPONENT INSPECTOR PANEL
        ImGui::Begin("Inspector");
        if (selectedEntity) {
            ImGui::Text("Entity: %s", selectedEntity->GetName().c_str());
            ImGui::Separator();

            if (auto* transform = selectedEntity->GetComponent<TransformComponent>()) {
                ImGui::PushID("TransformComponent");
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    glm::vec3 pos = transform->GetPosition();
                    if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.1f)) {
                        transform->SetPosition(pos);
                    }

                    glm::vec3 rotation = glm::degrees(glm::eulerAngles(transform->GetRotation()));
                    glm::vec3 prevRotation = rotation;
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(rotation), 0.5f, 0.0f, 0.0f, "%.1f°")) {
                        glm::vec3 delta = glm::radians(rotation - prevRotation);
                        glm::quat qX = glm::angleAxis(delta.x, glm::vec3(1.0f, 0.0f, 0.0f));
                        glm::quat qY = glm::angleAxis(delta.y, glm::vec3(0.0f, 1.0f, 0.0f));
                        glm::quat qZ = glm::angleAxis(delta.z, glm::vec3(0.0f, 0.0f, 1.0f));
                        transform->SetRotation(glm::normalize(qY * qX * qZ * transform->GetRotation()));
                    }

                    glm::vec3 scale = transform->GetScale();
                    if (ImGui::DragFloat3("Scale", glm::value_ptr(scale), 0.05f, 0.01f, 100.0f)) {
                        transform->SetScale(scale);
                    }
                }
                ImGui::PopID();
            }

            if (auto* camComp = selectedEntity->GetComponent<CameraComponent>()) {
                ImGui::PushID("CameraComponent");
                if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (ImGui::Button("Reset Camera")) {
                        if (auto* transform = selectedEntity->GetComponent<TransformComponent>()) {
                            transform->SetPosition(glm::vec3(0.0f, 3.0f, 10.0f));
                            transform->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                        }
                        camComp->SetFOV(45.0f);
                        camComp->SetPerspective(camComp->GetFOV(), viewportSize.x / viewportSize.y, 0.1f, 100.0f);
                    }

                    ImGui::SliderFloat("Movement Speed", &camComp->MovementSpeed, 1.0f, 20.0f);
                    ImGui::SliderFloat("Mouse Sensitivity", &camComp->MouseSensitivity, 0.05f, 1.0f);

                    float fov = camComp->GetFOV();
                    if (ImGui::SliderFloat("FOV (Zoom)", &fov, 1.0f, 90.0f)) {
                        camComp->SetFOV(fov);
                        if (viewportSize.x > 0.0f && viewportSize.y > 0.0f) {
                            camComp->SetPerspective(camComp->GetFOV(), viewportSize.x / viewportSize.y, 0.1f, 100.0f);
                        }
                    }
                }
                ImGui::PopID();
            }

            if (auto* lightComp = selectedEntity->GetComponent<LightComponent>()) {
                ImGui::PushID("LightComponent");
                bool keepLight = true;
                if (ImGui::CollapsingHeader("Light", &keepLight, ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* lightTypes[] = { "Directional", "Point", "Spot" };
                    int currentType = static_cast<int>(lightComp->Type);
                    if (ImGui::Combo("Type", &currentType, lightTypes, 3)) {
                        lightComp->Type = static_cast<LightType>(currentType);
                    }

                    ImGui::ColorEdit3("Color", glm::value_ptr(lightComp->Color));
                    ImGui::DragFloat("Intensity", &lightComp->Intensity, 0.1f, 0.0f, 50.0f);

                    if (lightComp->Type == LightType::Point) {
                        ImGui::DragFloat("Radius", &lightComp->Radius, 0.5f, 1.0f, 100.0f);
                    }
                    if (lightComp->Type == LightType::Spot) {
                        ImGui::DragFloat("Radius", &lightComp->Radius, 0.5f, 1.0f, 100.0f);
                        ImGui::DragFloat("Inner Cone", &lightComp->InnerConeAngle, 0.5f, 1.0f, 100.0f);
                        ImGui::DragFloat("Outer Cone", &lightComp->OuterConeAngle, 0.5f, 1.0f, 100.0f);
                    }

                    ImGui::Checkbox("Cast Shadows", &lightComp->CastShadows);
                }
                if (!keepLight) {
                    selectedEntity->RemoveComponent<LightComponent>();
                }
                ImGui::PopID();
            }

            if (auto* animComp = selectedEntity->GetComponent<AnimationComponent>()) {
                ImGui::PushID("AnimationComponent");
                bool keepAnim = true;
                if (ImGui::CollapsingHeader("Animation", &keepAnim, ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto* modelComp = selectedEntity->GetComponent<ModelComponent>();
                    Model* model = (modelComp && modelComp->GetModelHandle().IsValid()) ? modelComp->GetModelHandle().Get() : nullptr;

                    if (model && !model->animations.empty()) {
                        ImGui::Checkbox("Play", &animComp->IsPlaying);
                        ImGui::SameLine();
                        ImGui::Checkbox("Loop", &animComp->Loop);
                        ImGui::SliderFloat("Speed", &animComp->PlaybackSpeed, 0.0f, 3.0f, "%.2fx");

                        uint32_t safeIdx = std::min(animComp->ActiveClipIndex, static_cast<uint32_t>(model->animations.size() - 1));
                        const char* currentName = model->animations[safeIdx].name.empty()
                            ? "Clip 0"
                            : model->animations[safeIdx].name.c_str();

                        if (ImGui::BeginCombo("Clip", currentName)) {
                            for (uint32_t idx = 0; idx < static_cast<uint32_t>(model->animations.size()); ++idx) {
                                std::string label = model->animations[idx].name.empty()
                                    ? std::format("Clip {}", idx)
                                    : model->animations[idx].name;
                                if (ImGui::Selectable(label.c_str(), safeIdx == idx)) {
                                    animComp->ActiveClipIndex = idx;
                                    model->animations[idx].currentTime = model->animations[idx].start;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    else {
                        ImGui::TextDisabled("Model has no embedded animation clips.");
                    }
                }
                if (!keepAnim) {
                    selectedEntity->RemoveComponent<AnimationComponent>();
                }
                ImGui::PopID();
            }

            if (auto* rbComp = selectedEntity->GetComponent<RigidBodyComponent>()) {
                ImGui::PushID("RigidBodyComponent");
                bool keepRb = true;
                if (ImGui::CollapsingHeader("Rigid Body", &keepRb, ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* motionTypes[] = { "Static", "Kinematic", "Dynamic" };
                    int currentMotion = static_cast<int>(rbComp->MotionType);
                    if (ImGui::Combo("Motion Type", &currentMotion, motionTypes, 3)) {
                        rbComp->MotionType = static_cast<BodyMotionType>(currentMotion);
                    }
                    ImGui::DragFloat("Mass", &rbComp->Mass, 0.1f, 0.01f, 1000.0f);
                    ImGui::SliderFloat("Friction", &rbComp->Friction, 0.0f, 1.0f);
                    ImGui::SliderFloat("Restitution", &rbComp->Restitution, 0.0f, 1.0f);
                    ImGui::Checkbox("Use Gravity", &rbComp->UseGravity);
                }
                if (!keepRb) {
                    selectedEntity->RemoveComponent<RigidBodyComponent>();
                }
                ImGui::PopID();
            }

            if (auto* boxComp = selectedEntity->GetComponent<BoxColliderComponent>()) {
                ImGui::PushID("BoxColliderComponent");
                bool keepBox = true;
                if (ImGui::CollapsingHeader("Box Collider", &keepBox, ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::DragFloat3("Half Extents", glm::value_ptr(boxComp->HalfExtents), 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat3("Offset", glm::value_ptr(boxComp->Offset), 0.05f);
                    ImGui::Checkbox("Is Trigger", &boxComp->IsTrigger);

                    if (ImGui::Button("Fit to Model Bounds", ImVec2(-1.0f, 0.0f))) {
                        if (auto* modelComp = selectedEntity->GetComponent<ModelComponent>();
                            modelComp && modelComp->GetModelHandle().IsValid()) {
                            Model* model = modelComp->GetModelHandle().Get();
                            glm::vec3 minB(std::numeric_limits<float>::max());
                            glm::vec3 maxB(std::numeric_limits<float>::lowest());
                            bool hasVerts = false;

                            std::function<void(Node*, const glm::mat4&)> traverse = [&](Node* node, const glm::mat4& parent) {
                                if (!node) return;
                                glm::mat4 m = parent * node->getLocalMatrix();
                                if (node->mesh) {
                                    for (const auto& v : node->mesh->GetRawVertices()) {
                                        glm::vec3 p = glm::vec3(m * glm::vec4(v.pos, 1.0f));
                                        minB = glm::min(minB, p);
                                        maxB = glm::max(maxB, p);
                                        hasVerts = true;
                                    }
                                }
                                for (auto* c : node->children) traverse(c, m);
                                };
                            for (auto* root : model->nodes) traverse(root, glm::mat4(1.0f));

                            if (hasVerts) {
                                boxComp->HalfExtents = glm::max((maxB - minB) * 0.5f, glm::vec3(0.05f));
                                boxComp->Offset = (minB + maxB) * 0.5f;
                            }
                        }
                    }
                }
                if (!keepBox) {
                    selectedEntity->RemoveComponent<BoxColliderComponent>();
                }
                ImGui::PopID();
            }

            if (auto* peComp = selectedEntity->GetComponent<ParticleEmitterComponent>()) {
                ImGui::PushID("ParticleEmitterComponent");
                bool keepPe = true;
                if (ImGui::CollapsingHeader("Particle Emitter", &keepPe, ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enabled", &peComp->Enabled);
                    ImGui::DragFloat3("Offset", glm::value_ptr(peComp->Offset), 0.05f);
                    ImGui::DragFloat3("Direction", glm::value_ptr(peComp->Direction), 0.05f, -1.0f, 1.0f);
                    ImGui::ColorEdit3("Color", glm::value_ptr(peComp->Color));
                    ImGui::SliderFloat("HDR Intensity", &peComp->HDRIntensity, 0.5f, 15.0f);
                    ImGui::SliderFloat("Particle Size", &peComp->ParticleSize, 0.02f, 1.0f);
                    ImGui::SliderFloat("Speed", &peComp->InitialSpeed, 0.1f, 20.0f);
                    ImGui::SliderFloat("Spread", &peComp->Spread, 0.0f, 1.0f);
                    ImGui::DragFloat("Gravity", &peComp->Gravity, 0.2f, -30.0f, 30.0f);
                    ImGui::SliderInt("Active Count", &peComp->ActiveParticles, 1, SceneRenderer::MAX_PARTICLES);
                }
                if (!keepPe) {
                    selectedEntity->RemoveComponent<ParticleEmitterComponent>();
                }
                ImGui::PopID();
            }

            if (auto* modelComp = selectedEntity->GetComponent<ModelComponent>();
                modelComp && modelComp->GetModelHandle().IsValid())
            {
                Model* model = modelComp->GetModelHandle().Get();
                if (model && !model->materials.empty()) {
                    ImGui::PushID("MaterialsPBRTweaker");
                    if (ImGui::CollapsingHeader("Materials (PBR Tweaker)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        static int selectedMatIdx = 0;
                        selectedMatIdx = std::clamp(selectedMatIdx, 0, static_cast<int>(model->materials.size()) - 1);

                        // Dropdown to pick any sub-material (e.g. Car Paint, Rim, Glass, Watch Case, Dial)
                        const char* previewName = model->materials[selectedMatIdx].name.empty()
                            ? "Material 0"
                            : model->materials[selectedMatIdx].name.c_str();

                        if (ImGui::BeginCombo("Material Slot", previewName)) {
                            for (int i = 0; i < static_cast<int>(model->materials.size()); ++i) {
                                std::string label = model->materials[i].name.empty()
                                    ? std::format("Material {}", i)
                                    : std::format("[{}] {}", i, model->materials[i].name);
                                if (ImGui::Selectable(label.c_str(), selectedMatIdx == i)) {
                                    selectedMatIdx = i;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        const Material& srcMat = model->materials[selectedMatIdx];
                        auto* ovComp = selectedEntity->GetComponent<MaterialOverrideComponent>();
                        bool hasOverride = (ovComp && ovComp->Overrides.contains(selectedMatIdx) && ovComp->Overrides[selectedMatIdx].Enabled);

                        // Read active values (either from override or original glTF material)
                        MaterialOverride currentVals{};
                        if (hasOverride) {
                            currentVals = ovComp->Overrides[selectedMatIdx];
                        }
                        else {
                            currentVals.BaseColorFactor = srcMat.baseColorFactor;
                            currentVals.MetallicFactor = srcMat.metallicFactor;
                            currentVals.RoughnessFactor = srcMat.roughnessFactor;
                            currentVals.ClearcoatFactor = srcMat.clearcoatFactor;
                            currentVals.ClearcoatRoughness = srcMat.clearcoatRoughness;
                            currentVals.IridescenceFactor = srcMat.iridescenceFactor;
                            currentVals.EmissiveFactor = srcMat.emissiveFactor;
                        }

                        bool modified = false;
                        modified |= ImGui::ColorEdit4("Base Color", glm::value_ptr(currentVals.BaseColorFactor));
                        modified |= ImGui::SliderFloat("Metallic", &currentVals.MetallicFactor, 0.0f, 1.0f);
                        modified |= ImGui::SliderFloat("Roughness", &currentVals.RoughnessFactor, 0.04f, 1.0f);
                        modified |= ImGui::SliderFloat("Clearcoat", &currentVals.ClearcoatFactor, 0.0f, 1.0f);
                        modified |= ImGui::SliderFloat("Coat Roughness", &currentVals.ClearcoatRoughness, 0.04f, 1.0f);
                        modified |= ImGui::SliderFloat("Iridescence", &currentVals.IridescenceFactor, 0.0f, 1.0f);
                        modified |= ImGui::ColorEdit3("Emissive", glm::value_ptr(currentVals.EmissiveFactor));

                        if (modified) {
                            if (!ovComp) {
                                ovComp = selectedEntity->AddComponent<MaterialOverrideComponent>();
                                ovComp->Initialize();
                            }
                            currentVals.Enabled = true;
                            ovComp->Overrides[selectedMatIdx] = currentVals;
                        }

                        if (hasOverride) {
                            if (ImGui::Button("Reset Material to glTF Default", ImVec2(-1.0f, 0.0f))) {
                                ovComp->Overrides.erase(selectedMatIdx);
                                if (ovComp->Overrides.empty()) {
                                    selectedEntity->RemoveComponent<MaterialOverrideComponent>();
                                }
                            }
                        }
                    }
                    ImGui::PopID();
                }
            }

            // "+ Add Component" Popup Button
            ImGui::Spacing();
            if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 0.0f))) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (!selectedEntity->GetComponent<LightComponent>()) {
                    if (ImGui::MenuItem("Light Component (On This Entity)")) {
                        if (auto* light = selectedEntity->AddComponent<LightComponent>()) {
                            light->Initialize();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (ImGui::MenuItem("Child Light Entity (Movable Offset)")) {
                    static int childLightCounter = 1;
                    Entity* childLight = ServiceLocator::Get<Scene>().CreateEntity(
                        std::format("{}_Light_{}", selectedEntity->GetName(), childLightCounter++),
                        selectedEntity
                    );
                    auto* childTrans = childLight->AddComponent<TransformComponent>();
                    childTrans->SetPosition(glm::vec3(0.0f, 1.5f, 1.0f)); // Offset from parent pivot
                    auto* lightComp = childLight->AddComponent<LightComponent>();
                    lightComp->Type = LightType::Spot;
                    childLight->SetParent(selectedEntity);
                    childLight->Initialize();
                    selectedEntity = childLight; // Select immediately so ImGuizmo lets you move/aim it!
                    ImGui::CloseCurrentPopup();
                }
                if (!selectedEntity->GetComponent<AnimationComponent>()) {
                    if (ImGui::MenuItem("Animation Component")) {
                        if (auto* anim = selectedEntity->AddComponent<AnimationComponent>()) {
                            anim->Initialize();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!selectedEntity->GetComponent<RigidBodyComponent>()) {
                    if (ImGui::MenuItem("Rigid Body Component")) {
                        if (auto* rb = selectedEntity->AddComponent<RigidBodyComponent>()) rb->Initialize();
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!selectedEntity->GetComponent<BoxColliderComponent>()) {
                    if (ImGui::MenuItem("Box Collider Component")) {
                        if (auto* box = selectedEntity->AddComponent<BoxColliderComponent>()) box->Initialize();
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!selectedEntity->GetComponent<ParticleEmitterComponent>()) {
                    if (ImGui::MenuItem("Particle Emitter Component")) {
                        if (auto* pe = selectedEntity->AddComponent<ParticleEmitterComponent>()) pe->Initialize();
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (selectedEntity->GetComponent<LightComponent>() && selectedEntity->GetComponent<AnimationComponent>()
                    && selectedEntity->GetComponent<RigidBodyComponent>() && selectedEntity->GetComponent<BoxColliderComponent>()
                    && selectedEntity->GetComponent<ParticleEmitterComponent>()) {
                    ImGui::TextDisabled("All components attached");
                }

                ImGui::EndPopup();
            }
        }        
        else {
            ImGui::TextDisabled("Select an entity in the Scene Hierarchy.");
        }

        // Always visible regardless of entity selection
        ImGui::Separator();
        auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
        if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("IBL Ambient", &pbrRenderer.m_IBLIntensity, 0.0f, 1.5f);
            ImGui::SliderFloat("Emissive Strength", &pbrRenderer.m_EmissiveStrength, 0.0f, 15.0f);
            ImGui::SliderFloat("Bloom Intensity", &pbrRenderer.m_BloomIntensity, 0.0f, 2.0f);

            ImGui::Separator();
            ImGui::SliderFloat("SSR Intensity", &pbrRenderer.m_SSRIntensity, 0.0f, 2.0f);
            ImGui::SliderFloat("SSR Max Distance", &pbrRenderer.m_SSRMaxDistance, 5.0f, 60.0f);
            ImGui::SliderFloat("SSR Thickness", &pbrRenderer.m_SSRThickness, 0.05f, 2.0f);

            ImGui::Separator();
            ImGui::ColorEdit3("Fog Color", glm::value_ptr(pbrRenderer.m_FogColor));
            ImGui::SliderFloat("Fog Density", &pbrRenderer.m_FogDensity, 0.0f, 0.12f, "%.4f");
            ImGui::SliderFloat("Sun Shafts", &pbrRenderer.m_FogSunScatter, 0.0f, 6.0f);
            ImGui::SliderFloat("Fog Height Falloff", &pbrRenderer.m_FogHeightFalloff, 0.01f, 1.0f);
            ImGui::DragFloat("Fog Base Height", &pbrRenderer.m_FogBaseHeight, 0.1f, -20.0f, 20.0f);
            ImGui::DragFloat("Fog Max Distance", &pbrRenderer.m_FogMaxDistance, 1.0f, 0.0f, 100.0f);

            ImGui::Separator();
            static char hdriPathBuf[256] = "assets/textures/studio.hdr";
            static std::string lastSyncedHDRI = "";
            if (lastSyncedHDRI != pbrRenderer.m_ActiveHDRIPath) {
                lastSyncedHDRI = pbrRenderer.m_ActiveHDRIPath;
                std::snprintf(hdriPathBuf, sizeof(hdriPathBuf), "%s", lastSyncedHDRI.c_str());
            }
            ImGui::InputText("HDRI Path", hdriPathBuf, sizeof(hdriPathBuf));
            if (ImGui::Button("Load HDRI")) {
                pbrRenderer.SetEnvironmentHDRI(hdriPathBuf);
            }
            ImGui::SameLine();
            if (ImGui::Button("Procedural Studio")) {
                pbrRenderer.SetEnvironmentHDRI("");
            }
        }
        ImGui::End();

        // 4. ASSET BROWSER PANEL
        ImGui::Begin("Asset Browser");
        {
            namespace fs = std::filesystem;
            static fs::path rootAssetsDir = fs::exists("assets")
                ? fs::path("assets")
                : fs::path(ROOT_WORKSPACE_DIR) / "assets";
            static fs::path currentDir = rootAssetsDir;
            static char searchFilter[] = "";

            if (!fs::exists(currentDir)) {
                currentDir = rootAssetsDir;
            }

            // Navigation Bar: Back button, Current Path & Search Filter
            if (currentDir != rootAssetsDir) {
                if (ImGui::Button("<- Back")) {
                    currentDir = currentDir.parent_path();
                }
                ImGui::SameLine();
            }
            ImGui::TextDisabled("%s", fs::relative(currentDir, rootAssetsDir.parent_path()).generic_string().c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputTextWithHint("##AssetSearch", "Filter assets...", searchFilter, sizeof(searchFilter));
            ImGui::Separator();

            if (fs::exists(currentDir) && fs::is_directory(currentDir)) {
                float cellWidth = 115.0f;
                float panelWidth = ImGui::GetContentRegionAvail().x;
                int columnCount = std::max(1, static_cast<int>(panelWidth / cellWidth));

                if (ImGui::BeginTable("AssetGrid", columnCount)) {
                    std::string filterStr(searchFilter);
                    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

                    for (const auto& entry : fs::directory_iterator(currentDir)) {
                        const fs::path& p = entry.path();
                        std::string filename = p.filename().string();
                        std::string ext = p.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                        bool isDir = entry.is_directory();
                        bool isModel = (ext == ".glb" || ext == ".gltf");
                        bool isHDR = (ext == ".hdr");
                        bool isScene = (ext == ".json");

                        if (!isDir && !isModel && !isHDR && !isScene) continue;

                        if (!filterStr.empty()) {
                            std::string lowerName = filename;
                            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                            if (lowerName.find(filterStr) == std::string::npos) continue;
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushID(filename.c_str());

                        // Color-coded badge button by asset type
                        ImVec4 badgeCol = isDir ? ImVec4(0.85f, 0.68f, 0.22f, 1.0f) :
                            isModel ? ImVec4(0.22f, 0.58f, 0.85f, 1.0f) :
                            isHDR ? ImVec4(0.25f, 0.72f, 0.45f, 1.0f) :
                            ImVec4(0.65f, 0.42f, 0.82f, 1.0f);

                        const char* typeTag = isDir ? "[DIR]" : isModel ? "[3D]" : isHDR ? "[HDRI]" : "[SCN]";

                        ImGui::PushStyleColor(ImGuiCol_Button, badgeCol);
                        bool clicked = ImGui::Button(typeTag, ImVec2(cellWidth - 12.0f, 44.0f));
                        ImGui::PopStyleColor();

                        if (clicked || (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
                            if (isDir) {
                                currentDir = p;
                            }
                            else if (isModel) {
                                std::string assetKey = p.stem().string();
                                auto& resMgr = ServiceLocator::Get<ResourceManager>();
                                resMgr.RegisterAssetPath(assetKey, p.generic_string());

                                auto handle = resMgr.Load<Model>(assetKey);
                                if (handle.IsValid()) {
                                    static int importCounter = 1;
                                    auto& scene = ServiceLocator::Get<Scene>();
                                    Entity* spawned = scene.CreateEntity(std::format("{}_{}", assetKey, importCounter++));
                                    auto* t = spawned->AddComponent<TransformComponent>();
                                    t->SetPosition(glm::vec3(0.0f, 0.0f, -2.0f));
                                    spawned->AddComponent<ModelComponent>(handle);
                                    spawned->Initialize();
                                    selectedEntity = spawned;
                                }
                            }
                            else if (isHDR) {
                                pbrRenderer.SetEnvironmentHDRI(p.generic_string());
                            }
                            else if (isScene) {
                                SceneSerializer::Deserialize(p.generic_string());
                            }
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\nClick to %s", filename.c_str(),
                                isDir ? "open folder" : isModel ? "spawn into scene" : isHDR ? "apply HDRI skybox" : "load scene");
                        }

                        ImGui::TextWrapped("%s", filename.c_str());
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }

        ImGui::End();
        ImGui::Render();
    }

    void ImGuiVulkanUtil::handleKey(int key, int scancode, int action, int mods) {
        ImGuiIO& io = ImGui::GetIO();

        // Map the platform-specific key action to a boolean state
        // In GLFW: GLFW_RELEASE = 0, GLFW_PRESS = 1, GLFW_REPEAT = 2
        bool pressed = (action != 0);

        // Modern ImGui (v1.87+) uses AddKeyEvent to queue input events.
        // This handles key states, modifiers, and repeat logic internally.
        // Most backends can cast native key codes directly to ImGuiKey.
        io.AddKeyEvent((ImGuiKey)key, pressed);
    }

    void ImGuiVulkanUtil::handleMousePos(float x, float y) {
        ImGuiIO& io = ImGui::GetIO();
        // Modern event API for mouse position
        io.AddMousePosEvent(x, y);
    }

    void ImGuiVulkanUtil::handleMouseButton(int button, bool pressed) {
        ImGuiIO& io = ImGui::GetIO();
        // Modern event API for mouse buttons (0: Left, 1: Right, 2: Middle)
        io.AddMouseButtonEvent(button, pressed);
    }

    bool ImGuiVulkanUtil::getWantKeyCapture() {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    void ImGuiVulkanUtil::charPressed(uint32_t key) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(key);
    }

    void ImGuiVulkanUtil::SetSceneViewportTexture(vk::ImageView sceneImageView)
    {
        if (!sceneImageView) return;

        if (!*sceneViewportDescriptorSet) {
            vk::DescriptorSetAllocateInfo allocInfo{
                .descriptorPool = *descriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &*descriptorSetLayout
            };
            sceneViewportDescriptorSet = std::move(device->allocateDescriptorSets(allocInfo).front());
        }

        vk::DescriptorImageInfo imageInfo{
            .sampler = *sampler,
            .imageView = sceneImageView,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        vk::WriteDescriptorSet writeSet{
            .dstSet = *sceneViewportDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &imageInfo
        };

        device->updateDescriptorSets(writeSet, {});
    }

}