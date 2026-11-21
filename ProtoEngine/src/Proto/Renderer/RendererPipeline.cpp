#include "protopch.hpp"
#include "RendererPipeline.hpp"

#include "Core/ServiceLocator.hpp"
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"

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

    bool SceneRenderer::CreateFogPipeline()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/VolumetricFog.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {
                vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment }
            };
            m_FogSetLayout = device.createDescriptorSetLayout({ .bindingCount = 3, .pBindings = bindings.data() });

            std::array<vk::DescriptorSetLayout, 2> setLayouts = {
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout,
                *m_FogSetLayout
            };

            vk::PushConstantRange pushRange{
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(FogPushConstants)
            };

            m_FogPipelineLayout = device.createPipelineLayout({
                .setLayoutCount = 2,
                .pSetLayouts = setLayouts.data(),
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushRange
                });

            std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
                vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "VSMain" },
                vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = "PSMain" }
            };
            vk::PipelineVertexInputStateCreateInfo vertexInput;
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
            vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
            std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = 2, .pDynamicStates = dynStates.data() };
            vk::PipelineRasterizationStateCreateInfo rasterizer{ .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1.0f };
            vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
            vk::PipelineDepthStencilStateCreateInfo depthStencil{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };
            vk::PipelineColorBlendAttachmentState blendAttach{
                .blendEnable = vk::False,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 1, .pAttachments = &blendAttach };
            vk::Format format = vk::Format::eR16G16B16A16Sfloat;
            vk::PipelineRenderingCreateInfo renderingInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &format };

            m_FogPipeline = device.createGraphicsPipeline(nullptr, vk::GraphicsPipelineCreateInfo{
                .pNext = &renderingInfo,
                .stageCount = 2,
                .pStages = stages.data(),
                .pVertexInputState = &vertexInput,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *m_FogPipelineLayout
                });
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[FOG ERROR]: " << e.what() << std::endl;
            return false;
        }
    }

    SceneRenderer::SceneRenderer(vk::raii::Device& dev, vk::Queue queue) : device(dev), graphicsQueue(queue)
    {
        // Create command pool
        // ...
        // Create synchronization objects
        // ...
        // Set up render passes
        SetupRenderPasses();
        CreatePBRPipeline();
        CreateTransparentPipeline();
        CreateDeferredLightingPipeline();
        CreateShadowPipeline();
        CreateBloomPipelines();
        CreateSSAOPipelines();
        CreateParticlePipelines();
        CreateSSRPipeline();
        CreateFogPipeline();

        m_EnvCubemap = std::make_shared<Texture>("studio_env_cubemap");
        m_EnvCubemap->CreateProceduralStudioCubemap(64);

        m_BrdfLutTex = std::make_shared<Texture>("studio_brdf_lut");
        m_BrdfLutTex->CreateProceduralBRDFLUT(128);
    }

    void SceneRenderer::Render(const std::vector<Entity*>& entities, vk::raii::CommandBuffer& activeCmdBuffer, uint32_t imageIndex)
    {
        auto visibleEntities = entities;
        //if (visibleEntities.empty()) {
        //    visibleEntities = entities;
        //}

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
                                std::string targetTextureId = "default_white";

                                if (textureIdx >= 0 && textureIdx < static_cast<int>(rawModel->textureResourceIds.size())) {
                                    targetTextureId = rawModel->textureResourceIds[textureIdx];
                                }

                                auto& resManager = ServiceLocator::Get<ResourceManager>();
                                auto* tex = resManager.GetResource<Texture>(targetTextureId);

                                if (!tex || !tex->IsLoaded()) {
                                    tex = resManager.GetResource<Texture>("default_white");
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

        // Bind our verified graphics state context layout definitions
        if (*pbrPipeline) {
            activeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrPipeline);
        }

        auto vpSize = renderer.imGui->GetViewportSize();
        float vpWidth = static_cast<float>(vpSize.x);
        float vpHeight = static_cast<float>(vpSize.y);
        activeCmdBuffer.setViewport(0, { vk::Viewport(0.0f, 0.0f, vpWidth, vpHeight, 0.0f, 1.0f) });
        vk::Extent2D vpExtent = { static_cast<uint32_t>(vpWidth), static_cast<uint32_t>(vpHeight) };
        activeCmdBuffer.setScissor(0, { vk::Rect2D(vk::Offset2D(0, 0), vpExtent) });

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
                            RenderModelNode(rootNode, entityWorldMatrix, activeCmdBuffer, rawModel, activeCameraComponent, false, entity);
                        }
                    }
                }
            }
        }
    }

    bool SceneRenderer::CreatePBRPipeline() {
        try {

            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/pbr.spv");
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

            // Use your structure's type-safe binding description layout
            vk::VertexInputBindingDescription bindingDescription = Proto::Vertex::getBindingDescription();

            // Match your structure's actual 4-attribute layout. This removes the non-existent 5th attribute (Tangent) that corrupted memory alignment!
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

            // Configure 5 matching attachment blend states!
            std::array<vk::PipelineColorBlendAttachmentState, 5> blendAttachments;
            for (size_t i = 0; i < 5; ++i) {
                blendAttachments[i].setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
                    .setBlendEnable(false);
            }

            vk::PipelineColorBlendStateCreateInfo colorBlending;
            colorBlending.setLogicOpEnable(false)
                .setAttachmentCount(static_cast<uint32_t>(blendAttachments.size()))
                .setPAttachments(blendAttachments.data());

            vk::PushConstantRange pushConstantRange;
            pushConstantRange.setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment).setOffset(0).setSize(sizeof(PushConstantBlock));

            // AUTONOMOUSLY INITIALIZE DESCRIPTOR SET 0 LAYOUT (GLOBAL UBO)
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

            // AUTONOMOUSLY INITIALIZE DESCRIPTOR SET 1 LAYOUT (MATERIAL TEXTURES)
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

            // PACK BOTH OPERATIONAL RAII HANDLES INTO THE LAYOUT MANAGER
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

            // Map the 5 format types to match your BeginPass colorAttachments array!
            std::array<vk::Format, 5> gBufferFormats = {
                vk::Format::eR16G16B16A16Sfloat, // Target 0: Position + Metallic
                vk::Format::eR16G16B16A16Sfloat, // Target 1: Normal + Roughness
                vk::Format::eR8G8B8A8Unorm,      // Target 2: Albedo
                vk::Format::eR16G16B16A16Sfloat,  // Target 3: Emissive (HDR linear)
                vk::Format::eR8G8B8A8Unorm       // Target 4: SurfaceExt (Clearcoat + Iridescence)
            };

            vk::PipelineRenderingCreateInfo renderingInfo;
            renderingInfo.setColorAttachmentCount(static_cast<uint32_t>(gBufferFormats.size()))
                .setPColorAttachmentFormats(gBufferFormats.data())
                .setDepthAttachmentFormat(findDepthFormat());
            pipelineInfo.setPNext(&renderingInfo);

            pbrPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);

            // CRITICAL DEBUG TRAP:
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

        if (camera != nullptr)
        {
            ubo.view = camera->GetViewMatrix();
            ubo.camPos = glm::vec4(camera->GetPosition(), 1.0f);
        }
        else {
            // Fallback: If no ECS component is active yet, fetch view matrices from the raw fly camera
            if (camera) {
                ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                ubo.camPos = glm::vec4(camera->GetPosition(), 1.0f);
            }
        }

        // Build a precise, clean perspective matrix
        glm::vec2 vpSize = VulkanRenderer::Get().imGui
            ? VulkanRenderer::Get().imGui->GetViewportSize()
            : glm::vec2(1280.0f, 720.0f);

        float aspect = (vpSize.x > 0.0f && vpSize.y > 0.0f) ? (vpSize.x / vpSize.y) : static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);

        float fovDegrees = camera ? camera->GetFOV() : 45.0f; // Adjust member/getter name to match ComponentTypes.hpp
        float nearClip = camera ? camera->GetNearClip() : 0.1f;
        float farClip = camera ? camera->GetFarClip() : 100.0f;

        if (camera) {
            camera->SetAspectRatio(aspect);
            ubo.proj = camera->GetProjectionMatrix();
        }
        else {
            glm::mat4 fallbackProj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
            ubo.proj = fallbackProj;
        }

        // Set up lights with physically balanced intensities
        for (int i = 0; i < 4; ++i) {
            ubo.lightPositions[i] = glm::vec4(0.0f);
            ubo.lightColors[i] = glm::vec4(0.0f);
        }

        int lightIdx = 0;
        for (Entity* sceneEntity : Entity::GetActiveSceneEntities()) {
            if (!sceneEntity || !sceneEntity->IsActive()) continue;
            auto* lightComp = sceneEntity->GetComponent<LightComponent>();
            auto* lightTrans = sceneEntity->GetComponent<TransformComponent>();
            if (!lightComp || !lightTrans) continue;

            glm::vec3 dir = glm::normalize(lightTrans->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 pos = glm::vec3(lightTrans->GetWorldTransformMatrix()[3]);

            if (lightComp->Type == LightType::Directional) {
                ubo.lightPositions[lightIdx] = glm::vec4(dir, 0.0f);
            }
            else if (lightComp->Type == LightType::Point) {
                ubo.lightPositions[lightIdx] = glm::vec4(pos, 1.0f);
            }
            else { // Spot
                ubo.lightPositions[lightIdx] = glm::vec4(pos, 2.0f);
                ubo.lightDirections[lightIdx] = glm::vec4(dir, 0.0f);
                ubo.lightConeParams[lightIdx] = glm::vec4(
                    std::cos(glm::radians(lightComp->InnerConeAngle)),
                    std::cos(glm::radians(lightComp->OuterConeAngle)),
                    0.0f, 0.0f
                );
            }

            ubo.lightColors[lightIdx] = glm::vec4(lightComp->Color * lightComp->Intensity, lightComp->Radius);

            lightIdx++;
            if (lightIdx >= 4) break;
        }

        ComputeShadowAtlasMatrices(ubo.lightSpaceMatrices, ubo.lightShadowEnabled);

        // Set camera position for view-dependent effects
        ubo.camPos = glm::vec4(camera ? camera->GetPosition() : glm::vec3(2.0f, 2.0f, 2.0f), 1.0f);

        // Set PBR parameters
        ubo.exposure = 1.0f;
        ubo.gamma = 2.2f;
        ubo.prefilteredCubeMipLevels = 1.0f;
        ubo.scaleIBLAmbient = 1.0f;
        ubo.prefilteredCubeMipLevels = m_EnvCubemap ? static_cast<float>(m_EnvCubemap->mipLevels) : 1.0f;
        ubo.scaleIBLAmbient = m_IBLIntensity;

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
    void SceneRenderer::pushMaterialProperties(vk::CommandBuffer commandBuffer, const Model* model,
        uint32_t materialIndex, const glm::mat4& globalMatrix, Entity* ownerEntity) {
        const Material& material = model->materials[materialIndex];

        glm::vec4 baseColor = material.baseColorFactor;
        glm::vec3 emissive = material.emissiveFactor;
        float     metallic = material.metallicFactor;
        float     roughness = material.roughnessFactor;
        float     clearcoat = material.clearcoatFactor;
        float     clearcoatRough = material.clearcoatRoughness;
        float     iridescence = material.iridescenceFactor;

        if (ownerEntity) {
            if (auto* ovComp = ownerEntity->GetComponent<MaterialOverrideComponent>()) {
                auto it = ovComp->Overrides.find(materialIndex);
                if (it != ovComp->Overrides.end() && it->second.Enabled) {
                    const auto& ov = it->second;
                    baseColor = ov.BaseColorFactor;
                    emissive = ov.EmissiveFactor;
                    metallic = ov.MetallicFactor;
                    roughness = ov.RoughnessFactor;
                    clearcoat = ov.ClearcoatFactor;
                    clearcoatRough = ov.ClearcoatRoughness;
                    iridescence = ov.IridescenceFactor;
                }
            }
        }

        PushConstantBlock pushConstants{};
        pushConstants.modelMatrix = globalMatrix;
        pushConstants.baseColorFactor = baseColor;
        pushConstants.emissiveFactor = glm::vec4(emissive, m_EmissiveStrength);
        pushConstants.coatAndIridescence = glm::vec4(clearcoat, clearcoatRough, iridescence, material.iridescenceIor);
        pushConstants.metallicRoughnessFactors = glm::vec4(metallic, roughness, 0.0f, 0.0f);

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
            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/DeferredLighting.spv");
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

            // Empty Vertex Input State Layout! We bind no CPU structures because the shader builds the fullscreen canvas procedurally.
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

            // DISABLE DEPTH WRITING: Lighting pass uses 2D screen sampling
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

            // Compile a dedicated 3-binding descriptor set layout specifically for your 10 G-Buffer inputs!
            std::array<vk::DescriptorSetLayoutBinding, 10> gBufferBindings;
            for (uint32_t b = 0; b < 10; ++b) {
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

            vk::PushConstantRange shadowPushRange{
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(DeferredShadowPushConstants)
            };

            // Pack layouts for your deferred lighting pipeline
            vk::DescriptorSetLayout deferredPipelineSetLayouts[] = {
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout, // Set 0: Global Engine UBOs
                *m_DeferredSetLayout                                       // Set 1: 3 G-Buffer attachments
            };

            vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ 
                .setLayoutCount = 2,
                .pSetLayouts = deferredPipelineSetLayouts,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &shadowPushRange
            };
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
            vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat;
            renderingInfo.setColorAttachmentCount(1).setPColorAttachmentFormats(&colorFormat);
            pipelineInfo.setPNext(&renderingInfo);

            m_DeferredPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[DEFERRED ERROR]: Pipeline initialization skipped: " << e.what() << std::endl;
            return false;
        }
    }

    bool SceneRenderer::CreateShadowPipeline()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/Shadow.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            vk::PipelineShaderStageCreateInfo vertStage{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = *shaderModule,
                .pName = "VSMain"
            };

            // Match Proto::Vertex stride (location 0 = pos)
            vk::VertexInputBindingDescription bindingDesc = Proto::Vertex::getBindingDescription();
            vk::VertexInputAttributeDescription posAttr(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, pos));

            vk::PipelineVertexInputStateCreateInfo vertexInput{
                .vertexBindingDescriptionCount = 1,
                .pVertexBindingDescriptions = &bindingDesc,
                .vertexAttributeDescriptionCount = 1,
                .pVertexAttributeDescriptions = &posAttr
            };

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
                .topology = vk::PrimitiveTopology::eTriangleList
            };

            vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
            std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState{
                .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
                .pDynamicStates = dynamicStates.data()
            };

            // Front-face culling + depth bias eliminates shadow acne and peter-panning
            vk::PipelineRasterizationStateCreateInfo rasterizer{
                .polygonMode = vk::PolygonMode::eFill,
                .cullMode = vk::CullModeFlagBits::eFront,
                .frontFace = vk::FrontFace::eCounterClockwise,
                .depthBiasEnable = vk::True,
                .depthBiasConstantFactor = 1.25f,
                .depthBiasSlopeFactor = 1.75f,
                .lineWidth = 1.0f
            };

            vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };

            vk::PipelineDepthStencilStateCreateInfo depthStencil{
                .depthTestEnable = vk::True,
                .depthWriteEnable = vk::True,
                .depthCompareOp = vk::CompareOp::eLessOrEqual
            };

            vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 0 };

            vk::PushConstantRange pushRange{
                .stageFlags = vk::ShaderStageFlagBits::eVertex,
                .offset = 0,
                .size = sizeof(ShadowMapPushConstants)
            };

            vk::PipelineLayoutCreateInfo layoutInfo{
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushRange
            };
            m_ShadowPipelineLayout = device.createPipelineLayout(layoutInfo);

            vk::Format depthFormat = vk::Format::eD32Sfloat;
            vk::PipelineRenderingCreateInfo renderingInfo{
                .colorAttachmentCount = 0,
                .depthAttachmentFormat = depthFormat
            };

            vk::GraphicsPipelineCreateInfo pipelineInfo{
                .pNext = &renderingInfo,
                .stageCount = 1,
                .pStages = &vertStage,
                .pVertexInputState = &vertexInput,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *m_ShadowPipelineLayout
            };

            m_ShadowPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[SHADOW ERROR]: Failed to create shadow pipeline: " << e.what() << std::endl;
            return false;
        }
    }

    void SceneRenderer::ComputeShadowAtlasMatrices(glm::mat4 outMatrices[4], glm::ivec4& outShadowEnabled) const
    {
        outShadowEnabled = glm::ivec4(0);
        int lightIdx = 0;

        for (Entity* entity : Entity::GetActiveSceneEntities()) {
            if (!entity || !entity->IsActive()) continue;
            auto* lightComp = entity->GetComponent<LightComponent>();
            auto* lightTrans = entity->GetComponent<TransformComponent>();
            if (!lightComp || !lightTrans) continue;

            if (lightComp->CastShadows) {
                glm::vec3 lightPos = glm::vec3(lightTrans->GetWorldTransformMatrix()[3]);
                glm::vec3 lightDir = glm::normalize(lightTrans->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
                glm::vec3 upVec = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                    ? glm::vec3(0.0f, 0.0f, 1.0f)
                    : glm::vec3(0.0f, 1.0f, 0.0f);

                glm::mat4 lightView(1.0f);
                glm::mat4 lightProj(1.0f);

                if (lightComp->Type == LightType::Directional) {
                    glm::vec3 sceneCenter(0.0f, 0.0f, -2.0f);
                    lightView = glm::lookAt(sceneCenter - lightDir * 25.0f, sceneCenter, upVec);
                    lightProj = glm::orthoRH_ZO(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 60.0f);
                }
                else if (lightComp->Type == LightType::Spot) {
                    lightView = glm::lookAt(lightPos, lightPos + lightDir, upVec);
                    float fov = glm::clamp(lightComp->OuterConeAngle * 2.0f, 10.0f, 170.0f);
                    lightProj = glm::perspectiveRH_ZO(glm::radians(fov), 1.0f, 0.1f, std::max(lightComp->Radius, 1.0f));
                }
                else { // Point light wide-angle projection
                    lightView = glm::lookAt(lightPos, lightPos + lightDir, upVec);
                    lightProj = glm::perspectiveRH_ZO(glm::radians(140.0f), 1.0f, 0.1f, std::max(lightComp->Radius, 1.0f));
                }

                lightProj[1][1] *= -1.0f; // Flip Y axis only for Vulkan NDC!
                outMatrices[lightIdx] = lightProj * lightView;
                outShadowEnabled[lightIdx] = 1;
            }

            lightIdx++;
            if (lightIdx >= 4) break;
        }
    }

    void SceneRenderer::RenderShadowPass(vk::raii::CommandBuffer& cmd, const glm::mat4 lightSpaceMatrices[4], const glm::ivec4& shadowEnabled)
    {
        if (!*m_ShadowPipeline) return;
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_ShadowPipeline);

        for (int i = 0; i < 4; ++i) {
            if (shadowEnabled[i] == 0) continue;

            float tileX = static_cast<float>((i % 2) * 2048);
            float tileY = static_cast<float>((i / 2) * 2048);
            cmd.setViewport(0, { vk::Viewport(tileX, tileY, 2048.0f, 2048.0f, 0.0f, 1.0f) });
            cmd.setScissor(0, { vk::Rect2D({ static_cast<int32_t>(tileX), static_cast<int32_t>(tileY) },{ 2048, 2048 }) });

            std::function<void(Node*, const glm::mat4&)> drawShadowNode = [&](Node* node, const glm::mat4& parentMat) {
                if (!node) return;
                glm::mat4 globalMat = parentMat * node->getLocalMatrix();

                if (node->mesh && node->mesh->IsLoaded()) {
                    ShadowMapPushConstants pc{ lightSpaceMatrices[i], globalMat };
                    cmd.pushConstants(*m_ShadowPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(ShadowMapPushConstants), &pc);
                    cmd.bindVertexBuffers(0, { *node->mesh->GetVertexBuffer() }, { 0 });
                    cmd.bindIndexBuffer(*node->mesh->GetIndexBuffer(), 0, vk::IndexType::eUint32);
                    cmd.drawIndexed(node->mesh->GetIndexCount(), 1, 0, 0, 0);
                }
                for (auto* child : node->children) {
                    drawShadowNode(child, globalMat);
                }
            };

            for (Entity* entity : Entity::GetActiveSceneEntities()) {
                if (!entity || !entity->IsActive()) continue;
                auto* modelComp = entity->GetComponent<ModelComponent>();
                if (!modelComp || !modelComp->GetModelHandle().IsValid()) continue;
                Model* rawModel = modelComp->GetModelHandle().Get();
                if (!rawModel || !rawModel->IsLoaded()) continue;

                auto* transform = entity->GetComponent<TransformComponent>();
                glm::mat4 worldMat = transform ? transform->GetWorldTransformMatrix() : glm::mat4(1.0f);
                for (auto* rootNode : rawModel->nodes) {
                    drawShadowNode(rootNode, worldMat);
                }
            }
        }
    }

    glm::mat4 SceneRenderer::ComputeLightSpaceMatrix(bool& outShadowEnabled) const
    {
        outShadowEnabled = false;
        for (Entity* entity : Entity::GetActiveSceneEntities()) {
            if (!entity || !entity->IsActive()) continue;
            auto* lightComp = entity->GetComponent<LightComponent>();
            auto* lightTrans = entity->GetComponent<TransformComponent>();
            if (!lightComp || !lightTrans) continue;

            if (lightComp->Type != LightType::Directional) continue;

            outShadowEnabled = lightComp->CastShadows;
            if (!outShadowEnabled) return glm::mat4(1.0f);

            glm::vec3 lightPos = glm::vec3(lightTrans->GetWorldTransformMatrix()[3]);
            glm::vec3 lightDir = glm::normalize(lightTrans->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));

            // Avoid degenerate up-vector if light points straight down
            glm::vec3 upVec = (std::abs(glm::dot(lightDir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);

            glm::mat4 lightView = glm::lookAt(lightPos, lightPos + lightDir, upVec);
            glm::mat4 lightProj = glm::orthoRH_ZO(-15.0f, 15.0f, -15.0f, 15.0f, 0.1f, 50.0f);
            lightProj[1][1] *= -1.0f; // Match Vulkan Y-flipped NDC space

            return lightProj * lightView;
        }
        return glm::mat4(1.0f);
    }

    bool SceneRenderer::CreateTransparentPipeline()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            // Uses a forward PBR/unlit glass shader that outputs directly to FinalColor (1 color attachment)
            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/pbr_forward.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            vk::PipelineShaderStageCreateInfo vertStage{
                .stage = vk::ShaderStageFlagBits::eVertex, .module = *shaderModule, .pName = "VSMain"
            };
            vk::PipelineShaderStageCreateInfo fragStage{
                .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = "PSMain"
            };
            std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = { vertStage, fragStage };

            vk::VertexInputBindingDescription bindingDescription = Proto::Vertex::getBindingDescription();
            std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions = {
                vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, pos)),
                vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, normal)),
                vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, color)),
                vk::VertexInputAttributeDescription(3, 0, vk::Format::eR32G32B32Sfloat, offsetof(Proto::Vertex, texCoord))
            };

            vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
            vertexInputInfo.setVertexBindingDescriptionCount(1)
                .setPVertexBindingDescriptions(&bindingDescription)
                .setVertexAttributeDescriptionCount(static_cast<uint32_t>(attributeDescriptions.size()))
                .setPVertexAttributeDescriptions(attributeDescriptions.data());

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
            inputAssembly.setTopology(vk::PrimitiveTopology::eTriangleList);

            vk::PipelineViewportStateCreateInfo viewportState;
            viewportState.setViewportCount(1).setScissorCount(1);

            std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState;
            dynamicState.setDynamicStateCount(static_cast<uint32_t>(dynamicStates.size())).setPDynamicStates(dynamicStates.data());

            // Double-sided rendering for glass and thin interior panels
            vk::PipelineRasterizationStateCreateInfo rasterizer;
            rasterizer.setPolygonMode(vk::PolygonMode::eFill)
                .setLineWidth(1.0f)
                .setCullMode(vk::CullModeFlagBits::eNone)
                .setFrontFace(vk::FrontFace::eCounterClockwise);

            vk::PipelineMultisampleStateCreateInfo multisampling;
            multisampling.setRasterizationSamples(vk::SampleCountFlagBits::e1);

            // Read depth from GeometryPass so opaque car parts occlude glass, but disable depth writes
            vk::PipelineDepthStencilStateCreateInfo depthStencil;
            depthStencil.setDepthTestEnable(true)
                .setDepthWriteEnable(false)
                .setDepthCompareOp(vk::CompareOp::eLess);

            // Standard SrcAlpha / OneMinusSrcAlpha blending
            vk::PipelineColorBlendAttachmentState colorBlendAttachment;
            colorBlendAttachment.setColorWriteMask(
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
                .setBlendEnable(true)
                .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                .setColorBlendOp(vk::BlendOp::eAdd)
                .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
                .setAlphaBlendOp(vk::BlendOp::eAdd);

            vk::PipelineColorBlendStateCreateInfo colorBlending;
            colorBlending.setLogicOpEnable(false)
                .setAttachmentCount(1)
                .setPAttachments(&colorBlendAttachment);

            vk::PipelineRenderingCreateInfo renderingInfo;
            vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat;
            renderingInfo.setColorAttachmentCount(1)                
                .setPColorAttachmentFormats(&colorFormat) //.setPColorAttachmentFormats(&swapChainImageFormat)
                .setDepthAttachmentFormat(findDepthFormat());

            vk::GraphicsPipelineCreateInfo pipelineInfo;
            pipelineInfo.setPNext(&renderingInfo)
                .setStageCount(static_cast<uint32_t>(shaderStages.size()))
                .setPStages(shaderStages.data())
                .setPVertexInputState(&vertexInputInfo)
                .setPInputAssemblyState(&inputAssembly)
                .setPViewportState(&viewportState)
                .setPRasterizationState(&rasterizer)
                .setPMultisampleState(&multisampling)
                .setPDepthStencilState(&depthStencil)
                .setPColorBlendState(&colorBlending)
                .setPDynamicState(&dynamicState)
                .setLayout(*pbrPipelineLayout); // Reuses the exact same UBO + Material Texture layout!

            m_TransparentPipeline = device.createGraphicsPipeline(nullptr, pipelineInfo);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[TRANSPARENT PIPELINE ERROR]: " << e.what() << std::endl;
            return false;
        }
    }

    void SceneRenderer::RenderTransparentPass(const std::vector<Entity*>& entities, vk::raii::CommandBuffer& activeCmdBuffer)
    {
        if (!*m_TransparentPipeline) return;

        auto& renderer = VulkanRenderer::Get();
        uint32_t currentFrame = renderer.vulkanSwapChain->GetFrameIndex();

        activeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_TransparentPipeline);

        if (!renderer.vulkanCommandBuffer->descriptorSets.empty()) {
            activeCmdBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                *pbrPipelineLayout,
                0,
                { *renderer.vulkanCommandBuffer->descriptorSets[currentFrame] },
                {}
            );
        }

        for (auto entity : entities) {
            if (!entity || !entity->IsActive()) continue;
            auto* modelComp = entity->GetComponent<ModelComponent>();
            if (!modelComp || !modelComp->GetModelHandle().IsValid()) continue;

            Model* rawModel = modelComp->GetModelHandle().Get();
            if (!rawModel || !rawModel->IsLoaded()) continue;

            auto* transform = entity->GetComponent<TransformComponent>();
            glm::mat4 entityWorldMatrix = transform ? transform->GetWorldTransformMatrix() : glm::mat4(1.0f);

            for (auto* rootNode : rawModel->nodes) {
                RenderModelNode(rootNode, entityWorldMatrix, activeCmdBuffer, rawModel, nullptr, true, entity);
            }
        }
    }

    bool SceneRenderer::CreateBloomPipelines()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/Bloom.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
                vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment }
            };
            m_BloomSetLayout = device.createDescriptorSetLayout({ .bindingCount = 2, .pBindings = bindings.data() });

            vk::PushConstantRange pushRange{
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(BloomPushConstants)
            };
            m_BloomPipelineLayout = device.createPipelineLayout({
                .setLayoutCount = 1,
                .pSetLayouts = &*m_BloomSetLayout,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushRange
                });

            auto buildFullscreenPipeline = [&](const char* fragEntry, vk::Format targetFormat) -> vk::raii::Pipeline {
                std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
                    vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = *shaderModule, .pName = "VSMain" },
                    vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = fragEntry }
                };

                vk::PipelineVertexInputStateCreateInfo vertexInput;
                vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
                vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
                std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
                vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = 2, .pDynamicStates = dynStates.data() };
                vk::PipelineRasterizationStateCreateInfo rasterizer{ .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1.0f };
                vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
                vk::PipelineDepthStencilStateCreateInfo depthStencil{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };
                vk::PipelineColorBlendAttachmentState blendAttach{
                    .blendEnable = vk::False,
                    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                };
                vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 1, .pAttachments = &blendAttach };
                vk::PipelineRenderingCreateInfo renderingInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &targetFormat };

                return device.createGraphicsPipeline(nullptr, vk::GraphicsPipelineCreateInfo{
                    .pNext = &renderingInfo,
                    .stageCount = 2,
                    .pStages = stages.data(),
                    .pVertexInputState = &vertexInput,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState = &multisampling,
                    .pDepthStencilState = &depthStencil,
                    .pColorBlendState = &colorBlending,
                    .pDynamicState = &dynamicState,
                    .layout = *m_BloomPipelineLayout
                    });
                };

            m_BloomExtractPipeline = buildFullscreenPipeline("PSBrightExtract", vk::Format::eR16G16B16A16Sfloat);
            m_BloomBlurPipeline = buildFullscreenPipeline("PSBlur", vk::Format::eR16G16B16A16Sfloat);
            m_BloomCompositePipeline = buildFullscreenPipeline("PSComposite", swapChainImageFormat);
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[BLOOM ERROR]: " << e.what() << std::endl;
            return false;
        }
    }

    bool SceneRenderer::CreateSSAOPipelines()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/SSAO.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
                vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment }
            };
            m_SSAOSetLayout = device.createDescriptorSetLayout({ .bindingCount = 2, .pBindings = bindings.data() });

            std::array<vk::DescriptorSetLayout, 2> setLayouts = {
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout, // Set 0: Global UBO
                *m_SSAOSetLayout                                            // Set 1: Position + Normal
            };
            m_SSAOPipelineLayout = device.createPipelineLayout({
                .setLayoutCount = 2,
                .pSetLayouts = setLayouts.data()
                });

            auto buildPass = [&](const char* fragEntry) -> vk::raii::Pipeline {
                std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
                    vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "VSMain" },
                    vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = fragEntry }
                };
                vk::PipelineVertexInputStateCreateInfo vertexInput;
                vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
                vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
                std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
                vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = 2, .pDynamicStates = dynStates.data() };
                vk::PipelineRasterizationStateCreateInfo rasterizer{ .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1.0f };
                vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
                vk::PipelineDepthStencilStateCreateInfo depthStencil{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };
                vk::PipelineColorBlendAttachmentState blendAttach{
                    .blendEnable = vk::False,
                    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                };
                vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 1, .pAttachments = &blendAttach };
                vk::Format format = vk::Format::eR8Unorm;
                vk::PipelineRenderingCreateInfo renderingInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &format };

                return device.createGraphicsPipeline(nullptr, vk::GraphicsPipelineCreateInfo{
                    .pNext = &renderingInfo,
                    .stageCount = 2,
                    .pStages = stages.data(),
                    .pVertexInputState = &vertexInput,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState = &multisampling,
                    .pDepthStencilState = &depthStencil,
                    .pColorBlendState = &colorBlending,
                    .pDynamicState = &dynamicState,
                    .layout = *m_SSAOPipelineLayout
                    });
                };

            m_SSAOPipeline = buildPass("PSSSAO");
            m_SSAOBlurPipeline = buildPass("PSBlur");
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[SSAO ERROR]: " << e.what() << std::endl;
            return false;
        }
    }

    bool SceneRenderer::CreateParticlePipelines()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            // 1. Allocate Particle SSBO in fast eDeviceLocal VRAM and zero-initialized Particle SSBO
            vk::DeviceSize bufferSize = MAX_PARTICLES * sizeof(ParticleData);
            std::tie(m_ParticleBuffer, m_ParticleMemory) = lowLevelRenderer.vulkanCommandBuffer->CreateBuffer(
                bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );

            // Zero-fill on the GPU and synchronize transfer write -> compute read/write before first dispatch
            vk::raii::CommandBuffer initCmd = lowLevelRenderer.vulkanCommandBuffer->BeginSingleTimeCommands();
            initCmd.fillBuffer(*m_ParticleBuffer, 0, bufferSize, 0);

            vk::BufferMemoryBarrier fillBarrier{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = *m_ParticleBuffer,
                .offset = 0,
                .size = bufferSize
            };
            initCmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, { fillBarrier }, {}
            );

            lowLevelRenderer.vulkanCommandBuffer->EndSingleTimeCommands(std::move(initCmd));

            // 2. Descriptor Set Layout + Set (Binding 0 = RWStructuredBuffer<Particle>) binding 0 (eCompute) and binding 1 (eVertex)
            vk::DescriptorPoolSize poolSize{ vk::DescriptorType::eStorageBuffer, 2 };
            m_ParticleDescriptorPool = device.createDescriptorPool({
                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                .maxSets = 1,
                .poolSizeCount = 1,
                .pPoolSizes = &poolSize
                });

            std::array<vk::DescriptorSetLayoutBinding, 2> ssboBindings = {
                vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute },
                vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex }
            };
            m_ParticleSetLayout = device.createDescriptorSetLayout({ .bindingCount = 2, .pBindings = ssboBindings.data() });

            vk::DescriptorSetLayout layoutHandle = *m_ParticleSetLayout;
            m_ParticleSet = std::move(device.allocateDescriptorSets({
                .descriptorPool = *m_ParticleDescriptorPool,
                .descriptorSetCount = 1,
                .pSetLayouts = &layoutHandle
                }).front());

            vk::DescriptorBufferInfo bufInfo{ *m_ParticleBuffer, 0, bufferSize };
            std::array<vk::WriteDescriptorSet, 2> writes = {
                vk::WriteDescriptorSet{ .dstSet = *m_ParticleSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &bufInfo },
                vk::WriteDescriptorSet{ .dstSet = *m_ParticleSet, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .pBufferInfo = &bufInfo }
            };
            device.updateDescriptorSets(writes, {});

            // 3. Pipeline Layout (shared by Compute & Billboard Graphics pipelines)
            vk::PushConstantRange pushRange{
                .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex,
                .offset = 0,
                .size = sizeof(ParticlePushConstants)
            };

            std::array<vk::DescriptorSetLayout, 2> particleLayouts = {
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout, // Set 0: Global UBO (view, proj)
                *m_ParticleSetLayout                                        // Set 1: particlesOut + particlesIn
            };
            m_ParticlePipelineLayout = device.createPipelineLayout({
                .setLayoutCount = 2,
                .pSetLayouts = particleLayouts.data(),
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushRange
            });

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/Particles.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            // 4. Compute Pipeline (CSMain)
            m_ParticleComputePipeline = device.createComputePipeline(nullptr, vk::ComputePipelineCreateInfo{
                .stage = { .stage = vk::ShaderStageFlagBits::eCompute, .module = *shaderModule, .pName = "CSMain" },
                .layout = *m_ParticlePipelineLayout
                });

            // 5. Additive Billboard Graphics Pipeline (VSMain + PSMain -> HDRColor)
            std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
                vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "VSMain" },
                vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = "PSMain" }
            };
            vk::PipelineVertexInputStateCreateInfo vertexInput;
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
            vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
            std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
            vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = 2, .pDynamicStates = dynStates.data() };
            vk::PipelineRasterizationStateCreateInfo rasterizer{ .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1.0f };
            vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
            vk::PipelineDepthStencilStateCreateInfo depthStencil{ .depthTestEnable = vk::True, .depthWriteEnable = vk::False, .depthCompareOp = vk::CompareOp::eLess };

            vk::PipelineColorBlendAttachmentState additiveBlend{
                .blendEnable = vk::True,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOne,
                .colorBlendOp = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eOne,
                .dstAlphaBlendFactor = vk::BlendFactor::eOne,
                .alphaBlendOp = vk::BlendOp::eAdd,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
            };
            vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 1, .pAttachments = &additiveBlend };
            vk::Format hdrFormat = vk::Format::eR16G16B16A16Sfloat;
            vk::PipelineRenderingCreateInfo renderingInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &hdrFormat, .depthAttachmentFormat = findDepthFormat() };

            m_ParticleRenderPipeline = device.createGraphicsPipeline(nullptr, vk::GraphicsPipelineCreateInfo{
                .pNext = &renderingInfo,
                .stageCount = 2,
                .pStages = stages.data(),
                .pVertexInputState = &vertexInput,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *m_ParticlePipelineLayout
                });

            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[PARTICLE PIPELINE ERROR]: " << e.what() << std::endl;
            return false;
        }
    }

    void SceneRenderer::DispatchAndRenderParticles(vk::raii::CommandBuffer& cmd, float deltaTime)
    {
        if (!*m_ParticleComputePipeline || !*m_ParticleRenderPipeline || !*m_ParticleSet) return;

        // 1. Find active camera & optional Car emitter position
        CameraComponent* activeCam = nullptr;

        Entity* emitterEntity = nullptr;
        ParticleEmitterComponent* emitterComp = nullptr;
        for (Entity* entity : Entity::GetActiveSceneEntities()) {
            if (!entity || !entity->IsActive()) continue;
            if (!activeCam) activeCam = entity->GetComponent<CameraComponent>();
            if (auto* pe = entity->GetComponent<ParticleEmitterComponent>(); pe && pe->Enabled) {
                emitterEntity = entity;
                emitterComp = pe;
            }
        }
        if (!activeCam || !emitterEntity || !emitterComp) {
            m_HasActiveParticles = false;
            return;
        }
        m_HasActiveParticles = true;

        glm::mat4 worldMat = glm::mat4(1.0f);
        glm::quat worldRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (auto* t = emitterEntity->GetComponent<TransformComponent>()) {
            worldMat = t->GetWorldTransformMatrix();
            worldRot = t->GetRotation();
        }

        glm::vec3 worldSpawn = glm::vec3(worldMat * glm::vec4(emitterComp->Offset, 1.0f));
        glm::vec3 worldDir = glm::normalize(worldRot * emitterComp->Direction);

        float dt = std::clamp(deltaTime, 0.001f, 0.05f);
        static float totalTime = 0.0f;
        totalTime += dt;

        glm::mat4 view = activeCam->GetViewMatrix();
        glm::mat4 proj = activeCam->GetProjectionMatrix();
        glm::mat4 invView = glm::inverse(view);

        ParticlePushConstants pc{};
        pc.emitterPos = glm::vec4(worldSpawn, dt);
        pc.emitterDir = glm::vec4(worldDir, emitterComp->Spread);
        pc.particleCol = glm::vec4(emitterComp->Color * emitterComp->HDRIntensity, emitterComp->Gravity);
        pc.lifeAndSize = glm::vec4(emitterComp->MinLife, emitterComp->MaxLife, emitterComp->InitialSpeed, emitterComp->ParticleSize);
        pc.camRight = glm::vec4(glm::vec3(invView[0]), totalTime);
        pc.camUp = glm::vec4(glm::vec3(invView[1]), static_cast<float>(emitterComp->ActiveParticles));

        // 2. Dispatch Compute Shader (4096 / 256 = 16 thread groups)
        auto& lowLevelRenderer = VulkanRenderer::Get();
        auto currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_ParticleComputePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_ParticlePipelineLayout, 0,
            { *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame], *m_ParticleSet }, {});
        m_LastParticlePC = pc;
        cmd.pushConstants(*m_ParticlePipelineLayout,
            vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex,
            0, sizeof(ParticlePushConstants), &pc);
        cmd.dispatch(MAX_PARTICLES / 256, 1, 1);

        // 3. Synchronize SSBO writes from Compute Shader -> Vertex Shader reads
        vk::BufferMemoryBarrier ssboBarrier{
            .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = *m_ParticleBuffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eVertexShader,
            {}, {}, { ssboBarrier }, {}
        );
    }

    void SceneRenderer::SetEnvironmentHDRI(const std::string& path)
    {
        device.waitIdle();
        auto newEnv = std::make_shared<Texture>("active_env_cubemap");
        if (path.empty() || !newEnv->LoadHDRI(path, 512)) {
            newEnv->CreateProceduralStudioCubemap(64);
            m_ActiveHDRIPath = "";
        }
        else {
            m_ActiveHDRIPath = path;
        }
        m_EnvCubemap = std::move(newEnv);
        m_DeferredGBufferSet = nullptr; // Force LightingPass to rebind the new cubemap on next frame
    }

    bool SceneRenderer::CreateSSRPipeline()
    {
        try {
            auto& lowLevelRenderer = VulkanRenderer::Get();
            auto& device = lowLevelRenderer.GetLogicalDevice();

            auto shaderCode = lowLevelRenderer.vulkanPipeline->readFile("../../../assets/shaders/SSR.spv");
            vk::raii::ShaderModule shaderModule = lowLevelRenderer.vulkanPipeline->createShaderModule(shaderCode);

            std::array<vk::DescriptorSetLayoutBinding, 4> bindings = {
                vk::DescriptorSetLayoutBinding{ 0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ 3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment }
            };
            m_SSRSetLayout = device.createDescriptorSetLayout({ .bindingCount = 4, .pBindings = bindings.data() });

            std::array<vk::DescriptorSetLayout, 2> setLayouts = {
                *lowLevelRenderer.vulkanCommandBuffer->descriptorSetLayout, // Set 0: Global UBO
                * m_SSRSetLayout                                            // Set 1: Position + Normal
            };


            vk::PushConstantRange pushRange{
                .stageFlags = vk::ShaderStageFlagBits::eFragment,
                .offset = 0,
                .size = sizeof(SSRPushConstants)
            };

            m_SSRPipelineLayout = device.createPipelineLayout({
                .setLayoutCount = 2,
                .pSetLayouts = setLayouts.data(),
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &pushRange
            });

            auto buildPass = [&](const char* fragEntry) -> vk::raii::Pipeline {
                std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
                    vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex,   .module = *shaderModule, .pName = "VSMain" },
                    vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = *shaderModule, .pName = fragEntry }
                };
                vk::PipelineVertexInputStateCreateInfo vertexInput;
                vk::PipelineInputAssemblyStateCreateInfo inputAssembly{ .topology = vk::PrimitiveTopology::eTriangleList };
                vk::PipelineViewportStateCreateInfo viewportState{ .viewportCount = 1, .scissorCount = 1 };
                std::array<vk::DynamicState, 2> dynStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
                vk::PipelineDynamicStateCreateInfo dynamicState{ .dynamicStateCount = 2, .pDynamicStates = dynStates.data() };
                vk::PipelineRasterizationStateCreateInfo rasterizer{ .polygonMode = vk::PolygonMode::eFill, .cullMode = vk::CullModeFlagBits::eNone, .lineWidth = 1.0f };
                vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = vk::SampleCountFlagBits::e1 };
                vk::PipelineDepthStencilStateCreateInfo depthStencil{ .depthTestEnable = vk::False, .depthWriteEnable = vk::False };
                vk::PipelineColorBlendAttachmentState blendAttach{
                    .blendEnable = vk::False,
                    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                };
                vk::PipelineColorBlendStateCreateInfo colorBlending{ .attachmentCount = 1, .pAttachments = &blendAttach };
                vk::Format format = vk::Format::eR16G16B16A16Sfloat;
                vk::PipelineRenderingCreateInfo renderingInfo{ .colorAttachmentCount = 1, .pColorAttachmentFormats = &format };

                return device.createGraphicsPipeline(nullptr, vk::GraphicsPipelineCreateInfo{
                    .pNext = &renderingInfo,
                    .stageCount = 2,
                    .pStages = stages.data(),
                    .pVertexInputState = &vertexInput,
                    .pInputAssemblyState = &inputAssembly,
                    .pViewportState = &viewportState,
                    .pRasterizationState = &rasterizer,
                    .pMultisampleState = &multisampling,
                    .pDepthStencilState = &depthStencil,
                    .pColorBlendState = &colorBlending,
                    .pDynamicState = &dynamicState,
                    .layout = *m_SSRPipelineLayout
                    });
                };

            m_SSRPipeline = buildPass("PSMain");
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[SSR ERROR]: " << e.what() << std::endl;
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

    void SceneRenderer::RenderModelNode(Node* node, const glm::mat4& parentMatrix, vk::raii::CommandBuffer& cmd,
        const Model* model, CameraComponent* camera, bool renderTransparentOnly, Entity* ownerEntity)
    {
        if (!node) return;

        glm::mat4 globalMatrix = parentMatrix * node->getLocalMatrix();

        if (node->mesh && node->mesh->IsLoaded()) {
            auto& material = const_cast<Model*>(model)->materials[node->materialIndex];
            bool isTransparent = (material.alphaMode == AlphaMode::BLEND);

            if (isTransparent == renderTransparentOnly) {
                auto& lowLevelRenderer = VulkanRenderer::Get();
                uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

                pushMaterialProperties(*cmd, model, node->materialIndex, globalMatrix, ownerEntity);

                VkDescriptorSet set0 = !lowLevelRenderer.vulkanCommandBuffer->descriptorSets.empty() ?
                    *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame] : VK_NULL_HANDLE;
                VkDescriptorSet set1 = *material.descriptorSet;

                if (set0 != VK_NULL_HANDLE && set1 != VK_NULL_HANDLE) {
                    VkDescriptorSet rawSets[] = { set0, set1 };
                    vk::ArrayProxy<const vk::DescriptorSet> setsProxy(2, reinterpret_cast<const vk::DescriptorSet*>(rawSets));
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrPipelineLayout, 0, setsProxy, {});
                }
                else if (set0 != VK_NULL_HANDLE) {
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrPipelineLayout, 0, { vk::DescriptorSet(set0) }, {});
                }

                cmd.bindVertexBuffers(0, { *node->mesh->GetVertexBuffer() }, { 0 });
                cmd.bindIndexBuffer(*node->mesh->GetIndexBuffer(), 0, vk::IndexType::eUint32);
                cmd.drawIndexed(node->mesh->GetIndexCount(), 1, 0, 0, 0);
            }
        }

        // Still traverse children, but skip drawing BLEND geometry in the opaque G-Buffer pass
        for (auto child : node->children) {
            RenderModelNode(child, globalMatrix, cmd, model, camera, renderTransparentOnly, ownerEntity);
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

        // Forward drawing execution directly to your cached SceneRenderer handle pointer!
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

        vk::Image swapchainBackbufferImage = swapchainSubsystem->swapChainImages[imageIndex];

        // TRANSITION THE BACKBUFFER FROM PRESENT/UNDEFINED TO WRITABLE ATTACHMENT OPTIMAL
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
        colorAttachment.setImageView(swapchainSubsystem->swapChainImageViews[imageIndex]) // Output straight onto backbuffer!
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

                // Support modern ModelComponents in the fallback path
                if (entity->GetComponent<ModelComponent>() || entity->GetComponent<MeshComponent>()) {
                    visibleEntities.push_back(entity);
                }
            }
            return;
        }

        Frustum frustum = camera->GetFrustum();
        for (auto entity : allEntities) {
            if (!entity || !entity->IsActive()) continue;

            // Check for ModelComponent first
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
            vk::ImageAspectFlags aspect = (name == "Depth" || name == "ShadowMap") ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
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
                bool isDepth = (resource.name == "Depth" || resource.name == "ShadowMap");

                vk::ImageLayout targetReadLayout = isDepth
                    ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                    : vk::ImageLayout::eShaderReadOnlyOptimal;

                // Transition from resource.finalLayout (what the producer pass wrote), NOT initialLayout (eUndefined)!
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
                bool isDepth = (resource.name == "Depth" || resource.name == "ShadowMap");

                // Dynamically route the proper layout types!
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
                bool isDepth = (resource.name == "Depth" || resource.name == "ShadowMap");

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
            // Handle unsupported transition combinations. Production code would include additional common transition patterns
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
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);

        graph.AddResource("ShadowMap", vk::Format::eD32Sfloat, { 4096, 4096 },
            vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilReadOnlyOptimal);

        graph.AddResource("GBuffer_Emissive", vk::Format::eR16G16B16A16Sfloat, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("GBuffer_SurfaceExt", vk::Format::eR8G8B8A8Unorm, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("SSAO_Raw", vk::Format::eR8Unorm, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("SSAO_Blur", vk::Format::eR8Unorm, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        // Configure final color buffer for the completed lighting result. Standard color format with transfer capability for presentation or post-processing
        graph.AddResource("FinalColor", vk::Format::eB8G8R8A8Srgb, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        uint32_t halfW = std::max(1u, width / 2);
        uint32_t halfH = std::max(1u, height / 2);

        graph.AddResource("HDRColor", vk::Format::eR16G16B16A16Sfloat, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("SSRColor", vk::Format::eR16G16B16A16Sfloat, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("FogColor", vk::Format::eR16G16B16A16Sfloat, { width, height },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("BloomPing", vk::Format::eR16G16B16A16Sfloat, { halfW, halfH },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddResource("BloomPong", vk::Format::eR16G16B16A16Sfloat, { halfW, halfH },
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);

        graph.AddPass("ShadowPass",
            {},
            { "ShadowMap" },
            [&graph](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
            {
                auto* shadowRes = graph.GetResource("ShadowMap");
                if (!shadowRes) return;

                vk::RenderingAttachmentInfoKHR depthAttachment;
                depthAttachment.setImageView(*shadowRes->view)
                    .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                    .setLoadOp(vk::AttachmentLoadOp::eClear)
                    .setStoreOp(vk::AttachmentStoreOp::eStore)
                    .setClearValue(vk::ClearDepthStencilValue(1.0f, 0));

                vk::RenderingInfoKHR renderingInfo;
                renderingInfo.setRenderArea({ { 0, 0 }, { 4096, 4096 } })
                    .setLayerCount(1)
                    .setPDepthAttachment(&depthAttachment);

                cmd.beginRendering(renderingInfo);

                auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                glm::mat4 lightSpaceMatrices[4];
                glm::ivec4 shadowEnabled(0);
                pbrRenderer.ComputeShadowAtlasMatrices(lightSpaceMatrices, shadowEnabled);
                if (shadowEnabled.x || shadowEnabled.y || shadowEnabled.z || shadowEnabled.w) {
                    pbrRenderer.RenderShadowPass(cmd, lightSpaceMatrices, shadowEnabled);
                }

                cmd.endRendering();
            });

        // Configure geometry pass for G - Buffer population
        // This pass renders all geometry and stores intermediate data for lighting calculations
        graph.AddPass("GeometryPass",
            {},                                                        // No input dependencies - first pass in pipeline
            { "GBuffer_Position", "GBuffer_Normal", "GBuffer_Albedo", "GBuffer_Emissive", "GBuffer_SurfaceExt", "Depth" },  // Outputs all G-Buffer components
            [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
            {
                // 1. Fetch the compiled resource descriptors directly from your rendergraph map pointers
                auto* posRes = graph.GetResource("GBuffer_Position");
                auto* normalRes = graph.GetResource("GBuffer_Normal");
                auto* albedoRes = graph.GetResource("GBuffer_Albedo");
                auto* emissiveRes = graph.GetResource("GBuffer_Emissive");
                auto* surfaceRes = graph.GetResource("GBuffer_SurfaceExt");
                auto* depthRes = graph.GetResource("Depth");

                // Ensure resources are compiled before setting up attachment views
                if (posRes && normalRes && albedoRes && emissiveRes && depthRes)
                {
                    // Configure multiple render target attachments for G-Buffer output
                    // Each attachment corresponds to a different geometric property
                    std::array<vk::RenderingAttachmentInfoKHR, 5> colorAttachments;

                    // Configure position attachment - world space vertex positions
                    vk::ClearValue clearColor = vk::ClearColorValue(std::array<float, 4>{ 0.1f, 0.1f, 0.15f, 1.0f });
                    // Configure position attachment - Dereference the view handle safely using the * operator
                    colorAttachments[0].setImageView(*posRes->view)    // Target position buffer
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)  // Optimal for writes
                        .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to known state
                        .setStoreOp(vk::AttachmentStoreOp::eStore)    // Preserve for lighting pass
                        .setClearValue(vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }));

                    // Configure normal attachment - surface normals in world space
                    colorAttachments[1].setImageView(*normalRes->view)      // Target normal buffer
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to default normal
                        .setStoreOp(vk::AttachmentStoreOp::eStore)    // Preserve for lighting
                        .setClearValue(vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }));

                    // Configure albedo attachment - surface color and material properties
                    colorAttachments[2].setImageView(*albedoRes->view)      // Target albedo buffer
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eClear)       // Clear to default color
                        .setStoreOp(vk::AttachmentStoreOp::eStore)    // Preserve for lighting
                        .setClearValue(clearColor);

                    colorAttachments[3].setImageView(*emissiveRes->view)
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eClear)
                        .setStoreOp(vk::AttachmentStoreOp::eStore)
                        .setClearValue(vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }));

                    colorAttachments[4].setImageView(*surfaceRes->view)
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eClear)
                        .setStoreOp(vk::AttachmentStoreOp::eStore)
                        .setClearValue(vk::ClearColorValue(std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f }));

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
                
                // Apply dynamic state properties explicitly inside the open pass context!
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

        graph.AddPass("SSAOPass",
            { "GBuffer_Position", "GBuffer_Normal" },
            { "SSAO_Blur" },
            [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
            {
                auto* posRes = graph.GetResource("GBuffer_Position");
                auto* normRes = graph.GetResource("GBuffer_Normal");
                auto* rawRes = graph.GetResource("SSAO_Raw");
                auto* blurRes = graph.GetResource("SSAO_Blur");
                if (!posRes || !normRes || !rawRes || !blurRes) return;

                auto& lowLevelRenderer = VulkanRenderer::Get();
                auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();
                vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();

                auto allocateSSAOSet = [&](vk::raii::DescriptorSet& set, vk::ImageView v0, vk::ImageView v1) {
                    if (*set) return;
                    vk::DescriptorSetLayout layout = *pbrRenderer.m_SSAOSetLayout;
                    set = std::move(logicalDevice.allocateDescriptorSets({
                        .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                        .descriptorSetCount = 1,
                        .pSetLayouts = &layout
                        }).front());

                    vk::DescriptorImageInfo i0{ sampler, v0, vk::ImageLayout::eShaderReadOnlyOptimal };
                    vk::DescriptorImageInfo i1{ sampler, v1, vk::ImageLayout::eShaderReadOnlyOptimal };
                    std::array<vk::WriteDescriptorSet, 2> writes = {
                        vk::WriteDescriptorSet{ .dstSet = *set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &i0 },
                        vk::WriteDescriptorSet{ .dstSet = *set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &i1 }
                    };
                    logicalDevice.updateDescriptorSets(writes, {});
                    };

                allocateSSAOSet(pbrRenderer.m_SSAOSet, *posRes->view, *normRes->view);
                allocateSSAOSet(pbrRenderer.m_SSAOBlurSet, *rawRes->view, *rawRes->view);

                vk::Viewport vp(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
                vk::Rect2D scissor({ 0, 0 }, { width, height });

                // 1. Render SSAO_Raw
                vk::ImageMemoryBarrier rawToColor{
                    .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .image = *rawRes->image,
                    .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
                };
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, { rawToColor });

                vk::RenderingAttachmentInfoKHR rawAttach;
                rawAttach.setImageView(*rawRes->view).setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                    .setLoadOp(vk::AttachmentLoadOp::eDontCare).setStoreOp(vk::AttachmentStoreOp::eStore);

                cmd.beginRendering({ .renderArea = scissor, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &rawAttach });
                cmd.setViewport(0, { vp });
                cmd.setScissor(0, { scissor });
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_SSAOPipeline);
                std::array<vk::DescriptorSet, 2> sets0 = { *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame], *pbrRenderer.m_SSAOSet };
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_SSAOPipelineLayout, 0, sets0, {});
                cmd.draw(3, 1, 0, 0);
                cmd.endRendering();

                vk::ImageMemoryBarrier rawToRead{
                    .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                    .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                    .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                    .image = *rawRes->image,
                    .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
                };
                cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { rawToRead });

                // 2. Blur SSAO_Raw -> SSAO_Blur (Rendergraph transitions SSAO_Blur before and after)
                vk::RenderingAttachmentInfoKHR blurAttach;
                blurAttach.setImageView(*blurRes->view).setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                    .setLoadOp(vk::AttachmentLoadOp::eDontCare).setStoreOp(vk::AttachmentStoreOp::eStore);

                cmd.beginRendering({ .renderArea = scissor, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &blurAttach });
                cmd.setViewport(0, { vp });
                cmd.setScissor(0, { scissor });
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_SSAOBlurPipeline);
                std::array<vk::DescriptorSet, 2> sets1 = { *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame], *pbrRenderer.m_SSAOBlurSet };
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_SSAOPipelineLayout, 0, sets1, {});
                cmd.draw(3, 1, 0, 0);
                cmd.endRendering();
            });

        // Configure lighting pass for screen-space illumination calculations
        // This pass reads G-Buffer data and computes final lighting for each pixel
        graph.AddPass("LightingPass",
            { "GBuffer_Position", "GBuffer_Normal", "GBuffer_Albedo", "GBuffer_Emissive", "GBuffer_SurfaceExt", "SSAO_Blur", "ShadowMap" },  // Read all G-Buffer components
            { "HDRColor" },                                            // Output final lit result
            [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
            {
                auto* finalRes = graph.GetResource("HDRColor");
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
                // LINK THE PROCEDURAL FULL-SCREEN COMPOSITION PASS SHADER HERE

                 // Apply dynamic state properties explicitly for full-screen quad processing!
                float fWidth = static_cast<float>(width);
                float fHeight = static_cast<float>(height);
                cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, fWidth, fHeight, 0.0f, 1.0f) });
                cmd.setScissor(0, { vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D{width, height}) });

                auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pbrRenderer.GetDeferredPipeline());

                auto* posRes = graph.GetResource("GBuffer_Position");
                auto* normalRes = graph.GetResource("GBuffer_Normal");
                auto* albedoRes = graph.GetResource("GBuffer_Albedo");
                auto* emissiveRes = graph.GetResource("GBuffer_Emissive");
                auto* surfaceRes = graph.GetResource("GBuffer_SurfaceExt");
                auto* shadowRes = graph.GetResource("ShadowMap");

                auto& lowLevelRenderer = VulkanRenderer::Get();
                auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

                // Allocate and write ONLY ONCE so the handle stays alive while the GPU executes!
                if (!*pbrRenderer.m_DeferredGBufferSet) {
                    vk::DescriptorSetLayout deferredLayout = pbrRenderer.GetDeferredSetLayout();
                    vk::DescriptorSetAllocateInfo allocInfo{
                        .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                        .descriptorSetCount = 1,
                        .pSetLayouts = &deferredLayout
                    };
                    pbrRenderer.m_DeferredGBufferSet = std::move(logicalDevice.allocateDescriptorSets(allocInfo).front());

                    vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();
                    std::array<vk::DescriptorImageInfo, 10> imageInfos = {
                        vk::DescriptorImageInfo{ sampler, *posRes->view,     vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *normalRes->view,  vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *albedoRes->view,  vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *emissiveRes->view,vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *shadowRes->view,  vk::ImageLayout::eDepthStencilReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *surfaceRes->view, vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ *pbrRenderer.m_EnvCubemap->GetSampler(), *pbrRenderer.m_EnvCubemap->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ *pbrRenderer.m_EnvCubemap->GetSampler(), *pbrRenderer.m_EnvCubemap->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ *pbrRenderer.m_BrdfLutTex->GetSampler(), *pbrRenderer.m_BrdfLutTex->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal },
                        vk::DescriptorImageInfo{ sampler, *graph.GetResource("SSAO_Blur")->view, vk::ImageLayout::eShaderReadOnlyOptimal }
                    };

                    std::array<vk::WriteDescriptorSet, 10> writes;
                    for (uint32_t b = 0; b < 10; ++b) {
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

                bool shadowEnabled = false;
                glm::mat4 lightSpaceMatrix = pbrRenderer.ComputeLightSpaceMatrix(shadowEnabled);
                SceneRenderer::DeferredShadowPushConstants shadowPC{
                    lightSpaceMatrix,
                    shadowEnabled ? 1 : 0
                };
                cmd.pushConstants(
                    pbrRenderer.GetDeferredPipelineLayout(),
                    vk::ShaderStageFlagBits::eFragment,
                    0,
                    sizeof(SceneRenderer::DeferredShadowPushConstants),
                    &shadowPC
                );

                // Process full-screen procedural triangle composition draw(3)
                cmd.draw(3, 1, 0, 0);

                cmd.endRendering();                                             // Complete lighting calculations
            });

            graph.AddPass("TransparentPass",
                { "HDRColor" }, // Depends on LightingPass (FinalColor) and GeometryPass (Depth)
                {},                        // Modifies FinalColor in-place without transitioning it away from eShaderReadOnlyOptimal
                [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
                {
                    auto* finalRes = graph.GetResource("HDRColor");
                    auto* depthRes = graph.GetResource("Depth");
                    if (!finalRes || !depthRes) return;

                    // Transition FinalColor back from eShaderReadOnlyOptimal (set by LightingPass) to eColorAttachmentOptimal
                    vk::ImageMemoryBarrier toColorBarrier{
                        .srcAccessMask = vk::AccessFlagBits::eShaderRead,
                        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead,
                        .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = *finalRes->image,
                        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
                    };
                    cmd.pipelineBarrier(
                        vk::PipelineStageFlagBits::eFragmentShader,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        {}, {}, {}, { toColorBarrier }
                    );

                    // Load existing lit scene and depth buffer
                    vk::RenderingAttachmentInfoKHR colorAttachment;
                    colorAttachment.setImageView(*finalRes->view)
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eLoad)
                        .setStoreOp(vk::AttachmentStoreOp::eStore);

                    vk::RenderingAttachmentInfoKHR depthAttachment;
                    depthAttachment.setImageView(*depthRes->view)
                        .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eLoad)
                        .setStoreOp(vk::AttachmentStoreOp::eDontCare);

                    vk::RenderingInfoKHR renderingInfo;
                    renderingInfo.setRenderArea({ {0, 0}, {width, height} })
                        .setLayerCount(1)
                        .setColorAttachmentCount(1)
                        .setPColorAttachments(&colorAttachment)
                        .setPDepthAttachment(&depthAttachment);

                    auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                    pbrRenderer.DispatchAndRenderParticles(cmd, ImGui::GetIO().DeltaTime);

                    cmd.beginRendering(renderingInfo);

                    float fWidth = static_cast<float>(width);
                    float fHeight = static_cast<float>(height);
                    cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, fWidth, fHeight, 0.0f, 1.0f) });
                    cmd.setScissor(0, { vk::Rect2D({0, 0}, {width, height}) });

                    auto& lowLevelRenderer = VulkanRenderer::Get();
                    auto currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();

                    pbrRenderer.RenderTransparentPass(Entity::GetActiveSceneEntities(), cmd);
                    if (*pbrRenderer.m_ParticleRenderPipeline && *pbrRenderer.m_ParticleSet) {
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_ParticleRenderPipeline);
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            *pbrRenderer.m_ParticlePipelineLayout, 0,
                            { *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame],
                            *pbrRenderer.m_ParticleSet }, {});

                        cmd.pushConstants(*pbrRenderer.m_ParticlePipelineLayout,
                            vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex,
                            0, sizeof(SceneRenderer::ParticlePushConstants), &pbrRenderer.m_LastParticlePC);

                        if (pbrRenderer.m_HasActiveParticles)
                        {
                            cmd.draw(6, SceneRenderer::MAX_PARTICLES, 0, 0);
                        }
                    }

                    cmd.endRendering();

                    // Transition FinalColor back to eShaderReadOnlyOptimal so ImGui can sample it in the Viewport window
                    vk::ImageMemoryBarrier toShaderReadBarrier{
                        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = *finalRes->image,
                        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
                    };
                    cmd.pipelineBarrier(
                        vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, {}, {}, { toShaderReadBarrier }
                    );
                });

            graph.AddPass("FogPass",
                { "HDRColor" },
                { "FogColor" },
                [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
                {
                    auto* hdrRes = graph.GetResource("HDRColor");
                    auto* posRes = graph.GetResource("GBuffer_Position");
                    auto* shadowRes = graph.GetResource("ShadowMap");
                    auto* fogRes = graph.GetResource("FogColor");
                    if (!hdrRes || !posRes || !shadowRes || !fogRes) return;

                    auto& lowLevelRenderer = VulkanRenderer::Get();
                    auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                    auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                    uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();
                    vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();

                    if (!*pbrRenderer.m_FogSet) {
                        vk::DescriptorSetLayout layout = *pbrRenderer.m_FogSetLayout;
                        pbrRenderer.m_FogSet = std::move(logicalDevice.allocateDescriptorSets({
                            .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                            .descriptorSetCount = 1,
                            .pSetLayouts = &layout
                            }).front());

                        std::array<vk::DescriptorImageInfo, 3> infos = {
                            vk::DescriptorImageInfo{ sampler, *hdrRes->view,    vk::ImageLayout::eShaderReadOnlyOptimal },
                            vk::DescriptorImageInfo{ sampler, *posRes->view,    vk::ImageLayout::eShaderReadOnlyOptimal },
                            vk::DescriptorImageInfo{ sampler, *shadowRes->view, vk::ImageLayout::eDepthStencilReadOnlyOptimal }
                        };
                        std::array<vk::WriteDescriptorSet, 3> writes;
                        for (uint32_t b = 0; b < 3; ++b) {
                            writes[b] = vk::WriteDescriptorSet{
                                .dstSet = *pbrRenderer.m_FogSet,
                                .dstBinding = b,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                .pImageInfo = &infos[b]
                            };
                        }
                        logicalDevice.updateDescriptorSets(writes, {});
                    }

                    vk::RenderingAttachmentInfoKHR colorAttachment;
                    colorAttachment.setImageView(*fogRes->view)
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eDontCare)
                        .setStoreOp(vk::AttachmentStoreOp::eStore);

                    cmd.beginRendering({
                        .renderArea = { { 0, 0 },{ width, height } },
                        .layerCount = 1,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &colorAttachment
                        });
                    cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f) });
                    cmd.setScissor(0, { vk::Rect2D({ 0, 0 },{ width, height }) });

                    if (*pbrRenderer.m_FogPipeline) {
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_FogPipeline);
                        std::array<vk::DescriptorSet, 2> sets = {
                            *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame],
                            *pbrRenderer.m_FogSet
                        };
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_FogPipelineLayout, 0, sets, {});

                        glm::mat4 invVP(1.0f);
                        for (Entity* ent : Entity::GetActiveSceneEntities()) {
                            if (ent && ent->IsActive()) {
                                if (auto* cam = ent->GetComponent<CameraComponent>()) {
                                    invVP = glm::inverse(cam->GetProjectionMatrix() * cam->GetViewMatrix());
                                    break;
                                }
                            }
                        }

                        SceneRenderer::FogPushConstants pc{
                            invVP,
                            glm::vec4(pbrRenderer.m_FogColor, pbrRenderer.m_FogDensity),
                            glm::vec4(pbrRenderer.m_FogSunScatter, pbrRenderer.m_FogHeightFalloff, pbrRenderer.m_FogBaseHeight, pbrRenderer.m_FogMaxDistance)
                        };

                        cmd.pushConstants(*pbrRenderer.m_FogPipelineLayout,
                            vk::ShaderStageFlagBits::eFragment, 0, sizeof(pc), &pc);
                        cmd.draw(3, 1, 0, 0);
                    }
                    cmd.endRendering();
                });

            graph.AddPass("SSRPass",
                { "FogColor" },
                { "SSRColor" },
                [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
                {
                    auto* fogRes = graph.GetResource("FogColor");
                    auto* posRes = graph.GetResource("GBuffer_Position");
                    auto* normRes = graph.GetResource("GBuffer_Normal");
                    auto* albRes = graph.GetResource("GBuffer_Albedo");
                    auto* ssrRes = graph.GetResource("SSRColor");
                    if (!fogRes || !posRes || !normRes || !albRes || !ssrRes) return;

                    auto& lowLevelRenderer = VulkanRenderer::Get();
                    auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                    auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                    uint32_t currentFrame = lowLevelRenderer.vulkanSwapChain->GetFrameIndex();
                    vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();

                    if (!*pbrRenderer.m_SSRSet) {
                        vk::DescriptorSetLayout layout = *pbrRenderer.m_SSRSetLayout;
                        pbrRenderer.m_SSRSet = std::move(logicalDevice.allocateDescriptorSets({
                            .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                            .descriptorSetCount = 1,
                            .pSetLayouts = &layout
                            }).front());

                        std::array<vk::DescriptorImageInfo, 4> infos = {
                            vk::DescriptorImageInfo{ sampler, *fogRes->view,  vk::ImageLayout::eShaderReadOnlyOptimal },
                            vk::DescriptorImageInfo{ sampler, *posRes->view,  vk::ImageLayout::eShaderReadOnlyOptimal },
                            vk::DescriptorImageInfo{ sampler, *normRes->view, vk::ImageLayout::eShaderReadOnlyOptimal },
                            vk::DescriptorImageInfo{ sampler, *albRes->view,  vk::ImageLayout::eShaderReadOnlyOptimal }
                        };
                        std::array<vk::WriteDescriptorSet, 4> writes;
                        for (uint32_t b = 0; b < 4; ++b) {
                            writes[b] = vk::WriteDescriptorSet{
                                .dstSet = *pbrRenderer.m_SSRSet,
                                .dstBinding = b,
                                .descriptorCount = 1,
                                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                                .pImageInfo = &infos[b]
                            };
                        }
                        logicalDevice.updateDescriptorSets(writes, {});
                    }

                    vk::RenderingAttachmentInfoKHR colorAttachment;
                    colorAttachment.setImageView(*ssrRes->view)
                        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eDontCare)
                        .setStoreOp(vk::AttachmentStoreOp::eStore);

                    cmd.beginRendering({
                        .renderArea = { { 0, 0 },{ width, height } },
                        .layerCount = 1,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &colorAttachment
                        });

                    cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f) });
                    cmd.setScissor(0, { vk::Rect2D({ 0, 0 },{ width, height }) });

                    if (*pbrRenderer.m_SSRPipeline) {
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_SSRPipeline);
                        std::array<vk::DescriptorSet, 2> sets = {
                            *lowLevelRenderer.vulkanCommandBuffer->descriptorSets[currentFrame],
                            *pbrRenderer.m_SSRSet
                        };
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_SSRPipelineLayout, 0, sets, {});

                        SceneRenderer::SSRPushConstants pc{
                            pbrRenderer.m_SSRMaxDistance,
                            0.35f,
                            pbrRenderer.m_SSRThickness,
                            pbrRenderer.m_SSRIntensity
                        };
                        cmd.pushConstants(*pbrRenderer.m_SSRPipelineLayout,
                            vk::ShaderStageFlagBits::eFragment, 0, sizeof(pc), &pc);

                        cmd.draw(3, 1, 0, 0);
                    }

                    cmd.endRendering();
                });

            graph.AddPass("BloomPass",
                { "SSRColor" },
                {}, // Manage BloomPing/Pong transitions explicitly inside the pass (avoids double-transition VUID-01197)
                [&graph, halfW, halfH](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
                {
                    auto* ssrRes = graph.GetResource("SSRColor");
                    auto* pingRes = graph.GetResource("BloomPing");
                    auto* pongRes = graph.GetResource("BloomPong");
                    if (!ssrRes || !pingRes || !pongRes) return;

                    auto& lowLevelRenderer = VulkanRenderer::Get();
                    auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                    auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                    vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();

                    auto allocateSingleSet = [&](vk::raii::DescriptorSet& set, vk::ImageView view) {
                        if (*set) return;
                        vk::DescriptorSetLayout layout = *pbrRenderer.m_BloomSetLayout;
                        set = std::move(logicalDevice.allocateDescriptorSets({
                            .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                            .descriptorSetCount = 1,
                            .pSetLayouts = &layout
                            }).front());

                        vk::DescriptorImageInfo info{ sampler, view, vk::ImageLayout::eShaderReadOnlyOptimal };
                        std::array<vk::WriteDescriptorSet, 2> writes = {
                            vk::WriteDescriptorSet{.dstSet = *set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &info },
                            vk::WriteDescriptorSet{.dstSet = *set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &info }
                        };
                        logicalDevice.updateDescriptorSets(writes, {});
                        };

                    allocateSingleSet(pbrRenderer.m_BloomExtractSet, *ssrRes->view);
                    allocateSingleSet(pbrRenderer.m_BloomBlurHorizSet, *pingRes->view);
                    allocateSingleSet(pbrRenderer.m_BloomBlurVertSet, *pongRes->view);

                    float fW = static_cast<float>(halfW);
                    float fH = static_cast<float>(halfH);
                    vk::Viewport vp(0.0f, 0.0f, fW, fH, 0.0f, 1.0f);
                    vk::Rect2D scissor({ 0, 0 }, { halfW, halfH });

                    auto runPass = [&](vk::Image targetImg, vk::ImageView targetView,
                        vk::Pipeline pipeline, vk::DescriptorSet set, glm::vec2 dir) {
                            // Transition from eUndefined (valid from any prior layout when loadOp = eDontCare!)
                            vk::ImageMemoryBarrier toColor{
                                .srcAccessMask = vk::AccessFlagBits::eShaderRead,
                                .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                                .oldLayout = vk::ImageLayout::eUndefined,
                                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                .image = targetImg,
                                .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
                            };
                            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, { toColor });

                            vk::RenderingAttachmentInfoKHR attach;
                            attach.setImageView(targetView).setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                .setLoadOp(vk::AttachmentLoadOp::eDontCare).setStoreOp(vk::AttachmentStoreOp::eStore);

                            cmd.beginRendering({ .renderArea = scissor, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &attach });
                            cmd.setViewport(0, { vp });
                            cmd.setScissor(0, { scissor });
                            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_BloomPipelineLayout, 0, { set }, {});

                            SceneRenderer::BloomPushConstants pc{ dir, 2.5f, pbrRenderer.m_BloomIntensity };
                            cmd.pushConstants(*pbrRenderer.m_BloomPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(pc), &pc);
                            cmd.draw(3, 1, 0, 0);
                            cmd.endRendering();

                            vk::ImageMemoryBarrier toRead{
                                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
                                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                                .image = targetImg,
                                .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
                            };
                            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { toRead });
                        };

                    // 1. Bright extract HDRColor -> BloomPing (ends in eShaderReadOnlyOptimal)
                    runPass(*pingRes->image, *pingRes->view, *pbrRenderer.m_BloomExtractPipeline, *pbrRenderer.m_BloomExtractSet, { 0.0f, 0.0f });
                    // 2. Horizontal blur BloomPing -> BloomPong (ends in eShaderReadOnlyOptimal)
                    runPass(*pongRes->image, *pongRes->view, *pbrRenderer.m_BloomBlurPipeline, *pbrRenderer.m_BloomBlurHorizSet, { 1.0f, 0.0f });
                    // 3. Vertical blur BloomPong -> BloomPing (ends in eShaderReadOnlyOptimal)
                    runPass(*pingRes->image, *pingRes->view, *pbrRenderer.m_BloomBlurPipeline, *pbrRenderer.m_BloomBlurVertSet, { 0.0f, 1.0f });
                });

            graph.AddPass("CompositePass",
                { "SSRColor" }, // BloomPing is already in eShaderReadOnlyOptimal from BloomPass!
                { "FinalColor" },
                [&graph, width, height](vk::raii::CommandBuffer& cmd, uint32_t imageIndex)
                {
                    auto* ssrRes = graph.GetResource("SSRColor");
                    auto* pingRes = graph.GetResource("BloomPing");
                    auto* finalRes = graph.GetResource("FinalColor");
                    if (!ssrRes || !pingRes || !finalRes) return;

                    auto& lowLevelRenderer = VulkanRenderer::Get();
                    auto& logicalDevice = lowLevelRenderer.GetLogicalDevice();
                    auto& pbrRenderer = ServiceLocator::Get<SceneRenderer>();
                    vk::Sampler sampler = lowLevelRenderer.imGui->GetSampler();

                    if (!*pbrRenderer.m_BloomCompositeSet) {
                        vk::DescriptorSetLayout layout = *pbrRenderer.m_BloomSetLayout;
                        pbrRenderer.m_BloomCompositeSet = std::move(logicalDevice.allocateDescriptorSets({
                            .descriptorPool = *lowLevelRenderer.vulkanCommandBuffer->descriptorPool,
                            .descriptorSetCount = 1,
                            .pSetLayouts = &layout
                            }).front());

                        vk::DescriptorImageInfo hdrInfo{ sampler, *ssrRes->view, vk::ImageLayout::eShaderReadOnlyOptimal };
                        vk::DescriptorImageInfo bloomInfo{ sampler, *pingRes->view, vk::ImageLayout::eShaderReadOnlyOptimal };
                        std::array<vk::WriteDescriptorSet, 2> writes = {
                            vk::WriteDescriptorSet{.dstSet = *pbrRenderer.m_BloomCompositeSet, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &hdrInfo },
                            vk::WriteDescriptorSet{.dstSet = *pbrRenderer.m_BloomCompositeSet, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &bloomInfo }
                        };
                        logicalDevice.updateDescriptorSets(writes, {});
                    }

                    vk::RenderingAttachmentInfoKHR attach;
                    attach.setImageView(*finalRes->view).setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setLoadOp(vk::AttachmentLoadOp::eDontCare).setStoreOp(vk::AttachmentStoreOp::eStore);

                    cmd.beginRendering({ .renderArea = { {0, 0}, {width, height} }, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &attach });
                    cmd.setViewport(0, { vk::Viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f) });
                    cmd.setScissor(0, { vk::Rect2D({0, 0}, {width, height}) });
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_BloomCompositePipeline);
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pbrRenderer.m_BloomPipelineLayout, 0, { *pbrRenderer.m_BloomCompositeSet }, {});

                    SceneRenderer::BloomPushConstants pc{ { 0.0f, 0.0f }, 2.5f, pbrRenderer.m_BloomIntensity };
                    cmd.pushConstants(*pbrRenderer.m_BloomPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(pc), &pc);
                    cmd.draw(3, 1, 0, 0);
                    cmd.endRendering();
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
                return i; // Return the exact matching type index found!
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