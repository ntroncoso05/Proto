#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include "Platform/Vulkan/VK_CommandBuffer.hpp"
#include "Platform/Vulkan/VK_Image.hpp"
#include <vulkan/vulkan_raii.hpp>
#include <imgui.h>
#include <glm/glm.hpp>

namespace Proto
{
    class Entity;

    class ImGuiVulkanUtil {
    private:
        std::unique_ptr<VulkanCommandBuffer> vulkanCommandBuffer;
        std::unique_ptr<VulkanImage> vulkanImage;
        static constexpr uint32_t MAX_FRAMES = 2;

        // Core GPU rendering resources for UI display. These objects form the foundation of our ImGui-to-Vulkan rendering pipeline
        vk::raii::Sampler sampler{ nullptr };                    // Texture sampling configuration for font rendering
        std::array<vk::raii::Buffer, MAX_FRAMES> vertexBuffers{ nullptr, nullptr };   // Dynamic vertex buffer for UI geometry
        std::array<vk::raii::DeviceMemory, MAX_FRAMES> vertexBufferMemories{ nullptr, nullptr };
        std::array<int32_t, MAX_FRAMES> vertexCounts{ 0, 0 };                         // Current vertex count for draw commands

        std::array<vk::raii::Buffer, MAX_FRAMES> indexBuffers{ nullptr, nullptr };   // Dynamic index buffer for UI triangle connectivity
        std::array<vk::raii::DeviceMemory, MAX_FRAMES> indexBufferMemories{ nullptr, nullptr };
        std::array<int32_t, MAX_FRAMES> indexCounts{ 0, 0 };                         // Current index count for draw commands

        vk::raii::Image fontImage{ nullptr };                                        // GPU texture containing ImGui font atlas
        vk::raii::DeviceMemory fontImageMemory{ nullptr };
        vk::raii::ImageView fontImageView{ nullptr };                                // Shader-accessible view of font texture

        // Vulkan pipeline infrastructure for UI rendering. These objects define the complete GPU processing pipeline for ImGui elements
        vk::raii::PipelineCache pipelineCache{ nullptr };        // Pipeline compilation cache for faster startup
        vk::raii::PipelineLayout pipelineLayout{ nullptr };      // Resource binding layout (textures, uniforms)
        vk::raii::Pipeline pipeline{ nullptr };                  // Complete graphics pipeline for UI rendering
        vk::raii::DescriptorPool descriptorPool{ nullptr };      // Pool for allocating descriptor sets
        vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr }; // Layout defining shader resource bindings
        vk::raii::DescriptorSet descriptorSet{ nullptr };        // Actual resource bindings for font texture

        // Vulkan device context and system integration. These references connect our UI system to the broader Vulkan application context
        vk::raii::Device* device = nullptr;                    // Primary Vulkan device for resource creation
        vk::raii::PhysicalDevice* physicalDevice = nullptr;    // GPU hardware info for capability queries
        vk::raii::Queue* graphicsQueue = nullptr;              // Command submission queue for UI rendering
        uint32_t graphicsQueueFamily = 0;                      // Queue family index for validation

        // UI state management and rendering configuration. These members control the visual appearance and dynamic behavior of the UI system
        ImGuiStyle vulkanStyle;                                 // Custom visual styling for Vulkan applications

        // Push constants for efficient per-frame parameter updates. This structure enables fast updates of transformation and styling data
        struct PushConstBlock {
            glm::vec2 scale;                                    // UI scaling factors for different screen sizes
            glm::vec2 translate;                                // Translation offset for UI positioning
        } pushConstBlock;

        // Dynamic state tracking for performance optimization
        bool needsUpdateBuffers = false;                        // Flag indicating buffer resize requirements

        // Modern Vulkan rendering configuration
        vk::PipelineRenderingCreateInfo renderingInfo{};        // Dynamic rendering setup parameters
        vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb; // Target framebuffer format

        vk::raii::DescriptorSet sceneViewportDescriptorSet{ nullptr };
        bool isViewportHovered = false;
        bool isViewportFocused = false;
        glm::vec2 viewportSize = { 1280.0f, 720.0f };

        // Object Picking
        glm::vec2 viewportMin = { 0.0f, 0.0f };
        Entity* selectedEntity = nullptr;
    public:
        // Lifecycle management for proper resource initialization and cleanup
        ImGuiVulkanUtil(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
            vk::raii::Queue& graphicsQueue, uint32_t graphicsQueueFamily, VulkanCommandBuffer* commandBuffer, VulkanImage* image);
        ~ImGuiVulkanUtil();

        // Core functionality methods for ImGui integration
        void init(float width, float height);                   // Initialize ImGui context and configure display
        void initResources();                                   // Create all Vulkan resources for rendering
        void setStyle(uint32_t index);                          // Apply visual styling themes
        void updateTexture(ImTextureData* tex);                 // Dynamically update/create textures (v1.92+)

        // Frame-by-frame rendering operations
        bool newFrame(vk::Extent2D swapChainExtent);            // Begin new ImGui frame and generate geometry
        void updateBuffers(uint32_t frameIndex);                // Upload updated geometry to GPU buffers
        void drawFrame(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex, vk::Extent2D swapChainExtent); // Record rendering commands to command buffer
        void drawGUI();

        // Input event handling for interactive UI elements
        void handleKey(int key, int scancode, int action, int mods);    // Process keyboard input events
        void handleMousePos(float x, float y);                          // Process mouse movement events (v1.87+)
        void handleMouseButton(int button, bool pressed);               // Process mouse button events (v1.87+)
        bool getWantKeyCapture();                                       // Query if ImGui wants keyboard focus
        void charPressed(uint32_t key);                                 // Handle character input for text widgets

        inline vk::Sampler GetSampler() const { return *sampler; }

        void SetSceneViewportTexture(vk::ImageView sceneImageView);
        bool IsViewportHovered() const { return isViewportHovered; }
        bool IsViewportFocused() const { return isViewportFocused; }
        glm::vec2 GetViewportSize() const { return viewportSize; }

        // Object Picking
        glm::vec2 GetViewportMin() const { return viewportMin; }
        Entity* GetSelectedEntity() const { return selectedEntity; }
        void SetSelectedEntity(Entity* entity) { selectedEntity = entity; }

        bool showViewportOverlays = true;
        bool autoTurntable = false;
        float turntableSpeed = 20.0f;
    };
}