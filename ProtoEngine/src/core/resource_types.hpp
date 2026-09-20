#pragma once

#include "resource_manager.hpp"
#include "buffer.hpp"
//#include "vulkan/vulkan_image.hpp"

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <format> // Required for std::format

//#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define SHADER_BINARY_DIR "C:/Dev/Proto/build/ProtoEngine"

//#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace Proto
{
    // Texture resource
    class Texture : public Resource {
    private:
        //VulkanImage* vulkanImage;
        // Core Vulkan GPU resources for texture representation
        vk::raii::Image image = nullptr;              // GPU image object containing pixel data
        vk::raii::DeviceMemory memory = nullptr;      // GPU memory allocation backing the image
        //vk::DeviceSize offset;                        // Offset within the memory allocation for this texture
        vk::raii::ImageView imageView = nullptr;      // Shader-accessible view into the image
        vk::raii::Sampler sampler = nullptr;          // Sampling configuration (filtering, wrapping, etc.)           

    public:
        // Texture metadata for validation and debugging
        int width = 0;                // Image width in pixels
        int height = 0;               // Image height in pixels
        int channels = 0;             // Number of color channels (RGB=3, RGBA=4, etc.)
        unsigned char* pixels = nullptr;

        explicit Texture(const std::string& id) : Resource(id) {}

        ~Texture() override {
            Unload();                 // Ensure proper cleanup when object is destroyed
        }

        bool doLoad() override {
            // Step 2a: Construct file path using resource ID and expected format
            std::string filePath = std::format("{}/assets/textures/{}.png", SHADER_BINARY_DIR, GetId());
            //std::string filePath = "textures/" + GetId() + ".png";

            // Step 2b: Load raw image data from disk with format detection
            unsigned char* data = LoadImageData(filePath, &width, &height, &channels);
            if (!data) {
                return false;           // Failed to load - return failure without partial state
            }
            pixels = data;
            // Step 2c: Transform raw pixel data into Vulkan GPU resources
            //CreateVulkanImage(data, width, height, channels);

            // Step 2d: Clean up temporary CPU memory to prevent leaks
            FreeImageData(data);

            return true; //Resource::Load();    // Mark resource as successfully loaded
        }

        void doUnload() override {
            // Only perform cleanup if resource is currently loaded
            if (IsLoaded()) {
                // Step 3a: Obtain device handle for resource destruction
                //vk::Device device = GetDevice();

                // Step 3b: Destroy GPU objects in reverse creation order
                // This ordering prevents use-after-free errors in GPU drivers
                //device.destroySampler(sampler);       // Destroy sampling configuration
                //device.destroyImageView(imageView);   // Destroy shader view
                //device.destroyImage(image);           // Destroy image object
                //device.freeMemory(memory);            // Release GPU memory allocation

                // Step 3c: Update base class state to reflect unloaded status
                Resource::Unload();
            }
        }

        // Public interface for accessing Vulkan resources safely
        vk::Image GetImage() const { return image; }
        vk::ImageView GetImageView() const { return imageView; }
        vk::Sampler GetSampler() const { return sampler; }
    private:
        unsigned char* LoadImageData(const std::string& filePath, int* width, int* height, int* channels) {
            // Implementation using stb_image or ktx library
            // This method abstracts the details of different image format support
            // and provides a consistent interface for pixel data loading
            // ...
            stbi_uc* pixels = stbi_load(filePath.c_str(), width, height, channels, STBI_rgb_alpha);
            return pixels;
        }

        void FreeImageData(unsigned char* data) {
            // Implementation using stb_image or ktx library
            // Ensures proper cleanup of image loader specific memory allocations
            // Different libraries may require different cleanup approaches
            // ...
            //stbi_image_free(&data);
        }

        void CreateVulkanImage(unsigned char* data, int width, int height, int channels) {
            // Implementation to create Vulkan image, allocate memory, and upload data
            // This involves complex Vulkan operations including:
            // - Format selection based on channel count and data type
            // - Memory allocation with appropriate usage flags
            // - Image creation with optimal tiling and layout
            // - Data upload via staging buffers for efficiency
            // - Image view creation for shader access
            // - Sampler creation with appropriate filtering settings
            // ...
            //vulkanImage->createTextureImage(data, width, height, channels);
        }

        vk::Device GetDevice() {
            // Get device from somewhere (e.g., singleton or parameter)
            // Production code would use dependency injection or service location
            // to provide the Vulkan device handle without tight coupling
            // ...
            return vk::Device(); // Placeholder
        }
    };

    // Mesh resource
    class Mesh : public Resource {
    private:
        // Vertex data management - stores per-vertex attributes like position, normal, UV coordinates
        vk::raii::Buffer vertexBuffer = nullptr;                // GPU buffer containing vertex attribute data
        vk::raii::DeviceMemory vertexBufferMemory = nullptr;    // GPU memory backing the vertex buffer
        //vk::DeviceSize vertexBufferOffset;      // Offset within the memory allocation for vertex buffer
        uint32_t vertexCount = 0;               // Number of vertices in this mesh

        // Index data management - defines triangle connectivity using vertex indices
        vk::raii::Buffer indexBuffer = nullptr;                 // GPU buffer containing triangle index data
        vk::raii::DeviceMemory indexBufferMemory = nullptr;     // GPU memory backing the index buffer
        //vk::DeviceSize indexBufferOffset;       // Offset within the memory allocation for index buffer
        uint32_t indexCount = 0;                // Number of indices in this mesh (typically 3 per triangle)

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

    public:
        explicit Mesh(const std::string& id) : Resource(id) {}

        ~Mesh() override {
            Unload();                           // Ensure GPU resources are cleaned up
        }

        bool doLoad() override {
            // Step 2a: Construct file path using standardized naming convention
            //std::string filePath = "models/" + GetId() + ".gltf";
            std::string filePath = std::format("{}/assets/models/{}.obj", SHADER_BINARY_DIR, GetId());

            // Step 2b: Parse geometric data from file format into CPU-accessible structures
            //std::vector<Vertex> vertices;      // Temporary CPU storage for vertex attributes
            //std::vector<uint32_t> indices;     // Temporary CPU storage for triangle indices
            if (!LoadMeshData(filePath/*, vertices, indices*/)) {
                return false;                   // Failed to parse file - abort loading
            }

            // Step 2c: Transform CPU data into optimized GPU buffer resources
            //CreateVertexBuffer(vertices);       // Upload vertex attributes to GPU
            //CreateIndexBuffer(indices);         // Upload triangle connectivity to GPU

            // Step 2d: Cache metadata for efficient rendering operations
            vertexCount = static_cast<uint32_t>(vertices.size());
            indexCount = static_cast<uint32_t>(indices.size());

            return true; //Resource::Load();            // Mark resource as successfully loaded
        }

        void doUnload() override {
            // Only proceed with cleanup if resources are currently loaded
            if (IsLoaded()) {
                // Phase 3a: Obtain device handle for resource destruction
                //vk::Device device = GetDevice();

                // Phase 3b: Destroy buffers and free GPU memory in proper sequence
                // Index resources cleaned up first to maintain clear dependency order
                //device.destroyBuffer(indexBuffer);         // Destroy index buffer object
                //device.freeMemory(indexBufferMemory);      // Release index buffer memory

                // Vertex resources cleaned up second
                //device.destroyBuffer(vertexBuffer);        // Destroy vertex buffer object
                //device.freeMemory(vertexBufferMemory);     // Release vertex buffer memory

                // Phase 3c: Update base class state to reflect unloaded condition
                Resource::Unload();
            }
        }

        // Public interface for safe access to GPU resources and metadata
        vk::raii::Buffer GetVertexBuffer() { return std::move(vertexBuffer); }
        vk::raii::DeviceMemory GetVertexBufferMemory() { return std::move(vertexBufferMemory); }
        void SetVertexBuffer(vk::raii::Buffer buffer) { vertexBuffer = std::move(buffer); }
        void SetVertexBufferMemory(vk::raii::DeviceMemory bufferMemory) { vertexBufferMemory = std::move(bufferMemory); }

        vk::raii::Buffer GetIndexBuffer() { return std::move(indexBuffer); }
        vk::raii::DeviceMemory GetIndexBufferMemory() { return std::move(indexBufferMemory); }

        std::vector<Vertex> GetVertices() const { return vertices; }
        std::vector<uint32_t> GetIndices() const { return indices; }
        uint32_t GetVertexCount() const { return vertexCount; }
        uint32_t GetIndexCount() const { return indexCount; }
    private:
        bool LoadMeshData(const std::string& filePath/*, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices*/) {
            // Implementation using tinygltf or similar library
            // This method handles the complex task of:
            // - Opening and validating the mesh file format
            // - Parsing vertex attributes (positions, normals, UVs, etc.)
            // - Extracting index data that defines triangle connectivity
            // - Converting from file format to engine-specific vertex structures
            // - Performing validation to ensure data integrity
            // ...
            tinyobj::attrib_t                attrib;
            std::vector<tinyobj::shape_t>    shapes;
            std::vector<tinyobj::material_t> materials;
            std::string                      warn, err;

            if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filePath.c_str()))
            {
                throw std::runtime_error(warn + err);
            }

            std::unordered_map<Vertex, uint32_t> uniqueVertices{};

            for (const auto& shape : shapes)
            {
                for (const auto& index : shape.mesh.indices)
                {
                    Vertex vertex{};

                    vertex.pos = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2] };

                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1] };

                    vertex.color = { 1.0f, 1.0f, 1.0f };

#if 1
                    auto [it, inserted] = uniqueVertices.insert({ vertex, static_cast<uint32_t>(vertices.size()) });
                    if (inserted)
                    {
                        vertices.push_back(vertex);
                    }

                    indices.push_back(it->second);
#else
                    vertices.push_back(vertex);
                    indices.push_back(static_cast<uint32_t>(indices.size()));
#endif
                }
            }
            return true; // Placeholder
        }

        void CreateVertexBuffer(const std::vector<Vertex>& vertices) {
            // Implementation to create Vulkan buffer, allocate memory, and upload data
            // This involves several complex Vulkan operations:
            // - Calculating buffer size requirements based on vertex count and structure
            // - Creating buffer with appropriate usage flags (vertex buffer usage)
            // - Allocating GPU memory with optimal memory type selection
            // - Uploading data via staging buffer for efficient transfer
            // - Setting up memory barriers to ensure data availability
            // ...
        }

        void CreateIndexBuffer(const std::vector<uint32_t>& indices) {
            // Implementation to create Vulkan buffer, allocate memory, and upload data
            // Similar to vertex buffer creation but optimized for index data:
            // - Buffer creation with index buffer specific usage flags
            // - Memory allocation optimized for read-heavy access patterns
            // - Efficient data transfer using appropriate staging mechanisms
            // - Index format validation (16-bit vs 32-bit indices)
            // ...
        }

        vk::Device GetDevice() {
            // Get device from somewhere (e.g., singleton or parameter)
            // Production implementations typically use dependency injection
            // to avoid tight coupling between resource classes and core engine systems
            // ...
            return vk::Device(); // Placeholder
        }
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

        bool doLoad() override {
            // Determine file extension based on shader stage
            std::string extension;
            switch (stage) {
            case vk::ShaderStageFlagBits::eVertex: extension = ".vert"; break;
            case vk::ShaderStageFlagBits::eFragment: extension = ".frag"; break;
            case vk::ShaderStageFlagBits::eCompute: extension = ".comp"; break;
            default: return false;
            }

            // Load shader from file
            std::string filePath = "shaders/" + GetId() + extension + ".spv";

            // Read shader code
            std::vector<char> shaderCode;
            if (!ReadFile(filePath, shaderCode)) {
                return false;
            }

            // Create shader module
            CreateShaderModule(shaderCode);

            return Resource::Load();
        }

        void doUnload() override {
            // Destroy Vulkan resources
            if (IsLoaded()) {
                // Get device from somewhere (e.g., singleton or parameter)
                vk::Device device = GetDevice();

                device.destroyShaderModule(shaderModule);

                Resource::Unload();
            }
        }

        // Getters for Vulkan resources
        vk::ShaderModule GetShaderModule() const { return shaderModule; }
        vk::ShaderStageFlagBits GetStage() const { return stage; }

    private:
        bool ReadFile(const std::string& filePath, std::vector<char>& buffer) {
            // Implementation to read binary file
            // ...
            return true; // Placeholder
        }

        void CreateShaderModule(const std::vector<char>& code) {
            // Implementation to create Vulkan shader module
            // ...
        }

        vk::Device GetDevice() {
            // Get device from somewhere (e.g., singleton or parameter)
            // ...
            return vk::Device(); // Placeholder
        }
    };
}