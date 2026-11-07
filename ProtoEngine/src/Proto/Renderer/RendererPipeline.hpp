#pragma once
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Renderer/Camera.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include "Core/ResourceTypes.hpp"
#include "Core/ServiceLocator.hpp"

namespace Proto
{
    // Explicit 64-byte aligned structure layout matching your Slang PBR shader exactly
    struct PushConstantBlock {
        glm::mat4 modelMatrix = glm::mat4(1.0f);     // 🟩 Crucial: Pass unique transforms directly to the hardware!
        glm::vec4 baseColorFactor = glm::vec4(1.0f); // float4 baseColorFactor
        glm::vec4 metallicRoughnessFactors = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f); // X = metallic, Y = roughness

        int baseColorTextureSet = -1; // Texture binding index for base color
        int physicalDescriptorTextureSet = -1; // Texture binding for metallic/roughness
        int normalTextureSet = -1; // Texture binding for normal maps
        int occlusionTextureSet = -1; // Texture binding for ambient occlusion
        int emissiveTextureSet = -1; // Texture binding for emissive maps

        float alphaMask = 0.0f; // Alpha masking enable flag
        float alphaMaskCutoff = 0.5f; // Alpha cutoff threshold
        float padding = 0.0f; // Explicit padding to keep 4-byte struct alignment clean
    };

    class CullingSystem {
    private:
        Camera* camera;
        std::vector<Entity*> visibleEntities;

    public:
        CullingSystem() : camera(nullptr) {}
        explicit CullingSystem(Camera* cam) : camera(cam) {}

        void SetCamera(Camera* cam) {
            camera = cam;
        }

        Camera* GetCamera() {
            return camera;
        }

        std::vector<Entity*> GetVisibleEntities() {
            return visibleEntities;
        }

        void CullScene(const std::vector<Entity*>& allEntities);
    };

    // A comprehensive rendergraph implementation for automated dependency management
    class Rendergraph {
    private:
        // Resource description and management structure Represents Image resource used during rendering (textures)
        struct Resource {
            std::string name;                     // Human-readable identifier for debugging and referencing
            vk::Format format;                    // Pixel format (RGBA8, Depth24Stencil8, etc.)
            vk::Extent2D extent;                  // Dimensions in pixels for 2D resources
            vk::ImageUsageFlags usage;            // How this resource will be used (color attachment, texture, etc.)
            vk::ImageLayout initialLayout;        // Expected layout when the frame begins
            vk::ImageLayout finalLayout;          // Required layout when the frame ends

            // Actual GPU resources - populated during compilation
            vk::raii::Image image = nullptr;            // The GPU image object
            vk::raii::DeviceMemory memory = nullptr;    // Backing memory allocation
            vk::raii::ImageView view = nullptr;         // Shader-accessible view of the image
        };

        // Render pass representation within the graph structure
        // Each pass represents a distinct rendering operation with defined inputs and outputs
        struct Pass {
            std::string name;                     // Descriptive name for debugging and profiling
            std::vector<std::string> inputs;      // Resources this pass reads from (dependencies)
            std::vector<std::string> outputs;     // Resources this pass writes to (products)
            std::function<void(vk::raii::CommandBuffer&)> executeFunc;  // The actual rendering code
        };

        // Core data storage for the rendergraph system
        std::unordered_map<std::string, Resource> resources;  // All resources referenced in the graph
        std::vector<Pass> passes;                             // All rendering passes in definition order
        std::vector<size_t> executionOrder;                   // Computed optimal execution sequence

        // Automatic synchronization management
        // These objects ensure correct GPU execution order without manual barriers
        std::vector<vk::raii::Semaphore> semaphores;          // GPU synchronization primitives
        std::vector<std::pair<size_t, size_t>> semaphoreSignalWaitPairs;  // (signaling pass, waiting pass)

        vk::raii::Device& device;  // Vulkan device for resource creation

    public:
        explicit Rendergraph(vk::raii::Device& dev) : device(dev) {}

        // Resource registration interface for declaring all resources used during rendering. This method establishes resource metadata without creating actual GPU resources
        void AddResource(const std::string& name, vk::Format format, vk::Extent2D extent,
            vk::ImageUsageFlags usage, vk::ImageLayout initialLayout,
            vk::ImageLayout finalLayout);

        // Pass registration interface for defining rendering operations and their dependencies. This method establishes the logical structure of rendering without immediate execution
        void AddPass(const std::string& name,
            const std::vector<std::string>& inputs,
            const std::vector<std::string>& outputs,
            std::function<void(vk::raii::CommandBuffer&)> executeFunc);

        // Rendergraph compilation - transforms declarative descriptions into executable pipeline. This method performs dependency analysis, resource allocation, and execution planning
        void Compile();

        // Resource access interface for retrieving compiled resources
        Resource* GetResource(const std::string& name) {
            auto it = resources.find(name);
            return (it != resources.end()) ? &it->second : nullptr;
        }

        // Rendergraph execution engine - coordinates pass execution with automatic synchronization. This method transforms the compiled rendergraph into actual GPU work
        void Execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue);
    private:
        //TODO: Implementation(Implemented elsewhere)
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
            // Implementation to find suitable memory type
            return 0; // Placeholder
        }

        // Comprehensive image layout transition implementation. This function demonstrates proper synchronization and layout management in Vulkan
        void TransitionImageLayout(vk::raii::CommandBuffer& commandBuffer,
            vk::Image image,
            vk::Format format,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout);

        // Comprehensive deferred renderer setup demonstrating rendergraph resource management
        // This implementation shows how to efficiently organize multi-pass rendering workflows
        void SetupDeferredRenderer(Rendergraph& graph, uint32_t width, uint32_t height);
    };

    // Render target class
    class RenderTarget {
    private:
        vk::raii::Image colorImage{ nullptr };
        vk::raii::DeviceMemory colorMemory{ nullptr };
        vk::raii::ImageView colorImageView{ nullptr };

        vk::raii::Image depthImage{ nullptr };
        vk::raii::DeviceMemory depthMemory{ nullptr };
        vk::raii::ImageView depthImageView{ nullptr };

        uint32_t width;
        uint32_t height;

    public:
        RenderTarget(uint32_t w, uint32_t h) : width(w), height(h) {
            // Create color and depth images
            CreateColorResources();
            CreateDepthResources();

            // Note: With dynamic rendering, we don't need to create VkRenderPass
            // or VkFramebuffer objects. Instead, we just create the images and
            // image views that will be used directly with vkCmdBeginRendering.
        }

        // No need for explicit destructor with RAII objects

        vk::ImageView GetColorImageView() const { return *colorImageView; }
        vk::ImageView GetDepthImageView() const { return *depthImageView; }

        uint32_t GetWidth() const { return width; }
        uint32_t GetHeight() const { return height; }

    private:
        void CreateColorResources();

        void CreateDepthResources();

        vk::raii::Device& GetDevice() {
            // Get device from somewhere (e.g., singleton or parameter)
            // ...
            static vk::raii::Device device = nullptr; // Placeholder
            return device;
        }
    };

    // Base render pass class
    class RenderPass {
    private:
        std::string name;
        std::vector<std::string> dependencies;
        RenderTarget* target = nullptr;
        bool enabled = true;

    public:
        explicit RenderPass(const std::string& passName) : name(passName) {}
        virtual ~RenderPass() = default;

        const std::string& GetName() const { return name; }

        void AddDependency(const std::string& dependency);

        const std::vector<std::string>& GetDependencies() const {
            return dependencies;
        }

        void SetRenderTarget(RenderTarget* renderTarget) {
            target = renderTarget;
        }

        RenderTarget* GetRenderTarget() const {
            return target;
        }

        void SetEnabled(bool isEnabled) {
            enabled = isEnabled;
        }

        bool IsEnabled() const {
            return enabled;
        }

        virtual void Execute(vk::raii::CommandBuffer& commandBuffer);

    protected:
        // With dynamic rendering, BeginPass typically calls vkCmdBeginRendering, instead of vkCmdBeginRenderPass
        virtual void BeginPass(vk::raii::CommandBuffer& commandBuffer) = 0;
        virtual void Render(vk::raii::CommandBuffer& commandBuffer) = 0;
        // With dynamic rendering, EndPass typically calls vkCmdEndRendering, instead of vkCmdEndRenderPass
        virtual void EndPass(vk::raii::CommandBuffer& commandBuffer) = 0;
    };

    // Render pass manager
    class RenderPassManager {
    private:
        std::unordered_map<std::string, std::unique_ptr<RenderPass>> renderPasses;
        std::vector<RenderPass*> sortedPasses;
        bool dirty = true;

    public:
        RenderPassManager() = default;

        template<typename T, typename... Args>
        T* AddRenderPass(const std::string& name, Args&&... args) {
            static_assert(std::is_base_of<RenderPass, T>::value, "T must derive from RenderPass");

            auto it = renderPasses.find(name);
            if (it != renderPasses.end()) {
                return dynamic_cast<T*>(it->second.get());
            }

            // --- THE MACRO-SAFE SYSTEM ALLOCATION FIX ---
            // Constructing the base unique_ptr directly eliminates the type upcast conversion 
            // trap inside the <memory> header template loops entirely!
            std::unique_ptr<RenderPass> pass(new T(name, std::forward<Args>(args)...));

            T* passPtr = dynamic_cast<T*>(pass.get());
            renderPasses[name] = std::move(pass);
            dirty = true;

            return passPtr;
        }

        RenderPass* GetRenderPass(const std::string& name);

        void RemoveRenderPass(const std::string& name);

        void Execute(vk::raii::CommandBuffer& commandBuffer);

    private:
        void SortPasses();

        void TopologicalSort(const std::string& name,
            const std::unordered_map<std::string, RenderPass*>& passMap,
            std::unordered_set<std::string>& visited, std::unordered_set<std::string>& visiting);
    };

    // Geometry pass for deferred rendering
    class GeometryPass : public RenderPass {
    private:
        CullingSystem* cullingSystem;

        // G-buffer textures
        RenderTarget* gBuffer;

    public:
        GeometryPass(const std::string& name, CullingSystem* culling);

        ~GeometryPass() override;

    protected:
        void BeginPass(vk::raii::CommandBuffer& commandBuffer) override;

        void Render(vk::raii::CommandBuffer& commandBuffer) override;

        void EndPass(vk::raii::CommandBuffer& commandBuffer) override;
    };

    class SceneRenderer; // Forward declaration for our friend visibility hook
    // Lighting pass for deferred rendering
    class LightingPass : public RenderPass {
    private:
        GeometryPass* geometryPass;
        SceneRenderer* sceneRenderer;
        //std::vector<Light*> lights;

    public:
        LightingPass(const std::string& name, GeometryPass* gPass, SceneRenderer* renderer);

        //void AddLight(Light* light) {
        //    lights.push_back(light);
        //}

        //void RemoveLight(Light* light) {
        //    auto it = std::find(lights.begin(), lights.end(), light);
        //    if (it != lights.end()) {
        //        lights.erase(it);
        //    }
        //}

    protected:
        virtual void BeginPass(vk::raii::CommandBuffer& commandBuffer) override;

        void Render(vk::raii::CommandBuffer& commandBuffer) override;

        void EndPass(vk::raii::CommandBuffer& commandBuffer) override;
    };

    // Post-process effect base class
    class PostProcessEffect {
    public:
        virtual ~PostProcessEffect() = default;
        virtual void Apply(vk::raii::CommandBuffer& commandBuffer) = 0;
    };

    // Post-processing pass
    class PostProcessPass : public RenderPass {
    private:
        LightingPass* lightingPass;
        std::vector<PostProcessEffect*> effects;

    public:
        PostProcessPass(const std::string& name, LightingPass* lPass);

        void AddEffect(PostProcessEffect* effect);

        void RemoveEffect(PostProcessEffect* effect);

    protected:
        void BeginPass(vk::raii::CommandBuffer& commandBuffer) override;
        void Render(vk::raii::CommandBuffer& commandBuffer) override;
        void EndPass(vk::raii::CommandBuffer& commandBuffer) override;
    };

    // Ensure our standard Uniform Buffer memory layout is explicitly defined
    struct PBRUniformBufferObject {
        //glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 lightPositions[4];
        glm::vec4 lightColors[4];
        glm::vec4 camPos;
        float exposure;
        float gamma;
        float prefilteredCubeMipLevels;
        float scaleIBLAmbient;
    };

    class LightingPass; // Forward declaration for our friend visibility hook

    class SceneRenderer {
        // GRANT THE LIGHTING PASS PERMISSION TO READ ALL PRIVATE FIELDS
        friend class LightingPass;
    private:
        // --- ADD THESE MISSING STRUCTURAL MEMBER VARIABLES ---
        vk::raii::Device device = nullptr; // Raw logical device context link
        vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
        vk::raii::PipelineLayout pbrPipelineLayout = nullptr;
        vk::raii::Pipeline pbrPipeline = nullptr;

        vk::Format swapChainImageFormat = vk::Format::eB8G8R8A8Srgb;
        vk::Extent2D swapChainExtent = vk::Extent2D{ 1280, 720 };

        // Buffer array structures tracker collections
        struct PBRBufferAllocation {
            vk::raii::Buffer buffer = nullptr;
            vk::raii::DeviceMemory memory = nullptr;
            void* mapped = nullptr;
        };
        std::vector<PBRBufferAllocation> uniformBuffers;

        vk::Queue graphicsQueue;
        vk::raii::CommandPool commandPool = nullptr;

        RenderPassManager renderPassManager;
        CullingSystem cullingSystem;

        // Current frame resources
        vk::raii::CommandBuffer commandBuffer = nullptr;
        vk::raii::Fence fence = nullptr;
        vk::raii::Semaphore imageAvailableSemaphore = nullptr;
        vk::raii::Semaphore renderFinishedSemaphore = nullptr;

        // Internal format utility fallback lookup
        vk::Format findDepthFormat();
    public:
        SceneRenderer() = default;
        SceneRenderer(vk::raii::Device& dev, vk::Queue queue);

        // No need for explicit destructor with RAII objects

        void SetCamera(Camera* camera) { cullingSystem.SetCamera(camera); }

        void Render(const std::vector<Entity*>& entities, vk::raii::CommandBuffer& activeCmdBuffer, uint32_t imageIndex);

        bool CreatePBRPipeline();
        void updateUniformBuffer(uint32_t currentFrame, Entity* entity, CameraComponent* camera, const glm::mat4& modelMatrix = glm::mat4(1.0f));
        void pushMaterialProperties(vk::CommandBuffer commandBuffer, const Model* model, uint32_t materialIndex, const glm::mat4& globalMatrix);
        void RenderModelNode(Node* node, const glm::mat4& parentMatrix, vk::raii::CommandBuffer& cmd, const Model* model, CameraComponent* camera);
        inline vk::DescriptorSetLayout GetPBRDescriptorSetLayout() const { return *descriptorSetLayout; }
    private:
        void SetupRenderPasses();
    };

}