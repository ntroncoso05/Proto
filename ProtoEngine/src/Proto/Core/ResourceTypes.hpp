#pragma once

#include "ResourceManager.hpp"
#include "Platform/Vulkan/VK_Buffer.hpp"
#include "Platform/Vulkan/VK_ImageView.hpp"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <format> // Required for std::format
#include <filesystem>
#include <iostream>

#include <ktx.h>
#include <tiny_gltf.h>

namespace Proto
{
    struct Node;
    class Mesh;
    class Texture;

    // Texture resource
    class Texture : public Resource {
    public:
        int width = 0;
        int height = 0;
        int channels = 0;
        ktxTexture* pixels = nullptr;

        explicit Texture(const std::string& id);
        virtual ~Texture() override;

        virtual bool doLoad() override;
        virtual void doUnload() override;

        const vk::raii::Image& GetImage() const { return vkData.image; }
        const vk::raii::ImageView& GetImageView() const { return vkData.imageView; }
        const vk::raii::Sampler& GetSampler() const { return vkData.sampler; }

    private:
        struct VulkanGpuData {
            vk::raii::Image image{ nullptr };
            vk::raii::DeviceMemory memory{ nullptr };
            vk::raii::ImageView imageView{ nullptr };
            vk::raii::Sampler sampler{ nullptr };
        } vkData;

        ktxTexture* LoadImageData(const std::string& filePath, int* w, int* h, int* ch);
    };

    // Mesh resource
    class Mesh : public Resource {
    private:
        vk::raii::Buffer vertexBuffer = nullptr;
        vk::raii::DeviceMemory vertexBufferMemory = nullptr;
        uint32_t vertexCount = 0;

        vk::raii::Buffer indexBuffer = nullptr;
        vk::raii::DeviceMemory indexBufferMemory = nullptr;
        uint32_t indexCount = 0;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

    public:
        bool isSubmesh = false;

        explicit Mesh(const std::string& id);
        virtual ~Mesh() override;

        virtual bool doLoad() override;
        virtual void doUnload() override;

        inline const vk::raii::Buffer& GetVertexBuffer() const { return vertexBuffer; }
        inline const vk::raii::Buffer& GetIndexBuffer()  const { return indexBuffer; }
        uint32_t GetIndexCount() const { return indexCount; }

        std::vector<Vertex>& GetRawVertices() { return vertices; }
        std::vector<uint32_t>& GetRawIndices() { return indices; }
    private:
        bool LoadMeshData(const std::string& filePath);
    };

    enum class AlphaMode {
        OPAQUE_MODE,
        MASK,
        BLEND
    };

    struct Material {
        std::string name;
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        glm::vec3 emissiveFactor = glm::vec3(0.0f);

        int baseColorTextureIndex = -1;
        int metallicRoughnessTextureIndex = -1;
        int normalTextureIndex = -1;
        int occlusionTextureIndex = -1;
        int emissiveTextureIndex = -1;

        AlphaMode alphaMode = AlphaMode::OPAQUE_MODE;
        float alphaCutoff = 0.5f;

        // FIXED: Use the move-only RAII wrapper to prevent automatic destruction!
        vk::raii::DescriptorSet descriptorSet{ nullptr };
    };

    // Structure for animation keyframes
    struct AnimationChannel {
        enum PathType { TRANSLATION, ROTATION, SCALE };
        PathType path;
        Node* node = nullptr;
        uint32_t samplerIndex;
    };

    // Structure for animation interpolation
    struct AnimationSampler {
        enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
        InterpolationType interpolation;
        std::vector<float> inputs;  // Key frame timestamps
        std::vector<glm::vec4> outputsVec4;  // Key frame values (for rotations)
        std::vector<glm::vec3> outputsVec3;  // Key frame values (for translations and scales)
    };

    // Structure for animation
    struct Animation {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
        float start = std::numeric_limits<float>::max();
        float end = std::numeric_limits<float>::min();
        float currentTime = 0.0f;
    };

    struct Node {
        std::string name;
        uint32_t index = 0;          // Fixed E0135: missing index tracking variable
        int materialIndex = -1;      // Fixed E0135: missing material tracking variable
        Node* parent = nullptr;
        std::vector<Node*> children;

        Mesh* mesh = nullptr;        // Using Mesh pointer instance layout cleanly
        std::string meshResourceId;

        glm::mat4 matrix = glm::mat4(1.0f);
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);

        glm::mat4 getLocalMatrix();
        glm::mat4 getGlobalMatrix();
    };

#ifdef interface
    #undef interface
#endif
    // --- Unified Model Resource ---
    class Model : public Resource {
    public:
        std::vector<Node*> nodes;               // Scene Graph Root Nodes
        std::vector<Node*> linearNodes;         // Flat lookup array for clean destructor iteration
        std::vector<Material> materials;        // Shared Material Blocks
        std::vector<Animation> animations;      // Asset Animation Timelines
        std::vector<std::string> textureIds;    // Unique Resource IDs for textures owned by this model        
        std::vector<std::string> textureResourceIds;

        explicit Model(const std::string& id) : Resource(id) {}
        virtual ~Model() override;

        virtual bool doLoad() override;
        virtual void doUnload() override;

        Node* findNode(const std::string& name);
        void updateAnimation(uint32_t index, float deltaTime);
    };

    // Shader resource
    class Shader : public Resource {
    private:
        vk::ShaderModule shaderModule;
        vk::ShaderStageFlagBits stage;

    public:
        Shader(const std::string& id, vk::ShaderStageFlagBits shaderStage)
            : Resource(id), stage(shaderStage) {}

        ~Shader() override {
            Unload();
        }

        bool doLoad() override;

        void doUnload() override;

        // Getters for Vulkan resources
        vk::ShaderModule GetShaderModule() const { return shaderModule; }
        vk::ShaderStageFlagBits GetStage() const { return stage; }

    private:
        bool ReadFile(const std::string& filePath, std::vector<char>& buffer);

        void CreateShaderModule(const std::vector<char>& code);

        vk::Device GetDevice();
    };

}