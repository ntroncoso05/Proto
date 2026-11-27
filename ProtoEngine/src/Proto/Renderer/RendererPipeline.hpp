#pragma once
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Core/ResourceTypes.hpp"
#include "Core/ServiceLocator.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"

namespace Proto
{
    // Explicit 64-byte aligned structure layout matching your Slang PBR shader exactly
    struct PushConstantBlock {
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        uint32_t  materialIndex = 0;
        uint32_t  _pad[3] = { 0, 0, 0 };
    };

    struct GPUMaterialData {
        glm::vec4 baseColorFactor{ 1.0f };
        glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 1.0f };
        glm::vec4 coatAndIridescence{ 0.0f, 0.04f, 0.0f, 1.5f };
        glm::vec4 metallicRoughnessAlpha{ 0.0f, 0.5f, 0.0f, 0.5f };
        glm::ivec4 textureIndices0{ -1, -1, -1, -1 }; // baseColor, metallicRoughness, normal, occlusion
        glm::ivec4 textureIndices1{ -1, -1, -1, -1 }; // emissive, reserved...
    };

    class CullingSystem {
    private:
        CameraComponent* camera = nullptr;
        std::vector<Entity*> visibleEntities;

    public:
        CullingSystem() : camera(nullptr) {}
        explicit CullingSystem(CameraComponent* cam) : camera(cam) {}

        void SetCamera(CameraComponent* cam) {
            camera = cam;
        }

        CameraComponent* GetCamera() {
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
            std::function<void(vk::raii::CommandBuffer&, uint32_t)> executeFunc;  // The actual rendering code
        };

        // Core data storage for the rendergraph system
        std::unordered_map<std::string, Resource> resources;  // All resources referenced in the graph
        std::vector<Pass> passes;                             // All rendering passes in definition order
        std::vector<size_t> executionOrder;                   // Computed optimal execution sequence

        vk::raii::Device& device;  // Vulkan device for resource creation

    public:
        const Pass* GetPass(const std::string& passName) const
        {
            for (const auto& pass : passes) {
                if (pass.name == passName) {
                    return &pass;
                }
            }
            return nullptr;
        }

        explicit Rendergraph(vk::raii::Device& dev) : device(dev) {}

        // Resource registration interface for declaring all resources used during rendering. This method establishes resource metadata without creating actual GPU resources
        void AddResource(const std::string& name, vk::Format format, vk::Extent2D extent,
            vk::ImageUsageFlags usage, vk::ImageLayout initialLayout,
            vk::ImageLayout finalLayout);

        // Pass registration interface for defining rendering operations and their dependencies. This method establishes the logical structure of rendering without immediate execution
        void AddPass(const std::string& name,
            const std::vector<std::string>& inputs,
            const std::vector<std::string>& outputs,
            std::function<void(vk::raii::CommandBuffer&, uint32_t)> executeFunc);

        // Rendergraph compilation - transforms declarative descriptions into executable pipeline. This method performs dependency analysis, resource allocation, and execution planning
        void Compile();

        // Resource access interface for retrieving compiled resources
        Resource* GetResource(const std::string& name) {
            auto it = resources.find(name);
            return (it != resources.end()) ? &it->second : nullptr;
        }

        // Rendergraph execution engine - coordinates pass execution with automatic synchronization. This method transforms the compiled rendergraph into actual GPU work
        void Execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue, uint32_t imageIndex);

        // Comprehensive deferred renderer setup demonstrating rendergraph resource management
        // This implementation shows how to efficiently organize multi-pass rendering workflows
        void SetupDeferredRenderer(Rendergraph& graph, uint32_t width, uint32_t height);
    private:
        //TODO: Implementation(Implemented elsewhere)
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

        // Comprehensive image layout transition implementation. This function demonstrates proper synchronization and layout management in Vulkan
        void TransitionImageLayout(vk::raii::CommandBuffer& commandBuffer,
            vk::Image image,
            vk::Format format,
            vk::ImageLayout oldLayout,
            vk::ImageLayout newLayout);
    };

    // Render target class
    class RenderTarget {
    private:
        // Position MRT Target
        vk::raii::Image        positionImage{ nullptr };
        vk::raii::DeviceMemory positionMemory{ nullptr };
        vk::raii::ImageView    positionImageView{ nullptr };

        // Normal MRT Target
        vk::raii::Image        normalImage{ nullptr };
        vk::raii::DeviceMemory normalMemory{ nullptr };
        vk::raii::ImageView    normalImageView{ nullptr };

        // Albedo Target (Standard gamma format)
        vk::raii::Image        albedoImage{ nullptr };
        vk::raii::DeviceMemory albedoMemory{ nullptr };
        vk::raii::ImageView    albedoImageView{ nullptr };

        vk::raii::Image        depthImage{ nullptr };
        vk::raii::DeviceMemory depthMemory{ nullptr };
        vk::raii::ImageView    depthImageView{ nullptr };

        uint32_t width;
        uint32_t height;

        vk::raii::Device& m_ContextDevice; // Keep a persistent reference to your valid hardware context!
    public:
        RenderTarget(uint32_t w, uint32_t h, vk::raii::Device& dev);

        // No need for explicit destructor with RAII objects
        // Updated getters to expose all attachments cleanly to your passes
        vk::Image GetPositionImage() const { return *positionImage; }
        vk::Image GetNormalImage() const { return *normalImage; }
        vk::Image GetAlbedoImage() const { return *albedoImage; }
        vk::Image GetDepthImage() const { return *depthImage; }

        vk::ImageView GetPositionImageView() const { return *positionImageView; }
        vk::ImageView GetNormalImageView() const { return *normalImageView; }
        vk::ImageView GetAlbedoImageView() const { return *albedoImageView; }
        vk::ImageView GetDepthImageView() const { return *depthImageView; }

        uint32_t GetWidth() const { return width; }
        uint32_t GetHeight() const { return height; }

    private:
        void CreateColorResources();
        void CreateDepthResources();

        vk::raii::Device& GetDevice() { return m_ContextDevice; }
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

        virtual void Execute(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);

    protected:
        // With dynamic rendering, BeginPass typically calls vkCmdBeginRendering, instead of vkCmdBeginRenderPass
        virtual void BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) = 0;
        virtual void Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) = 0;
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

        void Execute(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);

    private:
        void SortPasses();

        void TopologicalSort(const std::string& name,
            const std::unordered_map<std::string, RenderPass*>& passMap,
            std::unordered_set<std::string>& visited, std::unordered_set<std::string>& visiting);
    };

    class SceneRenderer;
    // Geometry pass for deferred rendering
    class GeometryPass : public RenderPass {
    private:
        CullingSystem* cullingSystem;
        // G-buffer textures
        RenderTarget* gBuffer;
        SceneRenderer* sceneRenderer; // Added tracking pointer reference context!
    public:
        GeometryPass(const std::string& name, CullingSystem* culling, SceneRenderer* renderer);
        ~GeometryPass() override;
        inline RenderTarget* GetGBufferTarget() const { return gBuffer; }
    protected:
        void BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
        void Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
        void EndPass(vk::raii::CommandBuffer& commandBuffer) override;
    };

    class SceneRenderer; // Forward declaration for our friend visibility hook

    // Lighting pass for deferred rendering
    class LightingPass : public RenderPass {
    private:
        GeometryPass* geometryPass;
        SceneRenderer* sceneRenderer;

    public:
        LightingPass(const std::string& name, GeometryPass* gPass, SceneRenderer* renderer);

    protected:
        virtual void BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
        void Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
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
        void BeginPass(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
        void Render(vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) override;
        void EndPass(vk::raii::CommandBuffer& commandBuffer) override;
    };

    // Ensure our standard Uniform Buffer memory layout is explicitly defined
    struct PBRUniformBufferObject {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 lightSpaceMatrices[4];
        glm::ivec4 lightShadowEnabled;
        glm::vec4 lightPositions[4];   // xyz = pos (or dir if w==0), w = type (0=Dir, 1=Point, 2=Spot)
        glm::vec4 lightColors[4];      // rgb = color * intensity, a = radius
        glm::vec4 lightDirections[4];  // xyz = spot forward dir, w = cos(outerCone)
        glm::vec4 lightConeParams[4];  // x = cos(innerCone), y = cos(outerCone)
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
        friend class GeometryPass;
    private:
        // --- ADD THESE MISSING STRUCTURAL MEMBER VARIABLES ---
        vk::raii::Device& device; // Raw logical device context link
        vk::raii::DescriptorSetLayout descriptorSetLayout{ nullptr };
        vk::raii::PipelineLayout pbrPipelineLayout{ nullptr };
        vk::raii::Pipeline pbrPipeline{ nullptr };

        vk::Format swapChainImageFormat = vk::Format::eB8G8R8A8Srgb;
        vk::Format gBufferColorFormat = vk::Format::eR16G16B16A16Sfloat; // Keep synchronized!
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

        // Tracking sizes to handle window resizing cleanly
        uint32_t m_DepthWidth = 0;
        uint32_t m_DepthHeight = 0;

        // Deferred Composition Pipeline resources (for LightingPass)
        vk::raii::PipelineLayout m_DeferredPipelineLayout{ nullptr };
        vk::raii::Pipeline       m_DeferredPipeline{ nullptr };
        vk::raii::DescriptorSetLayout m_DeferredSetLayout{ nullptr };

        vk::raii::Pipeline m_TransparentPipeline{ nullptr };
    public:
        struct ShadowMapPushConstants {
            glm::mat4 lightSpaceMatrix;
            glm::mat4 modelMatrix;
        };

        struct DeferredShadowPushConstants {
            glm::mat4 lightSpaceMatrix;
            int shadowEnabled;
        };

        struct BloomPushConstants {
            glm::vec2 direction;
            float threshold;
            float bloomIntensity;
        };

        static constexpr uint32_t MAX_PARTICLES = 4096;

        struct ParticleData {
            glm::vec4 position;
            glm::vec4 velocity;
            glm::vec4 color;
        };

        struct ParticlePushConstants {
            glm::vec4 emitterPos;   // xyz = world spawn origin, w = deltaTime
            glm::vec4 emitterDir;   // xyz = world emit direction, w = spread
            glm::vec4 particleCol;  // rgb = color * HDRIntensity, a = gravity
            glm::vec4 lifeAndSize;  // x = minLife, y = maxLife, z = initialSpeed, w = particleSize
            glm::vec4 camRight;     // xyz = camera right, w = elapsedTime
            glm::vec4 camUp;        // xyz = camera up, w = activeParticleCount
        }; // 6 * 16 = 96 bytes (safely under 128 bytes!)

        vk::raii::DescriptorSet m_DeferredGBufferSet{ nullptr };

        vk::raii::PipelineLayout m_ShadowPipelineLayout{ nullptr };
        vk::raii::Pipeline m_ShadowPipeline{ nullptr };

        float m_EmissiveStrength = 1.0f;
        float m_BloomIntensity = 0.15f;
        float m_IBLIntensity = 0.25f;

        struct SSRPushConstants {
            float maxDistance = 25.0f;
            float stepSize = 0.35f;
            float thickness = 0.45f;
            float ssrIntensity = 0.85f;
        };

        float m_SSRIntensity = 0.85f;
        float m_SSRMaxDistance = 25.0f;
        float m_SSRThickness = 0.22f;

        vk::raii::DescriptorSetLayout m_BloomSetLayout{ nullptr };
        vk::raii::PipelineLayout m_BloomPipelineLayout{ nullptr };
        vk::raii::Pipeline m_BloomExtractPipeline{ nullptr };
        vk::raii::Pipeline m_BloomBlurPipeline{ nullptr };
        vk::raii::Pipeline m_BloomCompositePipeline{ nullptr };

        vk::raii::DescriptorSet m_BloomExtractSet{ nullptr };   // Binding 0: HDRColor
        vk::raii::DescriptorSet m_BloomBlurHorizSet{ nullptr }; // Binding 0: BloomPing
        vk::raii::DescriptorSet m_BloomBlurVertSet{ nullptr };  // Binding 0: BloomPong
        vk::raii::DescriptorSet m_BloomCompositeSet{ nullptr }; // Binding 0: HDRColor, Binding 1: BloomPing

        std::shared_ptr<Texture> m_EnvCubemap{ nullptr };
        std::shared_ptr<Texture> m_BrdfLutTex{ nullptr };
        std::string m_ActiveHDRIPath = "";

        vk::raii::DescriptorSetLayout m_SSAOSetLayout{ nullptr };
        vk::raii::PipelineLayout m_SSAOPipelineLayout{ nullptr };
        vk::raii::Pipeline m_SSAOPipeline{ nullptr };
        vk::raii::Pipeline m_SSAOBlurPipeline{ nullptr };

        vk::raii::DescriptorSet m_SSAOSet{ nullptr };     // Binding 0: GBuffer_Position, Binding 1: GBuffer_Normal
        vk::raii::DescriptorSet m_SSAOBlurSet{ nullptr }; // Binding 0: SSAO_Raw, Binding 1: SSAO_Raw

        vk::raii::Buffer               m_ParticleBuffer{ nullptr };
        vk::raii::DeviceMemory         m_ParticleMemory{ nullptr };
        vk::raii::DescriptorSetLayout  m_ParticleSetLayout{ nullptr };
        vk::raii::DescriptorPool       m_ParticleDescriptorPool{ nullptr };
        vk::raii::DescriptorSet        m_ParticleSet{ nullptr };
        vk::raii::PipelineLayout       m_ParticlePipelineLayout{ nullptr };
        vk::raii::Pipeline             m_ParticleComputePipeline{ nullptr };
        vk::raii::Pipeline             m_ParticleRenderPipeline{ nullptr };

        vk::raii::DescriptorSetLayout m_SSRSetLayout{ nullptr };
        vk::raii::PipelineLayout      m_SSRPipelineLayout{ nullptr };
        vk::raii::Pipeline            m_SSRPipeline{ nullptr };
        vk::raii::DescriptorSet       m_SSRSet{ nullptr };

        ParticlePushConstants m_LastParticlePC{};
        bool m_HasActiveParticles = false;

        SSRPushConstants m_SSRPC;

        struct FogPushConstants {
            glm::mat4 invViewProj{ 1.0f };
            glm::vec4 fogColorAndDensity{ 0.75f, 0.82f, 0.92f, 0.015f }; // rgb = tint, a = density
            glm::vec4 scatterAndHeight{ 1.8f, 0.18f, -1.0f, 65.0f };    // x = sunScatter, y = heightFalloff, z = baseHeight, w = maxDist
        };

        glm::vec3 m_FogColor = glm::vec3(0.75f, 0.82f, 0.92f);
        float     m_FogDensity = 0.015f; // Set to 0.0f to disable
        float     m_FogSunScatter = 1.8f;
        float     m_FogHeightFalloff = 0.18f;
        float     m_FogBaseHeight = -1.0f;
        float     m_FogMaxDistance = 65.0f;

        vk::raii::DescriptorSetLayout m_FogSetLayout{ nullptr };
        vk::raii::PipelineLayout      m_FogPipelineLayout{ nullptr };
        vk::raii::Pipeline            m_FogPipeline{ nullptr };
        vk::raii::DescriptorSet       m_FogSet{ nullptr };

        enum class GridUnit : int {
            Millimeters = 0, // 0.001 m, 10 subdiv (1 cm major)
            Centimeters = 1, // 0.01 m,  10 subdiv (10 cm major)
            Meters = 2, // 1.0 m,   10 subdiv (10 m major)
            Inches = 3, // 0.0254 m, 12 subdiv (1 ft major)
            Feet = 4  // 0.3048 m, 10 subdiv (10 ft major)
        };

        struct GridPushConstants {
            glm::mat4 invViewProj{ 1.0f };
            glm::mat4 viewProj{ 1.0f };
            glm::vec4 camPosAndPlaneY{ 0.0f, 3.0f, 10.0f, -1.0f }; // w = -1.0f aligns with GroundPlane!
            glm::vec4 unitParams{ 1.0f, 10.0f, 80.0f, 0.75f };
            glm::vec4 minorColor{ 0.55f, 0.58f, 0.62f, 0.30f };
            glm::vec4 majorColor{ 0.80f, 0.84f, 0.90f, 0.60f };
        };

        bool     m_ShowGrid = true;
        GridUnit m_GridUnit = GridUnit::Meters;
        float    m_GridPlaneY = -0.99f; // Slightly above GroundPlane (-1.0f) to prevent z-fighting
        float    m_GridFadeDistance = 80.0f;
        float    m_GridOpacity = 0.85f;
        bool     m_SnapToGrid = false;

        float GetGridUnitScaleMeters() const {
            switch (m_GridUnit) {
            case GridUnit::Millimeters: return 0.001f;
            case GridUnit::Centimeters: return 0.01f;
            case GridUnit::Meters:      return 1.0f;
            case GridUnit::Inches:      return 0.0254f;
            case GridUnit::Feet:        return 0.3048f;
            }
            return 1.0f;
        }

        float GetGridSubdivisions() const {
            return (m_GridUnit == GridUnit::Inches) ? 12.0f : 10.0f;
        }

        vk::raii::PipelineLayout m_GridPipelineLayout{ nullptr };
        vk::raii::Pipeline       m_GridPipeline{ nullptr };

        // Bindless
        static constexpr uint32_t MAX_BINDLESS_TEXTURES = 1024;
        static constexpr uint32_t MAX_GPU_MATERIALS = 2048;

        vk::raii::DescriptorPool      m_BindlessDescriptorPool{ nullptr };
        vk::raii::DescriptorSet       m_BindlessSet{ nullptr };
        vk::raii::Buffer              m_MaterialSSBOBuffer{ nullptr };
        vk::raii::DeviceMemory        m_MaterialSSBOMemory{ nullptr };
        void* m_MaterialSSBOMapped = nullptr;

        std::unordered_map<std::string, int32_t> m_TextureBindlessIndexMap;
        uint32_t                                 m_NextBindlessTextureSlot = 0;
        std::vector<GPUMaterialData>             m_FrameMaterials;
        std::unordered_map<uint64_t, uint32_t>   m_MaterialSlotCache;

        void InitBindlessResources();
        int32_t RegisterBindlessTexture(const std::string& textureId);
        uint32_t UploadEntityMaterial(Entity* ownerEntity, const Model* model, uint32_t localMatIdx);
    public:
        SceneRenderer() = default;
        SceneRenderer(vk::raii::Device& dev, vk::Queue queue);

        // No need for explicit destructor with RAII objects
        void SetCamera(CameraComponent* camera) { cullingSystem.SetCamera(camera); }
        void Render(const std::vector<Entity*>& entities, vk::raii::CommandBuffer& activeCmdBuffer, uint32_t imageIndex);

        bool CreatePBRPipeline();
        void updateUniformBuffer(uint32_t currentFrame, Entity* entity, CameraComponent* camera, const glm::mat4& modelMatrix = glm::mat4(1.0f));
        void RenderModelNode(Node* node, const glm::mat4& parentMatrix, vk::raii::CommandBuffer& cmd, const Model* model, CameraComponent* camera, bool renderTransparentOnly = false, Entity* ownerEntity = nullptr);
        inline vk::DescriptorSetLayout GetPBRDescriptorSetLayout() const { return *descriptorSetLayout; }

        // Public getter hook so LightingPass can bind it
        vk::Pipeline GetDeferredPipeline() const { return *m_DeferredPipeline; }
        vk::PipelineLayout GetDeferredPipelineLayout() const { return *m_DeferredPipelineLayout; }

        vk::DescriptorSetLayout GetDeferredSetLayout() const { return *m_DeferredSetLayout; }

        bool CreateDeferredLightingPipeline();

        bool CreateShadowPipeline();
        void ComputeShadowAtlasMatrices(glm::mat4 outMatrices[4], glm::ivec4& outShadowEnabled) const;
        void RenderShadowPass(vk::raii::CommandBuffer& cmd, const glm::mat4 lightSpaceMatrices[4], const glm::ivec4& shadowEnabled);
        glm::mat4 ComputeLightSpaceMatrix(bool& outShadowEnabled) const;

        bool CreateTransparentPipeline();
        void RenderTransparentPass(const std::vector<Entity*>& entities, vk::raii::CommandBuffer& activeCmdBuffer);

        bool CreateBloomPipelines();
        void ResetRendergraphDescriptorSets() {
            m_DeferredGBufferSet = nullptr;
            m_BloomExtractSet = nullptr;
            m_BloomBlurHorizSet = nullptr;
            m_BloomBlurVertSet = nullptr;
            m_BloomCompositeSet = nullptr;
            m_SSAOSet = nullptr;
            m_SSAOBlurSet = nullptr;
            m_SSRSet = nullptr;
            m_FogSet = nullptr;
        }

        bool CreateSSAOPipelines();

        bool CreateParticlePipelines();
        void DispatchAndRenderParticles(vk::raii::CommandBuffer& cmd, float deltaTime);

        void SetEnvironmentHDRI(const std::string& path);

        bool CreateSSRPipeline();

        bool CreateFogPipeline();

        bool CreateGridPipeline();
        void RenderGridPass(vk::raii::CommandBuffer& cmd);
    private:
        void SetupRenderPasses();
        // Internal format utility fallback lookup
        vk::Format findDepthFormat();
    };

}