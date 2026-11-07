#include "protopch.hpp"
#include "ImGuiVulkanUtil.hpp"
#include "Platform/Vulkan/VK_Pipeline.hpp"
#include "Platform/Vulkan/VK_BufferUtils.hpp"
#include "Core/ServiceLocator.hpp"

#include "ShaderConfig.hpp"
#include <fstream>
#include <format>
#include <filesystem>

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

        // All resources are automatically cleaned up by their destructors
        // No manual cleanup needed

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

        // Create descriptor pool for shader resource binding
        // Descriptors provide the interface between shaders and GPU resources
        vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eCombinedImageSampler, 1 };

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;     // Allow individual descriptor set freeing
        poolInfo.maxSets = 2;                                                      // Maximum number of descriptor sets
        poolInfo.poolSizeCount = 1;                                                // Number of pool size specifications
        poolInfo.pPoolSizes = &poolSize;                                           // Pool size configuration

        descriptorPool = device->createDescriptorPool(poolInfo);                   // Create descriptor pool

        // Create descriptor set layout defining shader resource interface
        // This layout must match the binding declarations in the ImGui shaders
        vk::DescriptorSetLayoutBinding binding{};
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;        // Combined texture and sampler
        binding.descriptorCount = 1;                                               // Single texture binding
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;                   // Used in fragment shader
        binding.binding = 0;                                                       // Shader binding point 0

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;                                               // Number of bindings in layout
        layoutInfo.pBindings = &binding;                                           // Binding configuration array

        descriptorSetLayout = device->createDescriptorSetLayout(layoutInfo);       // Create layout object

        // Allocate descriptor set from pool using the defined layout
        // This creates the actual binding that connects GPU resources to shaders
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

        // Create the graphics pipeline with dynamic rendering
        // ... (shader loading, pipeline state setup, etc.)

        // For brevity, we're omitting the full pipeline creation code here
        // In a real implementation, you would:
        // 1. Load the vertex and fragment shaders
        // 2. Set up all the pipeline state (vertex input, input assembly, rasterization, etc.)
        // 3. Include the renderingInfo in the pipeline creation to enable dynamic rendering

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

                //VulkanImageView::createImageView(*fontImage, format, vulkanImage->vulkanDevice, vk::ImageAspectFlagBits::eColor, 1);
                 // FIX: Capture and assign the returned ImageView to your class member variable
                fontImageView = image.CreateImageView(*fontImage, format, vk::ImageAspectFlagBits::eColor, 1);

                // NEW: Update the descriptor set NOW that fontImageView contains a valid handle
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
        return true;
    }

    void ImGuiVulkanUtil::updateBuffers()
    {
        auto& cmdBuffer = ServiceLocator::Get<VulkanCommandBuffer>();

        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData || drawData->CmdListsCount == 0) {
            return;
        }

        // Calculate required buffer sizes
        vk::DeviceSize vertexBufferSize = drawData->TotalVtxCount * sizeof(ImDrawVert);
        vk::DeviceSize indexBufferSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);

        // Resize buffers if needed
        if (drawData->TotalVtxCount > vertexCount) {
            // Recreate vertex buffer with new size
            auto [staging_Vertex, staging_VertexMemory] =
                cmdBuffer.CreateBuffer(vertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            // FIX: Assign directly to the class variables
            vertexBuffer = std::move(staging_Vertex);
            vertexBufferMemory = std::move(staging_VertexMemory);

            vertexCount = drawData->TotalVtxCount;
        }

        if (drawData->TotalIdxCount > indexCount) {
            // Recreate index buffer with new size
            auto [staging_Index, staging_IndexMemory] =
                cmdBuffer.CreateBuffer(indexBufferSize, vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            // FIX: Assign directly to the class variables
            indexBuffer = std::move(staging_Index);
            indexBufferMemory = std::move(staging_IndexMemory);

            indexCount = drawData->TotalIdxCount;
        }
        // 2. Map, copy, and unmap using your updated class memory members
        // Upload data to buffers
        ImDrawVert* vtxDst = static_cast<ImDrawVert*>(vertexBufferMemory.mapMemory(0, vertexBufferSize));
        ImDrawIdx* idxDst = static_cast<ImDrawIdx*>(indexBufferMemory.mapMemory(0, indexBufferSize));

        for (int n = 0; n < drawData->CmdListsCount; n++) {
            const ImDrawList* cmdList = drawData->CmdLists[n];
            memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmdList->VtxBuffer.Size;
            idxDst += cmdList->IdxBuffer.Size;
        }
        vertexBufferMemory.unmapMemory();
        indexBufferMemory.unmapMemory();
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

        // We already filled these buffers this frame
        vk::Buffer vertexBuffers[] = { vertexBuffer };
        
        vk::DeviceSize offsets[] = { 0 };
        commandBuffer.bindVertexBuffers(0, *vertexBuffer, { 0 });
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint16);

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

    void ImGuiVulkanUtil::drawGUI(Camera& camera)
    {
        // Create a window for camera controls
        ImGui::Begin("Camera Controls");

        // Add a button to reset camera position
        if (ImGui::Button("Reset Camera")) {
            camera.setPosition(glm::vec3(0.0f, 0.0f, 3.0f));
            camera.setYaw(-90.0f);
            camera.setPitch(0.0f);
        }

        // Add sliders for camera settings
        float movementSpeed = camera.getMovementSpeed();
        if (ImGui::SliderFloat("Movement Speed", &movementSpeed, 1.0f, 10.0f)) {
            camera.setMovementSpeed(movementSpeed);
        }

        float sensitivity = camera.getMouseSensitivity();
        if (ImGui::SliderFloat("Mouse Sensitivity", &sensitivity, 0.1f, 1.0f)) {
            camera.setMouseSensitivity(sensitivity);
        }

        float zoom = camera.getZoom();
        if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 45.0f)) {
            camera.setZoom(zoom);
        }

        ImGui::End();

        // Render ImGui
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
}