#include "protopch.hpp"
#include "RendererPipeline.hpp"

// --- 🟩 ADD THESE CRITICAL MISSING COUPLING INCLUDES ---
#include "Core/ServiceLocator.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Renderer/Camera.hpp" 

namespace Proto
{
    vk::Format SceneRenderer::findDepthFormat()
    {
        // Forward the request directly to your low-level Vulkan backend util subsystem
        auto& lowLevelRenderer = VulkanRenderer::Get();
        auto* imgUtil = lowLevelRenderer.GetVulkanImageSubsystem();

        if (imgUtil) {
            return imgUtil->FindDepthFormat();
        }

        // Fallback default safe depth format layout configuration if subsystem is offline
        return vk::Format::eD32Sfloat;
    }

    //void SceneRenderer::ValidateDepthResources(uint32_t width, uint32_t height)
    //{
    //    if (m_DepthWidth == width && m_DepthHeight == height && *m_DepthImageView) {
    //        return; // Allocation matches perfectly, skip re-creation
    //    }
    //
    //    auto& renderer = VulkanRenderer::Get();
    //    auto* imgUtil = renderer.GetVulkanImageSubsystem();
    //    vk::Format depthFormat = findDepthFormat();
    //
    //    // Clear old handles safely
    //    m_DepthImageView = nullptr;
    //    m_DepthMemory = nullptr;
    //    m_DepthImage = nullptr;
    //
    //    // Create 1x Single Sampled Optimal Depth Buffer Context
    //    auto [img, mem] = imgUtil->CreateImage(
    //        width, height, 1, vk::SampleCountFlagBits::e1, depthFormat,
    //        vk::ImageTiling::eOptimal,
    //        vk::ImageUsageFlagBits::eDepthStencilAttachment,
    //        vk::MemoryPropertyFlagBits::eDeviceLocal
    //    );
    //
    //    m_DepthImage = std::move(img);
    //    m_DepthMemory = std::move(mem);
    //    m_DepthImageView = imgUtil->CreateImageView(*m_DepthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
    //
    //    m_DepthWidth = width;
    //    m_DepthHeight = height;
    //    std::cout << "[ENGINE LOG]: Allocated dynamic depth buffer at size: " << width << "x" << height << std::endl;
    //}

    SceneRenderer::SceneRenderer(vk::raii::Device& dev, vk::Queue queue) : device(dev), graphicsQueue(queue)
    {
        // Create command pool
        // ...
        // Create synchronization objects
        // ...
        // Set up render passes
        SetupRenderPasses();
        CreatePBRPipeline();
        CreateDeferredLightingPipeline();
    }

    void SceneRenderer::Render(const std::vector<Entity*>& entities, vk::raii::CommandBuffer& activeCmdBuffer, uint32_t imageIndex)
    {
        //cullingSystem.CullScene(entities);
        //auto visibleEntities = cullingSystem.GetVisibleEntities();
        auto visibleEntities = entities;
        if (visibleEntities.empty()) {
            visibleEntities = entities;
        }

        auto& renderer = VulkanRenderer::Get();
        auto& logicalDevice = renderer.GetLogicalDevice();
        auto* swapchainSubsystem = renderer.vulkanSwapChain;
        uint32_t currentFrame = swapchainSubsystem->GetFrameIndex();

        uint32_t width = swapchainSubsystem->swapChainExtent.width;
        uint32_t height = swapchainSubsystem->swapChainExtent.height;

        // STAGE 1: THE MATERIAL WARM-UP PRE-PASS
        // We initialize and update descriptor sets completely BEFORE opening the render pass context.
        // This ensures mid-frame descriptor updates never corrupt our graphics pipeline state!
        for (auto entity : visibleEntities) {
            if (!entity || !entity->IsActive()) continue;

            auto* modelComp = entity->GetComponent<ModelComponent>();
            if (modelComp) {
                auto modelHandle = modelComp->GetModelHandle();
                if (modelHandle.IsValid() && modelHandle->IsLoaded()) {
                    Model* rawModel = modelHandle.Get();

                    // Change: Loop over materials directly instead of linearNodes!
                    for (size_t m = 0; m < rawModel->materials.size(); m++) {
                        auto& material = rawModel->materials[m];

                        if (!*material.descriptorSet || *material.descriptorSet == VK_NULL_HANDLE) {

                            vk::DescriptorSetAllocateInfo allocInfo{
                                .descriptorPool = *renderer.vulkanCommandBuffer->descriptorPool,
                                .descriptorSetCount = 1,
                                .pSetLayouts = &*descriptorSetLayout
                            };

                            auto allocatedSets = logicalDevice.allocateDescriptorSets(allocInfo);
                            material.descriptorSet = std::move(allocatedSets.front());

                            std::vector<vk::WriteDescriptorSet> writes;
                            std::vector<std::unique_ptr<vk::DescriptorImageInfo>> stableImageInfos;

                            auto updateTextureSetSlot = [&](int textureIdx, uint32_t dstBinding) {
                                std::string targetTextureId = "viking_room";

                                if (textureIdx >= 0 && textureIdx < static_cast<int>(rawModel->textureResourceIds.size())) {
                                    targetTextureId = rawModel->textureResourceIds[textureIdx];
                                }

                                auto& resManager = ServiceLocator::Get<ResourceManager>();
                                auto* tex = resManager.GetResource<Texture>(targetTextureId);

                                if (!tex || !tex->IsLoaded()) {
                                    tex = resManager.GetResource<Texture>("viking_room");
                                }

                                if (tex) {
                                    auto imgInfo = std::make_unique<vk::DescriptorImageInfo>(vk::DescriptorImageInfo{
                                        .sampler = *tex->GetSampler(),
                                        .imageView = *tex->GetImageView(),
                                        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
                                        });

                                    writes.push_back(vk::WriteDescriptorSet{
                                        .dstSet = *material.descriptorSet,
                                        .dstBinding = dstBinding,
                                        .descriptorCount = 1,
                                        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                        .pImageInfo = imgInfo.get()
                                        });
                                    stableImageInfos.push_back(std::move(imgInfo));
                                }
                            };

                            // Base-0 Continuous Bindings matching your shader attributes
                            updateTextureSetSlot(material.baseColorTextureIndex, 0);
                            updateTextureSetSlot(material.metallicRoughnessTextureIndex, 1);
                            updateTextureSetSlot(material.normalTextureIndex, 2);
                            updateTextureSetSlot(material.occlusionTextureIndex, 3);
                            updateTextureSetSlot(material.emissiveTextureIndex, 4);

                            if (!writes.empty()) {
                                logicalDevice.updateDescriptorSets(writes, {});
                            }
                        }
                    }
                }
            }
        }

        // STAGE 2: STANDARD OPEN RENDERING PASS
        //vk::Image swapchainImage = swapchainSubsystem->swapChainImages[imageIndex];
        //vk::ImageView swapchainView = swapchainSubsystem->swapChainImageViews[imageIndex];

        //vk::ImageMemoryBarrier barrierToRender{
        //    .srcAccessMask = vk::AccessFlagBits::eNone,
        //    .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        //    .oldLayout = vk::ImageLayout::eUndefined,
        //    .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
        //    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        //    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        //    .image = swapchainImage,
        //    .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
        //};
        //
        //vk::ImageMemoryBarrier barrierToRenderDepth{
        //   .srcAccessMask = vk::AccessFlagBits::eNone,
        //   .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        //   .oldLayout = vk::ImageLayout::eUndefined,
        //   .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        //   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        //   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        //   .image = *swapchainSubsystem->depthImage,
        //   .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
        //};
        //
        //activeCmdBuffer.pipelineBarrier(
        //    vk::PipelineStageFlagBits::eTopOfPipe,
        //    vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        //    vk::DependencyFlags(), {}, {}, { barrierToRender, barrierToRenderDepth }
        //);
        //
        //vk::ClearValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.15f, 1.0f});
        //vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0); // Far-plane reset
        //
        //vk::RenderingAttachmentInfo colorAttachment;
        //colorAttachment.setImageView(swapchainView)
        //    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        //    .setLoadOp(vk::AttachmentLoadOp::eClear)
        //    .setStoreOp(vk::AttachmentStoreOp::eStore)
        //    .setClearValue(clearColor);
        //
        //vk::RenderingAttachmentInfo depthAttachment;
        //depthAttachment.setImageView(*swapchainSubsystem->depthImageView) // 🟩 Read directly from swapchain subsystem resource managers!
        //    .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
        //    .setLoadOp(vk::AttachmentLoadOp::eClear)
        //    .setStoreOp(vk::AttachmentStoreOp::eDontCare)
        //    .setClearValue(clearDepth);
        //
        //vk::RenderingInfo renderingInfo;
        //renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { width, height }))
        //    .setLayerCount(1)
        //    .setColorAttachmentCount(1)
        //    .setPColorAttachments(&colorAttachment)
        //    .setPDepthAttachment(&depthAttachment);
        //
        //activeCmdBuffer.beginRendering(renderingInfo);

        // Bind our verified graphics state context layout definitions
        if (*pbrPipeline) {
            activeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrPipeline);
        }

        float fWidth = static_cast<float>(width);
        float fHeight = static_cast<float>(height);
        activeCmdBuffer.setViewport(0, { vk::Viewport(0.0f, 0.0f, fWidth, fHeight, 0.0f, 1.0f) });
        activeCmdBuffer.setScissor(0, { vk::Rect2D(vk::Offset2D(0, 0), swapchainSubsystem->swapChainExtent) });

        CameraComponent* activeCameraComponent = nullptr;
        for (auto entity : entities) {
            if (entity && entity->IsActive()) {
                if (auto* camComp = entity->GetComponent<CameraComponent>()) {
                    activeCameraComponent = camComp;
                    break;
                }
            }
        }

        updateUniformBuffer(currentFrame, nullptr, activeCameraComponent);

        if (!renderer.vulkanCommandBuffer->descriptorSets.empty()) {
            activeCmdBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *pbrPipelineLayout,
                0,
                { *renderer.vulkanCommandBuffer->descriptorSets[currentFrame] },
                {}
            );
        }

        // Draw loop
        for (auto entity : visibleEntities) {
            if (!entity || !entity->IsActive()) continue;

            auto* modelComp = entity->GetComponent<ModelComponent>();
            if (modelComp) {
                auto modelHandle = modelComp->GetModelHandle();
                if (modelHandle.IsValid()) {
                    Model* rawModel = modelHandle.Get();
                    if (rawModel && rawModel->IsLoaded()) {
                        auto* transform = entity->GetComponent<TransformComponent>();
                        glm::mat4 entityWorldMatrix = transform ? transform->GetWorldTransformMatrix() : glm::mat4(1.0f);

                        for (auto* rootNode : rawModel->nodes) {
                            RenderModelNode(rootNode, entityWorldMatrix, activeCmdBuffer, rawModel, activeCameraComponent);
                        }
                    }
                }
            }
        }

        //activeCmdBuffer.endRendering();
    }

    bool SceneRenderer::CreatePBRPipeline() {
        try {
            //if (!ServiceLocator::Contains<VulkanRenderer>()) {
            //    std::cerr << "[WARN]: SceneRenderer cannot compile PBR pipelines because VulkanRenderer is not registered yet!" << std::endl;
            //    return false;
            //}

            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("assets/shaders/pbr.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
            vertShaderStageInfo.setStage(vk::ShaderStageFlagBits::eVertex)
                .setModule(*shaderModule)
                .setPName("VSMain");

            vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
            fragShaderStageInfo.setStage(vk::ShaderStageFlagBits::eFragment)
                .setModule(*shaderModule)
                .setPName("PSMain");

            std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

            // Vertex Inputs Configuration
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

            // FIXED 1: Use your structure's type-safe binding description layout
            vk::VertexInputBindingDescription bindingDescription = Proto::Vertex::getBindingDescription();

            // FIXED 2: Match your structure's actual 4-attribute layout. This removes the non-existent 5th attribute (Tangent) that corrupted memory alignment!
            std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions = {
                vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, pos)),
                vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, normal)),
                vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, color)),
                vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32Sfloat,    offsetof(Proto::Vertex, texCoord))
            };

            vertexInputInfo.setVertexBindingDescriptionCount(1)
                .setPVertexBindingDescriptions(&bindingDescription)
                .setVertexAttributeDescriptionCount(static_cast<uint32_t>(attributeDescriptions.size()))
                .setPVertexAttributeDescriptions(attributeDescriptions.data());

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
            inputAssembly.setTopology(vk::PrimitiveTopology::eTriangleList).setPrimitiveRestartEnable(false);

            vk::PipelineViewportStateCreateInfo viewportState;
            viewportState.setViewportCount(1).setScissorCount(1);

            std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState;
            dynamicState.setDynamicStateCount(static_cast<uint32_t>(dynamicStates.size())).setPDynamicStates(dynamicStates.data());

            vk::PipelineRasterizationStateCreateInfo rasterizer;
            rasterizer.setDepthClampEnable(false).setRasterizerDiscardEnable(false).setPolygonMode(vk::PolygonMode::eFill)
                .setLineWidth(1.0f).setCullMode(vk::CullModeFlagBits::eBack).setFrontFace(vk::FrontFace::eCounterClockwise).setDepthBiasEnable(false);

            vk::PipelineMultisampleStateCreateInfo multisampling;
            multisampling.setSampleShadingEnable(false).setRasterizationSamples(vk::SampleCountFlagBits::e1);

            vk::PipelineDepthStencilStateCreateInfo depthStencil;
            depthStencil.setDepthTestEnable(true).setDepthWriteEnable(true).setDepthCompareOp(vk::CompareOp::eLess)
                .setDepthBoundsTestEnable(false).setStencilTestEnable(false);

            // 🟩 MRT FIX: Configure 3 matching attachment blend states!
            std::array<vk::PipelineColorBlendAttachmentState, 3> blendAttachments;
            for (size_t i = 0; i < 3; ++i) {
                blendAttachments[i].setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
                    .setBlendEnable(false);
            }
            //vk::PipelineColorBlendAttachmentState colorBlendAttachment;
            //colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
            //    .setBlendEnable(true).setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha).setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
            //    .setColorBlendOp(vk::BlendOp::eAdd).setSrcAlphaBlendFactor(vk::BlendFactor::eOne).setDstAlphaBlendFactor(vk::BlendFactor::eZero).setAlphaBlendOp(vk::BlendOp::eAdd);

            vk::PipelineColorBlendStateCreateInfo colorBlending;
            colorBlending.setLogicOpEnable(false)
                .setAttachmentCount(static_cast<uint32_t>(blendAttachments.size()))
                .setPAttachments(blendAttachments.data());

            vk::PushConstantRange pushConstantRange;
            pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment).setOffset(0).setSize(sizeof(PushConstantBlock));

            // FIXED 1: AUTONOMOUSLY INITIALIZE DESCRIPTOR SET 0 LAYOUT (GLOBAL UBO)
            vk::DescriptorSetLayoutBinding uboBinding{
                .binding = 0,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
            };
            vk::DescriptorSetLayoutCreateInfo uboLayoutInfo{ .bindingCount = 1, .pBindings = &uboBinding };

            // We compile a dedicated, local RAII variable inside the SceneRenderer to prevent tracking drops
            // Ensure you declare 'vk::raii::DescriptorSetLayout uboDescriptorSetLayout{nullptr};' in RendererPipeline.hpp if needed,
            // or let a local class variable hold it:
            vk::raii::DescriptorSetLayout uboDescriptorSetLayout = device.createDescriptorSetLayout(uboLayoutInfo);

            // FIXED 2: AUTONOMOUSLY INITIALIZE DESCRIPTOR SET 1 LAYOUT (MATERIAL TEXTURES)
            std::array<vk::DescriptorSetLayoutBinding, 5> texBindings;
            for (uint32_t b = 0; b < 5; ++b) {
                texBindings[b] = vk::DescriptorSetLayoutBinding{
                    .binding = b, //+ 1, // Bindings 1, 2, 3, 4, 5 matching your shader architecture parameters
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags = vk::ShaderStageFlagBits::eFragment
                };
            }
            vk::DescriptorSetLayoutCreateInfo texLayoutInfo{ .bindingCount = 5, .pBindings = texBindings.data() };
            descriptorSetLayout = device.createDescriptorSetLayout(texLayoutInfo);

            // FIXED 3: PACK BOTH OPERATIONAL RAII HANDLES INTO THE LAYOUT MANAGER
            vk::DescriptorSetLayout pipelineSetLayouts[] = {
                *uboDescriptorSetLayout, // Set 0
                *descriptorSetLayout     // Set 1
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
            pipelineLayoutInfo.setSetLayoutCount(2)
                .setPSetLayouts(pipelineSetLayouts)
                .setPushConstantRangeCount(1)
                .setPPushConstantRanges(&pushConstantRange);

            pbrPipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

            // Assemble graphics pipeline
            vk::GraphicsPipelineCreateInfo pipelineInfo;
            pipelineInfo.setStageCount(static_cast<uint32_t>(shaderStages.size())).setPStages(shaderStages.data())
                .setPVertexInputState(&vertexInputInfo).setPInputAssemblyState(&inputAssembly).setPViewportState(&viewportState)
                .setPRasterizationState(&rasterizer).setPMultisampleState(&multisampling).setPDepthStencilState(&depthStencil)
                .setPColorBlendState(&colorBlending).setPDynamicState(&dynamicState).setLayout(*pbrPipelineLayout)
                .setRenderPass(nullptr).setSubpass(0).setBasePipelineHandle(nullptr);

            // 🟩 MRT FIX: Map the 3 format types to match your BeginPass colorAttachments array!
            std::array<vk::Format, 3> gBufferFormats = {
                vk::Format::eR16G16B16A16Sfloat, // Target 0: Position
                vk::Format::eR16G16B16A16Sfloat, // Target 1: Normal
                vk::Format::eR8G8B8A8Unorm       // Target 2: Albedo
            };

            vk::PipelineRenderingCreateInfo renderingInfo;
            renderingInfo.setColorAttachmentCount(static_cast<uint32_t>(gBufferFormats.size()))
                .setPColorAttachmentFormats(gBufferFormats.data())
                .setDepthAttachmentFormat(findDepthFormat());
            pipelineInfo.setPNext(&renderingInfo);

            pbrPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

            // 🟩 ADD THIS CRITICAL DEBUG TRAP:
            if (!*pbrPipeline) {
                throw std::runtime_error("[FATAL ENGINE ERROR]: PBR Graphics Pipeline compilation returned nullptr! Check shader attributes mapping.");
            }

            // Keep uboDescriptorSetLayout alive by moving it or mapping its lifetime into the class properties if required, 
            // but since pipeline layout creation processes copies of handles immediately, leaving it scoped here is perfectly safe!
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "Error creating PBR pipeline: " << e.what() << std::endl;
            return false;
        }
    }

    // Update uniform buffer
    void SceneRenderer::updateUniformBuffer(uint32_t currentFrame, Entity* entity, CameraComponent* camera, const glm::mat4& modelMatrix) {

        // Create the uniform buffer object
        PBRUniformBufferObject ubo{};

        Camera* activeCam = nullptr;
        if (camera != nullptr)
        {
            ubo.view = camera->GetViewMatrix();
            ubo.camPos = glm::vec4(camera->GetPosition(), 1.0f);
        }
        else {
            // Fallback: If no ECS component is active yet, fetch view matrices from the raw fly camera
            activeCam = cullingSystem.GetCamera();
            if (activeCam) {
                ubo.view = activeCam->getViewMatrix();
                ubo.camPos = glm::vec4(activeCam->getPosition(), 1.0f);
            }
        }

        // Build a precise, clean perspective matrix
        float fovRadians = glm::radians(45.0f);
        float aspect = static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        glm::mat4 projection = glm::perspective(fovRadians, aspect, 0.1f, 100.0f);

        // 🟩 CLEAN VULKAN Y-FLIP: Rotate the clip space coordinates by 180 degrees 
        // around the X-axis instead of multiplying matrix indices. This cleanly mirrors the viewport 
        // without shearing or distorting your meshes!
        glm::mat4 correction = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
        ubo.proj = correction * projection;

        // Set up lights with physically balanced intensities
        // Light 1: Pure White Light coming from slightly behind the camera view tracking
        ubo.lightPositions[0] = glm::vec4(0.0f, 10.0f, 8.0f, 1.0f);
        ubo.lightColors[0] = glm::vec4(45.0f, 45.0f, 45.0f, 1.0f);

        // Light 2: Soft Blue fill light coming from the left to handle shadows
        ubo.lightPositions[1] = glm::vec4(-10.0f, 2.0f, 0.0f, 1.0f);
        ubo.lightColors[1] = glm::vec4(5.0f, 5.0f, 15.0f, 1.0f);

        // Light 3: Soft Amber rim light coming from behind the room mesh structure
        ubo.lightPositions[2] = glm::vec4(0.0f, 1.0f, -10.0f, 1.0f);
        ubo.lightColors[2] = glm::vec4(15.0f, 10.0f, 5.0f, 1.0f);

        // Light 4: Fill Light
        ubo.lightPositions[3] = glm::vec4(0.0f, -5.0f, 0.0f, 1.0f);
        ubo.lightColors[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // Leave offline

        // Set camera position for view-dependent effects
        ubo.camPos = glm::vec4(camera ? camera->GetPosition() : glm::vec3(2.0f, 2.0f, 2.0f), 1.0f);

        // Set PBR parameters
        ubo.exposure = 1.0f;
        ubo.gamma = 2.2f;
        ubo.prefilteredCubeMipLevels = 1.0f;
        ubo.scaleIBLAmbient = 1.0f;

        // Copy the uniform buffer object to the device memory using vk::raii
        // With vk::raii, we can use the mapped memory directly
        // Perform memory copy into your mapped uniform block arrays safely
        auto& renderer = VulkanRenderer::Get();
        auto* cmdBufferUtil = renderer.GetCommandBuffer();
        if (currentFrame < cmdBufferUtil->uniformBuffersMapped.size() && cmdBufferUtil->uniformBuffersMapped[currentFrame] != nullptr) {
            memcpy(cmdBufferUtil->uniformBuffersMapped[currentFrame], &ubo, sizeof(PBRUniformBufferObject));
        }
    }

    // Push material properties to shader
    void SceneRenderer::pushMaterialProperties(vk::CommandBuffer commandBuffer, const Model* model, uint32_t materialIndex, const glm::mat4& globalMatrix) {
        // Get material from the model
        const Material& material = model->materials[materialIndex];

        PushConstantBlock pushConstants{};

        // 🟩 Pass the unique node matrix straight into the push constant block!
        pushConstants.modelMatrix = globalMatrix;

        pushConstants.baseColorFactor = material.baseColorFactor;
        pushConstants.metallicRoughnessFactors.x = material.metallicFactor;
        pushConstants.metallicRoughnessFactors.y = material.roughnessFactor;

        pushConstants.baseColorTextureSet = material.baseColorTextureIndex;
        pushConstants.physicalDescriptorTextureSet = material.metallicRoughnessTextureIndex;
        pushConstants.normalTextureSet = material.normalTextureIndex;
        pushConstants.occlusionTextureSet = material.occlusionTextureIndex;
        pushConstants.emissiveTextureSet = material.emissiveTextureIndex;
        pushConstants.alphaMask = (material.alphaMode == AlphaMode::MASK) ? 1.0f : 0.0f;
        pushConstants.alphaMaskCutoff = material.alphaCutoff;

        commandBuffer.pushConstants(
            *pbrPipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            sizeof(PushConstantBlock),
            &pushConstants
        );
    }

    bool SceneRenderer::CreateDeferredLightingPipeline()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            // 1. Load your separate compiled lighting pass bytecode stream
            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("assets/shaders/DeferredLighting.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            vk::PipelineShaderStageCreateInfo vertStageInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = *shaderModule,
                .pName = "VSMain"
            };

            vk::PipelineShaderStageCreateInfo fragStageInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = *shaderModule,
                .pName = "PSMain"
            };
            std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = { vertStageInfo, fragStageInfo };

            // CRITICAL DEFERRED FIX: Empty Vertex Input State Layout!
            // We bind no CPU structures because the shader builds the fullscreen canvas procedurally.
            vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
                .topology = vk::PrimitiveTopology::eTriangleList
            };

            vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
            std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()), .pDynamicStates = dynamicStates.data() };

            vk::PipelineRasterizationStateCreateInfo rasterizer{
                .polygonMode = vk::PolygonMode::eFill,
                .cullMode = vk::CullModeFlagBits::eNone, // Disable backface culling for full-screen quads
                .frontFace = vk::FrontFace::eCounterClockwise,
                .lineWidth = 1.0f
            };

            vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };

            // 3. 🟩 DISABLE DEPTH WRITING: Lighting pass uses 2D screen sampling
            vk::PipelineDepthStencilStateCreateInfo depthStencil{
                .depthTestEnable = vk::False,
                .depthWriteEnable = vk::False,
                .depthCompareOp = vk::CompareOp::eAlways
            };

            vk::PipelineColorBlendAttachmentState colorBlendAttachment{
                .blendEnable = vk::False,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 1, .pAttachments = &colorBlendAttachment };

            // Pack layouts using your existing UBO set layout properties
            //vk::DescriptorSetLayout pipelineSetLayouts[] = {
            //    *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout, // Set 0: Global Engine UBOs
            //    *descriptorSetLayout                                       // Set 1: G-Buffer textures samplers mapping
            //};
            //
            //vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 2, .pSetLayouts = pipelineSetLayouts };
            //m_DeferredPipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

            // 🟩 FIXED: Compile a dedicated 3-binding descriptor set layout specifically for your 3 G-Buffer inputs!
            std::array<vk::DescriptorSetLayoutBinding, 3> gBufferBindings;
            for (uint32_t b = 0; b < 3; ++b) {
                gBufferBindings[b] = vk::DescriptorSetLayoutBinding{
                    .binding = b,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .descriptorCount = 1,
                    .stageFlags = vk::ShaderStageFlagBits::eFragment
                };
            }
            vk::DescriptorSetLayoutCreateInfo gBufferLayoutInfo{
                .bindingCount = static_cast<uint32_t>(gBufferBindings.size()),
                .pBindings = gBufferBindings.data()
            };

            // Allocate a separate layout handle variable (ensure you declare vk::raii::DescriptorSetLayout m_DeferredSetLayout{nullptr}; in your class header private section)
            m_DeferredSetLayout = device.createDescriptorSetLayout(gBufferLayoutInfo);

            // Pack layouts for your deferred lighting pipeline
            vk::DescriptorSetLayout deferredPipelineSetLayouts[] = {
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout, // Set 0: Global Engine UBOs
                *m_DeferredSetLayout                                       // Set 1: 3 G-Buffer attachments
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ .setLayoutCount = 2, .pSetLayouts = deferredPipelineSetLayouts };
            m_DeferredPipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);

            vk::GraphicsPipelineCreateInfo pipelineInfo{
                .stageCount = static_cast<uint32_t>(shaderStages.size()),
                .pStages = shaderStages.data(),
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *m_DeferredPipelineLayout
            };

            // Link straight to backbuffer swapchain format requirements for final output
            vk::PipelineRenderingCreateInfo renderingInfo;
            renderingInfo.setColorAttachmentCount(1).setPColorAttachmentFormats(&swapChainImageFormat);
            pipelineInfo.setPNext(&renderingInfo);

            m_DeferredPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[DEFERRED ERROR]: Pipeline initialization skipped: " << e.what() << std::endl;
            return false;
        }
    }

    void SceneRenderer::SetupRenderPasses()
    {
        // Fetch the active renderer manager instance context
        auto& renderer = VulkanRenderer::Get();
        auto* globalManager = renderer.GetRenderPassManager(); // Add a public getter for m_RenderPassManager in VulkanRenderer if needed

        // Register passes straight to the global manager executing in drawFrame!
        auto geometryPass = globalManager->AddRenderPass<GeometryPass>("GeometryPass", &cullingSystem, this);
        auto lightingPass = globalManager->AddRenderPass<LightingPass>("LightingPass", geometryPass, this);
        auto postProcessPass = globalManager->AddRenderPass<PostProcessPass>("PostProcessPass", lightingPass);
        // Add post-process effects
        // ...
    }

    void SceneRenderer::RenderModelNode(Node* node, const glm::mat4& parentMatrix, vk::raii::CommandBuffer& cmd, const Model* model, CameraComponent* camera)
    {
        if (!node) return;

        glm::mat4 globalMatrix = parentMatrix * node->getLocalMatrix();

        if (node->mesh && node->mesh->IsLoaded()) {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

            // 🟩 Forward the global matrix straight to the material push constants block!
            pushMaterialProperties(*cmd, model, node->materialIndex, globalMatrix);

            auto& material = const_cast<Model*>(model)->materials[node->materialIndex];

            // Bind and draw
            VkDescriptorSet set0 = !lowLevelRenderer.vulkanCommandBuffer->descriptorSets.empty() ?
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame] : VK_NULL_HANDLE;
            VkDescriptorSet set1 = *material.descriptorSet;

            if (set0 != VK_NULL_HANDLE && set1 != VK_NULL_HANDLE) {
                VkDescriptorSet rawSets[] = { set0, set1 };
                vk::ArrayProxy<const vk::DescriptorSet> setsProxy(2, reinterpret_cast<const vk::DescriptorSet*>(rawSets));
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrPipelineLayout, 0, setsProxy, {});
            }
            else if (set0 != VK_NULL_HANDLE) {
                // 🟩 FALLBACK: If set1 is missing, bind just Set 0 (UBO) so geometry still renders in solid color!
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrPipelineLayout, 0, { vk::DescriptorSet(set0) }, {});
            }

            cmd.bindVertexBuffers(0, { *node->mesh->GetVertexBuffer() }, { 0 });
            cmd.bindIndexBuffer(*node->mesh->GetIndexBuffer(), 0, vk::IndexType::eUint32);
            cmd.drawIndexed(node->mesh->GetIndexCount(), 1, 0, 0, 0);
        }

        for (auto child : node->children) {
            RenderModelNode(child, globalMatrix, cmd, model, camera);
        }
    }

    GeometryPass::GeometryPass(const std::string& name, CullingSystem* culling, SceneRenderer* renderer)
        : RenderPass(name), cullingSystem(culling), sceneRenderer(renderer)
    {
        // Create G-buffer render targets matching standard workspace display layouts
        gBuffer = new RenderTarget(1920, 1080, renderer->device); // Example resolution
        SetRenderTarget(gBuffer);
    }

    GeometryPass::~GeometryPass()
    {
        delete gBuffer;
    }

    void GeometryPass::BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        auto& renderer = VulkanRenderer::Get();
        auto* swapchainSubsystem = renderer.vulkanSwapChain;
        uint32_t width = swapchainSubsystem->swapChainExtent.width;
        uint32_t height = swapchainSubsystem->swapChainExtent.height;

        vk::ClearValue clearColor = vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.15f, 1.0f});
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

        // Transition layouts for all 3 images via a pipeline barrier array
        std::vector<vk::ImageMemoryBarrier> barriers;
        auto addColorBarrier = [&](vk::Image img) {
            barriers.push_back(vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = img,
                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
                });
            };

        // Gather images from the active target context
        addColorBarrier(gBuffer->GetPositionImage());
        addColorBarrier(gBuffer->GetNormalImage());
        addColorBarrier(gBuffer->GetAlbedoImage());

        // Separate depth attachment barrier track
        barriers.push_back(vk::ImageMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = gBuffer->GetDepthImage(),
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
            });

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
            vk::DependencyFlags(), {}, {}, barriers
        );

        // Assemble MRT attachment configurations
        std::array<vk::RenderingAttachmentInfo, 3> colorAttachments;

        colorAttachments[0].setImageView(gBuffer->GetPositionImageView())
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(clearColor);

        colorAttachments[1].setImageView(gBuffer->GetNormalImageView())
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(clearColor);

        colorAttachments[2].setImageView(gBuffer->GetAlbedoImageView())
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(clearColor);

        vk::RenderingAttachmentInfo depthAttachment;
        depthAttachment.setImageView(gBuffer->GetDepthImageView())
            .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(clearDepth);

        vk::RenderingInfo renderingInfo;
        renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { width, height }))
            .setLayerCount(1)
            .setColorAttachmentCount(static_cast<uint32_t>(colorAttachments.size()))
            .setPColorAttachments(colorAttachments.data())
            .setPDepthAttachment(&depthAttachment);

        commandBuffer.beginRendering(renderingInfo);
    }

    void GeometryPass::Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // 1. Cull the scene using your tracking camera parameters
        cullingSystem->CullScene(Entity::GetActiveSceneEntities());
        const auto& visibleEntities = cullingSystem->GetVisibleEntities();

        // FIXED DISCONNECT: Forward drawing execution directly to your cached SceneRenderer handle pointer!
        // This forces your object tree to bind pipelines, update UBO bindings, and stream transforms.
        sceneRenderer->Render(visibleEntities, commandBuffer, imageIndex);
    }

    void GeometryPass::EndPass(vk::raii::CommandBuffer& commandBuffer)
    {
        // End dynamic rendering
        commandBuffer.endRendering();
    }


    LightingPass::LightingPass(const std::string& name, GeometryPass* gPass, SceneRenderer* renderer) : RenderPass(name), geometryPass(gPass), sceneRenderer(renderer)
    {
        // Add dependency on geometry pass
        AddDependency(gPass->GetName());
    }

    void LightingPass::BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        auto& lowLevelRenderer = VulkanRenderer::Get();
        auto* swapchainSubsystem = lowLevelRenderer.vulkanSwapChain;
        uint32_t currentFrame = swapchainSubsystem->GetFrameIndex();

        // Query active window size bounds dynamically
        uint32_t width = swapchainSubsystem->swapChainExtent.width;
        uint32_t height = swapchainSubsystem->swapChainExtent.height;

        // 🟩 FIX A: Fetch the real raw backbuffer image corresponding to this frame slot
        // (We extract this out using the imageIndex passed down from your draw frame loops)
        // Note: If imageIndex isn't tracked here, pull it safely from lowLevelRenderer or cached indices
        vk::Image swapchainBackbufferImage = swapchainSubsystem->swapChainImages[imageIndex];

        // 🟩 FIX B: TRANSITION THE BACKBUFFER FROM PRESENT/UNDEFINED TO WRITABLE ATTACHMENT OPTIMAL
        // This bridges the gap and satisfies the exact driver requirements blocking your display output!
        vk::ImageMemoryBarrier barrierToComposeColor{
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .oldLayout = vk::ImageLayout::eUndefined, // Force overwrite old frame presentation contents cleanly
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainBackbufferImage,
            .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}
        };

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::DependencyFlags(), {}, {}, { barrierToComposeColor }
        );

        // Configure your color attachment point to write directly to the hardware backbuffer view
        vk::RenderingAttachmentInfoKHR colorAttachment;
        colorAttachment.setImageView(swapchainSubsystem->swapChainImageViews[imageIndex]) // 🟩 Output straight onto backbuffer!
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear) // Clear out previous background tracks safely
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.15f, 1.0f}));

        vk::RenderingInfoKHR renderingInfo;
        renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { width, height }))
            .setLayerCount(1)
            .setColorAttachmentCount(1)
            .setPColorAttachments(&colorAttachment)
            .setPDepthAttachment(nullptr); // Screen composition quad requires no depth attachment tracking!

        commandBuffer.beginRendering(renderingInfo);
    }

    void LightingPass::Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        auto& lowLevelRenderer = VulkanRenderer::Get();
        auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
        uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

        auto* gBufferTarget = geometryPass->GetGBufferTarget();
        if (!gBufferTarget) return;

        // 1. Transition all 3 G-Buffer layouts to Read-Only
        auto transitionToRead = [&](vk::Image img) {
            vk::ImageMemoryBarrier b{ .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite, .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal, .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = img, .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1} };
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { b });
            };
        transitionToRead(gBufferTarget->GetPositionImage());
        transitionToRead(gBufferTarget->GetNormalImage());
        transitionToRead(gBufferTarget->GetAlbedoImage());

        // 2. Bind Pipeline & Config Viewports
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, sceneRenderer->GetDeferredPipeline());

        float width = static_cast<float>(lowLevelRenderer.vulkanSwapChain->swapChainExtent.width);
        float height = static_cast<float>(lowLevelRenderer.vulkanSwapChain->swapChainExtent.height);
        commandBuffer.setViewport(0, { vk::Viewport(0.0f, 0.0f, width, height, 0.0f, 1.0f) });
        commandBuffer.setScissor(0, { vk::Rect2D(vk::Offset2D(0, 0), lowLevelRenderer.vulkanSwapChain->swapChainExtent) });

        // 3. Allocate Set 1 and write all 3 views into continuous descriptor bindings
        vk::DescriptorSetLayout deferredLayout = sceneRenderer->GetDeferredSetLayout();
        vk::DescriptorSetAllocateInfo allocInfo{ .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool, .descriptorSetCount = 1, .pSetLayouts = &deferredLayout };
        auto gBufferDescriptorSets = logicalDevice.allocateDescriptorSets(allocInfo);
        vk::raii::DescriptorSet gBufferSet = std::move(gBufferDescriptorSets.front());

        vk::Sampler textureSampler = lowLevelRenderer.imGui->GetSampler();

        std::array<vk::DescriptorImageInfo, 3> imageInfos = {
            vk::DescriptorImageInfo{textureSampler, gBufferTarget->GetPositionImageView(), vk::ImageLayout::eShaderReadOnlyOptimal},
            vk::DescriptorImageInfo{textureSampler, gBufferTarget->GetNormalImageView(), vk::ImageLayout::eShaderReadOnlyOptimal},
            vk::DescriptorImageInfo{textureSampler, gBufferTarget->GetAlbedoImageView(), vk::ImageLayout::eShaderReadOnlyOptimal}
        };

        std::array<vk::WriteDescriptorSet, 3> writes;
        for (uint32_t b = 0; b < 3; ++b) {
            writes[b] = vk::WriteDescriptorSet{ .dstSet = *gBufferSet, .dstBinding = b, .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfos[b] };
        }
        logicalDevice.updateDescriptorSets(writes, {});

        // 4. Bind descriptor sets and draw full screen composition triangle
        VkDescriptorSet layoutsArray[] = { *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame], *gBufferSet };
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, sceneRenderer->GetDeferredPipelineLayout(), 0, vk::ArrayProxy<const vk::DescriptorSet>(2, reinterpret_cast<const vk::DescriptorSet*>(layoutsArray)), {});

        commandBuffer.draw(3, 1, 0, 0);
    }

    void LightingPass::EndPass(vk::raii::CommandBuffer& commandBuffer)
    {
        // End dynamic rendering
        commandBuffer.endRendering();
    }

    void CullingSystem::CullScene(const std::vector<Entity*>& allEntities)
    {
        visibleEntities.clear();

        if (!camera) {
            for (auto entity : allEntities) {
                if (!entity || !entity->IsActive()) continue;

                // FIXED 1: Support modern ModelComponents in the fallback path
                if (entity->GetComponent<ModelComponent>() || entity->GetComponent<MeshComponent>()) {
                    visibleEntities.push_back(entity);
                }
            }
            return;
        }

        Frustum frustum = camera->GetFrustum();
        for (auto entity : allEntities) {
            if (!entity || !entity->IsActive()) continue;

            // FIXED 2: Check for ModelComponent first
            auto modelComponent = entity->GetComponent<ModelComponent>();
            auto meshComponent = entity->GetComponent<MeshComponent>();

            if (!modelComponent && !meshComponent) continue;

            auto transformComponent = entity->GetComponent<TransformComponent>();
            if (!transformComponent) continue;

            // Bounding Box Lookup
            BoundingBox boundingBox;
            if (modelComponent) {
                // If your engine handles model bounds, extract them here, or use a default visible box to guarantee it renders immediately:
                boundingBox = BoundingBox(glm::vec3(-100.0f), glm::vec3(100.0f));
            }
            else {
                boundingBox = meshComponent->GetBoundingBox();
            }

            boundingBox.SetTransform(transformComponent->GetLocalTransformMatrix());

            if (frustum.Intersects(boundingBox)) {
                visibleEntities.push_back(entity);
            }
        }
    }

    void Rendergraph::AddResource(const std::string& name, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage, vk::ImageLayout initialLayout, vk::ImageLayout finalLayout)
    {
        resources.try_emplace(name, Resource{   // Register in the global resource map
            .name = name,
            .format = format,
            .extent = extent,
            .usage = usage,
            .initialLayout = initialLayout,
            .finalLayout = finalLayout
            });
    }

    void Rendergraph::AddPass(const std::string& name, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs, std::function<void(vk::raii::CommandBuffer&, uint32_t)> executeFunc)

    {
        Pass pass;
        pass.name = name;                        // Assign descriptive identifier
        pass.inputs = inputs;                    // List all resources this pass reads
        pass.outputs = outputs;                  // List all resources this pass writes
        pass.executeFunc = executeFunc;          // Store the actual rendering implementation

        passes.push_back(pass);                  // Add to the ordered pass list
    }

    void Rendergraph::Compile()
    {
        // Dependency Graph Construction. Build bidirectional dependency relationships between passes
        std::vector<std::vector<size_t>> dependencies(passes.size());  // What each pass depends on
        std::vector<std::vector<size_t>> dependents(passes.size());    // What depends on each pass

        // Track which pass produces each resource (write-after-write dependencies)
        std::unordered_map<std::string, size_t> resourceWriters;

        // Resource Production Registration
        // Register every pass's outputs *before* looking at any inputs, in its own pass over
        // `passes`. Producers and consumers can appear in any order in the `passes` vector -
        // a pass added later might produce a resource a pass added earlier consumes - so
        // dependency discovery below must not assume producers come first positionally.
        for (size_t i = 0; i < passes.size(); ++i) {
            for (const auto& output : passes[i].outputs) {
                resourceWriters[output] = i;                    // Record this pass as producer
            }
        }

        // Dependency Discovery Through Resource Usage Analysis
        // Now that every producer is known, analyze each pass's inputs to determine data flow
        // relationships, regardless of where the producer sits in the passes vector.
        for (size_t i = 0; i < passes.size(); ++i) {
            const auto& pass = passes[i];

            // Process input dependencies - this pass must wait for producers
            for (const auto& input : pass.inputs) {
                auto it = resourceWriters.find(input);
                if (it != resourceWriters.end()) {
                    // Found the pass that produces this input - create dependency link
                    dependencies[i].push_back(it->second);      // This pass depends on the producer
                    dependents[it->second].push_back(i);        // Producer has this as dependent
                }
            }
        }

        // Topological Sort for Optimal Execution Order
        // Use depth-first search to compute valid execution sequence while detecting cycles
        std::vector<bool> visited(passes.size(), false);       // Track completed nodes
        std::vector<bool> inStack(passes.size(), false);       // Track current recursion path

        std::function<void(size_t)> visit = [&](size_t node) {
            if (inStack[node]) {
                // Cycle detection - circular dependency found
                throw std::runtime_error("Cycle detected in rendergraph");
            }

            if (visited[node]) {
                return;  // Already processed this node and its dependencies
            }

            inStack[node] = true;   // Mark as currently being processed

            // Recursively process all passes this one depends on first (post-order traversal), so a producer is always pushed onto executionOrder before its consumers.
            for (auto dependency : dependencies[node]) {
                visit(dependency);
            }

            inStack[node] = false;  // Remove from current path
            visited[node] = true;   // Mark as completely processed
            executionOrder.push_back(node);  // Add to execution sequence
            };

        // Process all unvisited nodes to handle disconnected graph components
        for (size_t i = 0; i < passes.size(); ++i) {
            if (!visited[i]) {
                visit(i);
            }
        }

        // Automatic Synchronization Object Creation. Generate semaphores for all dependencies identified during analysis
        //for (size_t i = 0; i < passes.size(); ++i) {
        //    for (auto dep : dependencies[i]) {
        //        // Create a GPU semaphore for this dependency relationship. The dependent pass will wait on this semaphore before executing
        //        semaphores.emplace_back(device.createSemaphore({}));
        //        semaphoreSignalWaitPairs.emplace_back(dep, i);    // (producer, consumer) pair
        //    }
        //}

        // Physical Resource Allocation and Creation. Transform resource descriptions into actual GPU objects
        for (auto& [name, resource] : resources) {
            // Configure image creation parameters based on resource description
            vk::ImageCreateInfo imageInfo;
            imageInfo.setImageType(vk::ImageType::e2D)                    // 2D texture/render target
                .setFormat(resource.format)                          // Pixel format from description
                .setExtent({ resource.extent.width, resource.extent.height, 1 })  // Dimensions
                .setMipLevels(1)                                      // Single mip level for simplicity
                .setArrayLayers(1)                                    // Single layer (not array texture)
                .setSamples(vk::SampleCountFlagBits::e1)              // No multisampling
                .setTiling(vk::ImageTiling::eOptimal)                 // GPU-optimal memory layout
                .setUsage(resource.usage)                             // Usage flags from registration
                .setSharingMode(vk::SharingMode::eExclusive)          // Single queue family access
                .setInitialLayout(vk::ImageLayout::eUndefined);       // Initial layout (will be transitioned)

            resource.image = device.createImage(imageInfo);               // Create the GPU image object

            // Allocate backing memory for the image
            vk::MemoryRequirements memRequirements = resource.image.getMemoryRequirements();

            vk::MemoryAllocateInfo allocInfo;
            allocInfo.setAllocationSize(memRequirements.size)             // Required memory size
                .setMemoryTypeIndex(FindMemoryType(memRequirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eDeviceLocal));         // GPU-local memory

            resource.memory = device.allocateMemory(allocInfo);           // Allocate GPU memory
            resource.image.bindMemory(*resource.memory, 0);               // Bind memory to image

            // Create image view for shader access
            vk::ImageAspectFlags aspect = (name == "Depth") ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
            vk::ImageViewCreateInfo viewInfo;
            viewInfo.setImage(*resource.image)                          // Reference the created image
                .setViewType(vk::ImageViewType::e2D)                    // 2D view type
                .setFormat(resource.format)                             // Match image format
                .setSubresourceRange({ aspect, 0, 1, 0, 1 });  // Full image access

            resource.view = device.createImageView(viewInfo);           // Create shader-accessible view
        }
    }

    void Rendergraph::Execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue, uint32_t imageIndex)
    {
        for (auto passIdx : executionOrder) {
            const auto& pass = passes[passIdx];

            // 1. Transition input resources to shader-readable layouts
            for (const auto& input : pass.inputs) {
                auto& resource = resources[input];
                bool isDepth = (resource.name == "Depth");

                vk::ImageLayout targetReadLayout = isDepth
                    ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                    : vk::ImageLayout::eShaderReadOnlyOptimal;

                // 🟩 FIX: Transition from resource.finalLayout (what the producer pass wrote), NOT initialLayout (eUndefined)!
                // Using eUndefined discards the rendered G-Buffer contents!
                vk::ImageMemoryBarrier barrier;
                barrier.setOldLayout(resource.finalLayout)
                    .setNewLayout(targetReadLayout)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(*resource.image)
                    .setSubresourceRange({ (isDepth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), 0, 1, 0, 1 })
                    .setSrcAccessMask(isDepth ? vk::AccessFlagBits::eDepthStencilAttachmentWrite : vk::AccessFlagBits::eColorAttachmentWrite)
                    .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

                vk::PipelineStageFlags srcStage = isDepth
                    ? vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests
                    : vk::PipelineStageFlagBits::eColorAttachmentOutput;

                commandBuffer.pipelineBarrier(
                    srcStage, vk::PipelineStageFlagBits::eFragmentShader,
                    vk::DependencyFlagBits::eByRegion, {}, {}, { barrier }
                );
            }

            // 2. Transition output resources to render target layouts
            for (const auto& output : pass.outputs) {
                auto& resource = resources[output];
                bool isDepth = (resource.name == "Depth");

                // 🟩 FIXED: Dynamically route the proper layout types!
                vk::ImageLayout targetLayout = isDepth ? vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eColorAttachmentOptimal;
                vk::AccessFlags targetAccess = isDepth ? vk::AccessFlagBits::eDepthStencilAttachmentWrite : vk::AccessFlagBits::eColorAttachmentWrite;

                vk::ImageMemoryBarrier barrier;
                barrier.setOldLayout(resource.initialLayout)
                    .setNewLayout(targetLayout)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(*resource.image)
                    .setSubresourceRange({ (isDepth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), 0, 1, 0, 1 })
                    .setSrcAccessMask(vk::AccessFlagBits::eMemoryRead)
                    .setDstAccessMask(targetAccess);

                vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
                vk::PipelineStageFlags dstStage = isDepth ?
                    vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests :
                    vk::PipelineStageFlagBits::eColorAttachmentOutput;

                commandBuffer.pipelineBarrier(
                    srcStage, dstStage, vk::DependencyFlagBits::eByRegion, {}, {}, { barrier }
                );
            }

            // 3. Execute the pass lambda (GeometryPass / LightingPass)
            pass.executeFunc(commandBuffer, imageIndex);

            // 4. Transition output resources to their final required layouts
            for (const auto& output : pass.outputs) {
                auto& resource = resources[output];
                bool isDepth = (resource.name == "Depth");

                vk::ImageLayout currentLayout = isDepth ? vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eColorAttachmentOptimal;
                vk::AccessFlags srcAccess = isDepth ? vk::AccessFlagBits::eDepthStencilAttachmentWrite : vk::AccessFlagBits::eColorAttachmentWrite;

                vk::ImageMemoryBarrier barrier;
                barrier.setOldLayout(currentLayout)
                    .setNewLayout(resource.finalLayout)
                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                    .setImage(*resource.image)
                    .setSubresourceRange({ (isDepth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor), 0, 1, 0, 1 })
                    .setSrcAccessMask(srcAccess)
                    .setDstAccessMask(vk::AccessFlagBits::eMemoryRead);

                vk::PipelineStageFlags srcStage = isDepth ?
                    vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests :
                    vk::PipelineStageFlagBits::eColorAttachmentOutput;

                commandBuffer.pipelineBarrier(
                    srcStage, vk::PipelineStageFlagBits::eAllCommands,
                    vk::DependencyFlagBits::eByRegion, {}, {}, { barrier }
                );
            }
        }
    }

    void Rendergraph::TransitionImageLayout(vk::raii::CommandBuffer& commandBuffer, vk::Image image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
    {

        // Configure the basic image memory barrier structure. This barrier will coordinate memory access and layout transitions
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(oldLayout)                                        // Current image layout state
            .setNewLayout(newLayout)                                        // Target layout after transition
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)                // No queue family ownership transfer
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)                // Same queue family throughout
            .setImage(image)                                                // Target image for the transition
            .setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }); // Full color image range

        // Initialize pipeline stage tracking for synchronization timing. These stages define when operations must complete and when new operations can begin
        vk::PipelineStageFlags sourceStage;      // When previous operations must finish
        vk::PipelineStageFlags destinationStage; // When subsequent operations can start

        // Configure synchronization for undefined-to-transfer layout transitions
        // This pattern is common when preparing images for data uploads
        if (oldLayout == vk::ImageLayout::eUndefined &&
            newLayout == vk::ImageLayout::eTransferDstOptimal) {

            // Configure memory access permissions for upload preparation
            barrier.setSrcAccessMask(vk::AccessFlagBits::eNone)                // No previous access to synchronize
                .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);      // Enable transfer write operations

            // Set pipeline stage synchronization points for upload workflow
            sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;               // No previous work to wait for
            destinationStage = vk::PipelineStageFlagBits::eTransfer;           // Transfer operations can proceed

            // Configure synchronization for transfer-to-shader layout transitions
            // This pattern prepares uploaded images for shader sampling
        }
        else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
            newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {

            // Configure memory access transition from writing to reading
            barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)       // Previous transfer writes must complete
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);         // Enable shader read access

            // Set pipeline stage synchronization for shader usage workflow
            sourceStage = vk::PipelineStageFlagBits::eTransfer;                // Transfer operations must complete
            destinationStage = vk::PipelineStageFlagBits::eFragmentShader;     // Fragment shaders can access

        }
        else {
            // Handle unsupported transition combinations
            // Production code would include additional common transition patterns
            throw std::invalid_argument("Unsupported layout transition!");
        }

        // Execute the pipeline barrier with configured synchronization
        // This commits the layout transition and memory synchronization to the command buffer
        commandBuffer.pipelineBarrier(
            sourceStage,                                                       // Wait for these operations to complete
            destinationStage,                                                  // Before allowing these operations to begin
            vk::DependencyFlagBits::eByRegion,                                 // Enable region-local optimizations
            nullptr,                                                        // No memory barriers needed
            nullptr,                                                        // No buffer barriers needed
            barrier                                                        // Apply our image memory barrier
        );
    }

    void Rendergraph::SetupDeferredRenderer(Rendergraph& graph, uint32_t width, uint32_t height)
    {

        // Configure position buffer for world-space vertex positions
        // High precision format preserves positional accuracy for lighting calculations
        graph.AddResource("GBuffer_Position", vk::Format::eR16G16B16A16Sfloat, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        // Configure normal buffer for surface orientation data
        // High precision normals enable accurate lighting and reflection calculations
        graph.AddResource("GBuffer_Normal", vk::Format::eR16G16B16A16Sfloat, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        // Configure albedo buffer for surface color information
        // Standard 8-bit precision sufficient for color data with gamma encoding
        graph.AddResource("GBuffer_Albedo", vk::Format::eR8G8B8A8Unorm, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        // Configure depth buffer for occlusion and depth testing
        // High precision depth enables accurate depth reconstruction in lighting pass
        graph.AddResource("Depth", vk::Format::eD32Sfloat, { width, height },
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

        // Configure final color buffer for the completed lighting result
        // Standard color format with transfer capability for presentation or post-processing
        graph.AddResource("FinalColor", vk::Format::eB8G8R8A8Srgb, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal);


        // Configure geometry pass for G - Buffer population
        // This pass renders all geometry and stores intermediate data for lighting calculations
        graph.AddPass("GeometryPass",
            {},                                                        // No input dependencies - first pass in pipeline
            { "GBuffer_Position", "GBuffer_Normal", "GBuffer_Albedo", "Depth" },  // Outputs all G-Buffer components
            [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
            {
                // 1. Fetch the compiled resource descriptors directly from your rendergraph map pointers
                auto* posRes = graph.GetResource("GBuffer_Position");
                auto* normalRes = graph.GetResource("GBuffer_Normal");
                auto* albedoRes = graph.GetResource("GBuffer_Albedo");
                auto* depthRes = graph.GetResource("Depth");

                // Ensure resources are compiled before setting up attachment views
                if (posRes && normalRes && albedoRes && depthRes)
                {
                    // Configure multiple render target attachments for G-Buffer output
                    // Each attachment corresponds to a different geometric property
                    std::array<vk::RenderingAttachmentInfoKHR, 3> colorAttachments;

                    // Configure position attachment - world space vertex positions
                    vk::ClearValue clearColor = vk::ClearColorValue(std::array<float, 4>{ 0.1f, 0.1f, 0.15f, 1.0f });
                    // Configure position attachment - Dereference the view handle safely using the * operator
                    colorAttachments[0].setImageView(*posRes->view)    // Target position buffer
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)  // Optimal for writes
                        .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to known state
                        .setStoreOp(vk::AttachmentStoreOp::eStore)    // Preserve for lighting pass
                        .setClearValue(clearColor);

                    // Configure normal attachment - surface normals in world space
                    colorAttachments[1].setImageView(*normalRes->view)      // Target normal buffer
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to default normal
                        .setStoreOp(vk::AttachmentStoreOp::eStore)    // Preserve for lighting
                        .setClearValue(clearColor);

                    // Configure albedo attachment - surface color and material properties
                    colorAttachments[2].setImageView(*albedoRes->view)      // Target albedo buffer
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to default color
                        .setStoreOp(vk::AttachmentStoreOp::eStore)    // Preserve for lighting
                        .setClearValue(clearColor);

                    // Configure depth attachment for occlusion culling
                    vk::RenderingAttachmentInfoKHR depthAttachment;
                    depthAttachment.setImageView(*depthRes->view)                   // Target depth buffer
                        .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)  // Optimal for depth ops
                        .setLoadOp(vk::AttachmentLoadOp::eClear)           // Clear to far plane
                        .setStoreOp(vk::AttachmentStoreOp::eStore)         // Preserve for lighting pass
                        .setClearValue(vk::ClearDepthStencilValue(1.0f, 0)); // Clean depth initialization                        // Clear to maximum depth

                    // Assemble complete rendering configuration
                    vk::RenderingInfoKHR renderingInfo;
                    renderingInfo.setRenderArea({ {0, 0}, {width, height} })          // Full screen rendering
                        .setLayerCount(1)                                   // Single layer rendering
                        .setColorAttachmentCount(static_cast<uint32_t>(colorAttachments.size()))  // Number of G-Buffer targets
                        .setPColorAttachments(colorAttachments.data())     // G-Buffer attachment array
                        .setPDepthAttachment(&depthAttachment);            // Depth testing configuration

                    // Execute geometry rendering with dynamic rendering
                    cmd.beginRendering(renderingInfo);                              // Begin G-Buffer population
                }
                // Bind geometry pipeline and render all scene objects
                // Each draw call populates position, normal, and albedo for visible fragments
                // ... (geometry rendering implementation would go here)
                
                // 🟩 VUID FIX: Apply dynamic state properties explicitly inside the open pass context!
                float fWidth = static_cast<float>(width);
                float fHeight = static_cast<float>(height);
                cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, fWidth, fHeight, 0.0f, 1.0f) });
                cmd.setScissor(0, { vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D{width, height}) });

                 // LINK THE RAW GEOMETRY INSTRUCTION DRAWING LOOP HERE
                auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                // Query visible objects tree and bind your compiled PBR shader stage
                pbrRenderer.Render(Entity::GetActiveSceneEntities(), cmd, imageIndex);

                cmd.endRendering();                                             // Complete G-Buffer population
            });

        // Configure lighting pass for screen-space illumination calculations
        // This pass reads G-Buffer data and computes final lighting for each pixel
        graph.AddPass("LightingPass",
            { "GBuffer_Position", "GBuffer_Normal", "GBuffer_Albedo", "Depth" },  // Read all G-Buffer components
            { "FinalColor" },                                            // Output final lit result
            [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
            {
                auto* finalRes = graph.GetResource("FinalColor");
                // Configure single color output for final lighting result
                vk::RenderingAttachmentInfoKHR colorAttachment;
                colorAttachment.setImageView(finalRes->view)             // Target final color buffer
                    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)  // Optimal for color writes
                    .setLoadOp(vk::AttachmentLoadOp::eClear)          // Clear to background color
                    .setStoreOp(vk::AttachmentStoreOp::eStore)        // Preserve final result
                    .setClearValue(vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.15f, 1.0f}));        // Clear to black background

                // Configure lighting pass rendering without depth testing
                // Depth testing unnecessary since we're processing each pixel exactly once
                vk::RenderingInfoKHR renderingInfo;
                renderingInfo.setRenderArea({ {0, 0}, {width, height} })          // Full screen processing
                    .setLayerCount(1)                                   // Single layer output
                    .setColorAttachmentCount(1)                        // Single color output
                    .setPColorAttachments(&colorAttachment);           // Final color attachment

                // Execute screen-space lighting calculations
                cmd.beginRendering(renderingInfo);                              // Begin lighting pass

                // Bind lighting pipeline and draw full-screen quad
                // Fragment shader reads G-Buffer textures and computes lighting for each pixel
                // All scene lights are processed in a single screen-space pass
                // ... (lighting calculation implementation would go here)
                // 4. 🟩 LINK THE PROCEDURAL FULL-SCREEN COMPOSITION PASS SHADER HERE

                 // 🟩 VUID FIX: Apply dynamic state properties explicitly for full-screen quad processing!
                float fWidth = static_cast<float>(width);
                float fHeight = static_cast<float>(height);
                cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, fWidth, fHeight, 0.0f, 1.0f) });
                cmd.setScissor(0, { vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D{width, height}) });

                auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pbrRenderer.GetDeferredPipeline());

                // Pass global engine uniform descriptor tracks 
                //auto& lowLevelRenderer = VulkanRenderer::Get();
                //uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();
                //cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pbrRenderer.GetDeferredPipelineLayout(), 0,
                //    { *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame] }, {});

                auto* posRes = graph.GetResource("GBuffer_Position");
                auto* normalRes = graph.GetResource("GBuffer_Normal");
                auto* albedoRes = graph.GetResource("GBuffer_Albedo");

                auto& lowLevelRenderer = VulkanRenderer::Get();
                auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

                // 🟩 Allocate and write ONLY ONCE so the handle stays alive while the GPU executes!
                if (!*pbrRenderer.m_DeferredGBufferSet) {
                    vk::DescriptorSetLayout deferredLayout = pbrRenderer.GetDeferredSetLayout();
                    vk::DescriptorSetAllocateInfo allocInfo{
                        .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                        .descriptorSetCount = 1,
                        .pSetLayouts = &deferredLayout
                    };
                    pbrRenderer.m_DeferredGBufferSet = std::move(logicalDevice.allocateDescriptorSets(allocInfo).front());

                    vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();
                    std::array<vk::DescriptorImageInfo, 3> imageInfos = {
                        vk::DescriptorImageInfo{ sampler, *posRes->view,    vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *normalRes->view, vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *albedoRes->view, vk::ImageLayout::eShaderReadOnlyOptimal }
                    };

                    std::array<vk::WriteDescriptorSet, 3> writes;
                    for (uint32_t b = 0; b < 3; ++b) {
                        writes[b] = vk::WriteDescriptorSet{
                            .dstSet = *pbrRenderer.m_DeferredGBufferSet,
                            .dstBinding = b,
                            .descriptorCount = 1,
                            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                            .pImageInfo = &imageInfos[b]
                        };
                    }
                    logicalDevice.updateDescriptorSets(writes, {});
                }

                // Bind both persistent sets safely
                std::array<vk::DescriptorSet, 2> sets = {
                    *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame],
                    *pbrRenderer.m_DeferredGBufferSet
                };
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pbrRenderer.GetDeferredPipelineLayout(), 0, sets, {});

                // Process full-screen procedural triangle composition draw(3)
                cmd.draw(3, 1, 0, 0);

                cmd.endRendering();                                             // Complete lighting calculations
            });

        // Compile the complete rendergraph for execution
        // This analyzes dependencies and generates optimal execution plan
        graph.Compile();
    }

    uint32_t Rendergraph::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
    {
        // Fetch the active low-level hardware device details via your central registry context
        auto& renderer = VulkanRenderer::Get();
        auto& physicalDevice = renderer.vulkanContext->GetDevice().physicalDevice;

        // Query the physical hardware's available memory banks and properties
        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            // 1. Check if the specific bit in the typeFilter is set for this memory type index
            // 2. Check if the memory types flags match our requested hardware requirements (e.g. DeviceLocal)
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i; // 🟩 Return the exact matching type index found!
            }
        }

        throw std::runtime_error("[FATAL MEMORY ERROR]: Failed to discover a compatible hardware memory type index for G-Buffer targets!");
    }

    RenderTarget::RenderTarget(uint32_t w, uint32_t h, vk::raii::Device& dev) : width(w), height(h), m_ContextDevice(dev)
    {
        // Create color and depth images
        CreateColorResources();
        CreateDepthResources();

        // Note: With dynamic rendering, we don't need to create VkRenderPass
        // or VkFramebuffer objects. Instead, we just create the images and
        // image views that will be used directly with vkCmdBeginRendering.
    }

    void RenderTarget::CreateColorResources()
    {
        auto& renderer = VulkanRenderer::Get();
        auto* imgUtil = renderer.GetVulkanImageSubsystem();

        vk::Format hdrFormat = vk::Format::eR16G16B16A16Sfloat; // High precision for spatial data vectors
        vk::Format srgbFormat = vk::Format::eR8G8B8A8Unorm;    // Color format

        // 1. Allocate Position Target
        auto [posImg, posMem] = imgUtil->CreateImage(
            width, height, 1, vk::SampleCountFlagBits::e1, hdrFormat,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        positionImage = std::move(posImg); positionMemory = std::move(posMem);
        positionImageView = imgUtil->CreateImageView(*positionImage, hdrFormat, vk::ImageAspectFlagBits::eColor, 1);

        // 2. Allocate Normal Target
        auto [normImg, normMem] = imgUtil->CreateImage(
            width, height, 1, vk::SampleCountFlagBits::e1, hdrFormat,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        normalImage = std::move(normImg); normalMemory = std::move(normMem);
        normalImageView = imgUtil->CreateImageView(*normalImage, hdrFormat, vk::ImageAspectFlagBits::eColor, 1);

        // 3. Allocate Albedo Target
        auto [albedoImg, albedoMem] = imgUtil->CreateImage(
            width, height, 1, vk::SampleCountFlagBits::e1, srgbFormat,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        albedoImage = std::move(albedoImg); albedoMemory = std::move(albedoMem);
        albedoImageView = imgUtil->CreateImageView(*albedoImage, srgbFormat, vk::ImageAspectFlagBits::eColor, 1);
    }

    void RenderTarget::CreateDepthResources()
    {
        // Implementation to create depth image, memory, and view
        // With dynamic rendering, we just need to create the image and image view
        // that will be used with vkCmdBeginRendering
        // ...
        auto& renderer = VulkanRenderer::Get();
        auto* imgUtil = renderer.GetVulkanImageSubsystem();
        vk::Format format = imgUtil->FindDepthFormat();

        auto [img, mem] = imgUtil->CreateImage(
            width, height, 1, vk::SampleCountFlagBits::e1, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        depthImage = std::move(img);
        depthMemory = std::move(mem);

        depthImageView = imgUtil->CreateImageView(*depthImage, format, vk::ImageAspectFlagBits::eDepth, 1);
    }

    void RenderPass::AddDependency(const std::string& dependency)
    {
        dependencies.push_back(dependency);
    }

    void RenderPass::Execute(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!enabled) return;

        BeginPass(commandBuffer, imageIndex);
        Render(commandBuffer, imageIndex);
        EndPass(commandBuffer);
    }

    RenderPass* RenderPassManager::GetRenderPass(const std::string& name)
    {
        auto it = renderPasses.find(name);
        if (it != renderPasses.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RenderPassManager::RemoveRenderPass(const std::string& name)
    {
        auto it = renderPasses.find(name);
        if (it != renderPasses.end()) {
            renderPasses.erase(it);
            dirty = true;
        }
    }

    void RenderPassManager::Execute(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (dirty) {
            SortPasses();
            dirty = false;
        }

        for (auto pass : sortedPasses) {
            pass->Execute(commandBuffer, imageIndex);
        }
    }

    void RenderPassManager::SortPasses()
    {
        // Topologically sort render passes based on dependencies
        sortedPasses.clear();

        // Create a copy of render passes for sorting
        std::unordered_map<std::string, RenderPass*> passMap;
        for (const auto& [name, pass] : renderPasses) {
            passMap[name] = pass.get();
        }

        // Perform topological sort
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> visiting;

        for (const auto& [name, pass] : passMap) {
            if (visited.find(name) == visited.end()) {
                TopologicalSort(name, passMap, visited, visiting);
            }
        }
    }

    void RenderPassManager::TopologicalSort(const std::string& name, const std::unordered_map<std::string, RenderPass*>& passMap,
        std::unordered_set<std::string>& visited, std::unordered_set<std::string>& visiting)
    {
        visiting.insert(name);

        auto pass = passMap.at(name);
        for (const auto& dep : pass->GetDependencies()) {
            if (visited.find(dep) == visited.end()) {
                if (visiting.find(dep) != visiting.end()) {
                    // Circular dependency detected
                    throw std::runtime_error("Circular dependency detected in render passes");
                }
                TopologicalSort(dep, passMap, visited, visiting);
            }
        }

        visiting.erase(name);
        visited.insert(name);
        sortedPasses.push_back(pass);
    }

    PostProcessPass::PostProcessPass(const std::string& name, LightingPass* lPass)
        : RenderPass(name), lightingPass(lPass)
    {
        // Add dependency on lighting pass
        AddDependency(lPass->GetName());
    }

    void PostProcessPass::AddEffect(PostProcessEffect* effect)
    {
        effects.push_back(effect);
    }

    void PostProcessPass::RemoveEffect(PostProcessEffect* effect)
    {
        auto it = std::find(effects.begin(), effects.end(), effect);
        if (it != effects.end()) {
            effects.erase(it);
        }
    }

    void PostProcessPass::BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // Begin rendering with dynamic rendering
        vk::RenderingInfoKHR renderingInfo;

        // Set up color attachment for the post-processing pass
        vk::RenderingAttachmentInfoKHR colorAttachment;
        colorAttachment.setImageView(GetRenderTarget()->GetAlbedoImageView())
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}));

        // Configure rendering info
        renderingInfo.setRenderArea(vk::Rect2D({ 0, 0 }, { GetRenderTarget()->GetWidth(), GetRenderTarget()->GetHeight() }))
            .setLayerCount(1)
            .setColorAttachmentCount(1)
            .setPColorAttachments(&colorAttachment);

        // Begin dynamic rendering
        commandBuffer.beginRendering(renderingInfo);
    }

    void PostProcessPass::Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // With dynamic rendering, each effect can set up its own rendering state
        // and access input textures directly as shader resources

        // Apply each post-process effect
        for (auto effect : effects) {
            effect->Apply(commandBuffer);
        }
    }

    void PostProcessPass::EndPass(vk::raii::CommandBuffer& commandBuffer)
    {
        // End dynamic rendering
        commandBuffer.endRendering();
    }

}