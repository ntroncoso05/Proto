#include "protopch.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ResourceTypes.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp" // Now safely visible here!
#include "Platform/Vulkan/VK_BufferUtils.hpp"
#include "Renderer/RendererPipeline.hpp"
#include "Core/ServiceLocator.hpp"

#include <filesystem>
#include <format>
#include <iostream>
#include <glm/gtx/quaternion.hpp> 
#include <glm/gtc/type_ptr.hpp>

namespace Proto
{
    // NODE
    glm::mat4 Node::getLocalMatrix()
    {
        return glm::translate(glm::mat4(1.0f), translation) *
            glm::toMat4(rotation) *
            glm::scale(glm::mat4(1.0f), scale) *
            matrix;
    }

    glm::mat4 Node::getGlobalMatrix()
    {
        glm::mat4 m = getLocalMatrix();
        Node* p = parent;
        while (p) {
            m = p->getLocalMatrix() * m;
            p = p->parent;
        }
        return m;
    }

    // MODEL
    Model::~Model()
    {
        for (auto node : linearNodes) {
            delete node;
        }
        Unload();
        doUnload();
    }

    // 🟩 Explicit Virtual Unload Definition
    void Model::doUnload()
    {
        auto& resManager = ServiceLocator::Get<ResourceManager>();

        // Clean up heap-allocated structural transform graph nodes safely
        for (auto node : linearNodes) {
            if (node) {
                // If the node holds a unique geometry handle, unload it from memory
                if (node->mesh) {
                    node->mesh->Unload();
                    delete node->mesh;
                }
                delete node;
            }
        }

        // Decrement dependency references inside the master registry cache loops
        for (const auto& texId : textureResourceIds) {
            resManager.Release<Texture>(texId);
        }

        // Flush all tracking containers to baseline defaults
        linearNodes.clear();
        nodes.clear();
        materials.clear();
        textureResourceIds.clear();
    }

    Node* Model::findNode(const std::string& name)
    {
        auto nodeIt = std::ranges::find_if(linearNodes, [&name](auto const& node) {
            return node->name == name;
            });
        return (nodeIt != linearNodes.end()) ? *nodeIt : nullptr;
    }

    void Model::updateAnimation(uint32_t index, float deltaTime)
    {
        assert(!animations.empty() && index < animations.size());

        Animation& animation = animations[index];
        animation.currentTime += deltaTime;
        if (animation.currentTime > animation.end) {
            animation.currentTime = animation.start;
        }

        for (auto& channel : animation.channels) {
            AnimationSampler& sampler = animation.samplers[channel.samplerIndex];

            // Find the current key frame using binary search
            auto keyFrameIt = std::ranges::lower_bound(sampler.inputs, animation.currentTime);
            if (keyFrameIt != sampler.inputs.end() && keyFrameIt != sampler.inputs.begin()) {
                size_t i = std::distance(sampler.inputs.begin(), keyFrameIt) - 1;
                float t = (animation.currentTime - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);

                switch (channel.path) {
                    case AnimationChannel::TRANSLATION: {
                        glm::vec3 start = sampler.outputsVec3[i];
                        glm::vec3 end = sampler.outputsVec3[i + 1];
                        channel.node->translation = glm::mix(start, end, t);
                        break;
                    }
                    case AnimationChannel::ROTATION: {
                        glm::quat start = glm::quat(sampler.outputsVec4[i].w, sampler.outputsVec4[i].x, sampler.outputsVec4[i].y, sampler.outputsVec4[i].z);
                        glm::quat end = glm::quat(sampler.outputsVec4[i + 1].w, sampler.outputsVec4[i + 1].x, sampler.outputsVec4[i + 1].y, sampler.outputsVec4[i + 1].z);
                        channel.node->rotation = glm::slerp(start, end, t);
                        break;
                    }
                    case AnimationChannel::SCALE: {
                        glm::vec3 start = sampler.outputsVec3[i];
                        glm::vec3 end = sampler.outputsVec3[i + 1];
                        channel.node->scale = glm::mix(start, end, t);
                        break;
                    }
                }
                break;
            }
        }
    }

    bool Model::doLoad()
    {
        std::string filePath = std::format("assets/models/{}.gltf", GetId());
        if (!std::filesystem::exists(filePath)) {
            if (std::filesystem::exists("../" + filePath)) filePath = "../" + filePath;
            else if (std::filesystem::exists("../../" + filePath)) filePath = "../../" + filePath;
            else if (std::filesystem::exists("../../../" + filePath)) filePath = "../../../" + filePath;
            else {
                std::cerr << "[ERROR]: glTF File layout not found: " << filePath << std::endl;
                return false;
            }
        }

        tinygltf::Model gltfModel;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        // FIXED 1: EXTRACT EXPLICIT BASE DIRECTORY STRING
        std::filesystem::path pathObject(filePath);
        std::string baseDir = pathObject.parent_path().string() + "/";

        // FIXED 2: BIND THE BASE DIRECTORY DIRECTLY TO THE LOADER PROPERTIES
        // This ensures TinyGLTF finds your external companion .bin data chunk!
        loader.SetImageWriter(nullptr, nullptr); // Resets standard handler extensions

        // FIXED 3: CALL THE CORRECT FUNCTION OVERLOAD WITH AN INT (NOT A STRING!)
        // Passing 0 tells TinyGLTF to use its standard parsing mode without stripping assets.
        bool ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath, 0);

        if (!warn.empty()) std::cout << "[WARN]: glTF: " << warn << std::endl;
        if (!err.empty())  std::cerr << "[ERROR]: glTF: " << err << std::endl;
        if (!ret) return false;

        std::cout << "[ENGINE DEBUG]: glTF Loaded Successfully!" << std::endl;
        std::cout << " -> Meshes count: " << gltfModel.meshes.size() << std::endl;
        std::cout << " -> Images count: " << gltfModel.images.size() << std::endl;
        std::cout << " -> Materials count: " << gltfModel.materials.size() << std::endl;

        auto& resManager = ServiceLocator::Get<ResourceManager>();

        // 🟩 FIXED 3: MAP TEXTURE STRINGS FROM THE METADATA VECTOR INDICES
        textureResourceIds.clear();
        if (!gltfModel.images.empty()) {
            textureResourceIds.resize(gltfModel.images.size());
            for (size_t i = 0; i < gltfModel.images.size(); i++) {
                const auto& image = gltfModel.images[i];
                if (!image.uri.empty()) {
                    std::string externalFilename = image.uri;
                    size_t lastDot = externalFilename.find_last_of(".");
                    std::string genericId = (lastDot != std::string::npos) ? externalFilename.substr(0, lastDot) : externalFilename;

                    textureResourceIds[i] = genericId;
                }
                else {
                    textureResourceIds[i] = "viking_room";
                }
            }
        }
        else {
            // Force tracking indices to look up the startup texture key name 
            textureResourceIds.push_back("viking_room");
        }

        // 2. PARSE MATERIALS
        materials.clear();
        for (const auto& gltfMat : gltfModel.materials) {
            Material mat{};
            mat.name = gltfMat.name;
            auto color = gltfMat.pbrMetallicRoughness.baseColorFactor;
            mat.baseColorFactor = glm::vec4(color[0], color[1], color[2], color[3]);
            mat.metallicFactor = static_cast<float>(gltfMat.pbrMetallicRoughness.metallicFactor);
            mat.roughnessFactor = static_cast<float>(gltfMat.pbrMetallicRoughness.roughnessFactor);

            // 🟩 FIXED 4: INDICES GUARANTEED ASSIGNMENT FALLBACK
            if (gltfModel.images.empty()) {
                mat.baseColorTextureIndex = 0;
                mat.metallicRoughnessTextureIndex = -1;
                mat.normalTextureIndex = -1;
                mat.occlusionTextureIndex = -1;
                mat.emissiveTextureIndex = -1;
            }
            else {
                mat.baseColorTextureIndex = gltfMat.pbrMetallicRoughness.baseColorTexture.index;
                mat.metallicRoughnessTextureIndex = gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index;
                mat.normalTextureIndex = gltfMat.normalTexture.index;
                mat.occlusionTextureIndex = gltfMat.occlusionTexture.index;
                mat.emissiveTextureIndex = gltfMat.emissiveTexture.index;
            }

            mat.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
            materials.push_back(std::move(mat));
        }

        // 3. GENERATE STRUCTURAL SCENE NODES
        linearNodes.resize(gltfModel.nodes.size());
        for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
            const auto& node = gltfModel.nodes[i];
            Node* newNode = new Node();
            newNode->name = node.name;
            newNode->index = static_cast<uint32_t>(i);

            if (node.translation.size() == 3) newNode->translation = glm::make_vec3(node.translation.data());
            if (node.rotation.size() == 4)    newNode->rotation = glm::make_quat(node.rotation.data());
            if (node.scale.size() == 3)       newNode->scale = glm::make_vec3(node.scale.data());
            if (node.matrix.size() == 16)     newNode->matrix = glm::make_mat4(node.matrix.data());

            linearNodes[i] = newNode;
        }

        for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
            const auto& node = gltfModel.nodes[i];
            for (int childIdx : node.children) {
                linearNodes[childIdx]->parent = linearNodes[i];
                linearNodes[i]->children.push_back(linearNodes[childIdx]);
            }
        }

        const tinygltf::Scene& defaultScene = gltfModel.scenes[gltfModel.defaultScene >= 0 ? gltfModel.defaultScene : 0];
        for (int nodeIdx : defaultScene.nodes) {
            nodes.push_back(linearNodes[nodeIdx]);
        }

        // 4. PACK VERTEX AND INDEX BUFFER DATA FROM THE LOADED CHUNK
        for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
            const auto& node = gltfModel.nodes[i];
            if (node.mesh < 0) continue;

            const auto& gltfMesh = gltfModel.meshes[node.mesh];
            if (gltfMesh.primitives.empty()) continue;

            const auto& primitive = gltfMesh.primitives[0];
            std::string submeshId = std::format("{}_submesh_node_{}", GetId(), i);

            Mesh* meshAsset = new Mesh(submeshId);
            meshAsset->isSubmesh = true;

            auto& indices = meshAsset->GetRawIndices();
            auto& vertices = meshAsset->GetRawVertices();

            if (primitive.indices >= 0) {
                const auto& accessor = gltfModel.accessors[primitive.indices];
                const auto& view = gltfModel.bufferViews[accessor.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                const uint8_t* ptr = buf.data.data() + view.byteOffset + accessor.byteOffset;

                indices.resize(accessor.count);
                if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                    memcpy(indices.data(), ptr, accessor.count * sizeof(uint32_t));
                }
                else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                    const uint16_t* src = reinterpret_cast<const uint16_t*>(ptr);
                    for (size_t k = 0; k < accessor.count; ++k) indices[k] = src[k];
                }
            }

            auto posIt = primitive.attributes.find("POSITION");
            if (posIt != primitive.attributes.end()) {
                const auto& accessor = gltfModel.accessors[posIt->second];
                const auto& view = gltfModel.bufferViews[accessor.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                const float* rawPos = reinterpret_cast<const float*>(buf.data.data() + view.byteOffset + accessor.byteOffset);

                vertices.resize(accessor.count);
                for (size_t v = 0; v < accessor.count; ++v) {
                    vertices[v].pos = glm::vec3(rawPos[v * 3 + 0], rawPos[v * 3 + 1], rawPos[v * 3 + 2]);
                    vertices[v].color = glm::vec3(1.0f);
                }
            }

            auto uvIt = primitive.attributes.find("TEXCOORD_0");
            if (uvIt != primitive.attributes.end()) {
                const auto& accessor = gltfModel.accessors[uvIt->second];
                const auto& view = gltfModel.bufferViews[accessor.bufferView];
                const auto& buf = gltfModel.buffers[view.buffer];
                const float* rawUv = reinterpret_cast<const float*>(buf.data.data() + view.byteOffset + accessor.byteOffset);

                for (size_t v = 0; v < accessor.count; ++v) {
                    vertices[v].texCoord = glm::vec2(rawUv[v * 2 + 0], rawUv[v * 2 + 1]);
                }
            }

            if (meshAsset->doLoad()) {
                struct ResourceAccessor : public Resource { static void Confirm(Resource* r) { r->loaded = true; } };
                ResourceAccessor::Confirm(meshAsset);
                linearNodes[i]->mesh = meshAsset;
                //FIXED 1: CONSTRAIN THE MATERIAL INDEX LOOKUP RANGE SAFE TO CHANNELS SIZE
                // This guarantees your submesh nodes always align precisely with the 
                // successfully allocated and warmed material descriptor set array bounds!
                int parsedMaterialIdx = primitive.material;
                if (parsedMaterialIdx < 0 || parsedMaterialIdx >= static_cast<int>(materials.size())) {
                    linearNodes[i]->materialIndex = 0; // Fallback directly to our primary base color slot
                }
                else {
                    linearNodes[i]->materialIndex = static_cast<uint32_t>(parsedMaterialIdx);
                }
            }
            else {
                delete meshAsset;
            }
        }
        return true;
    }

    // --- TEXTURE LIFECYCLE MECHANICS ---
    Texture::Texture(const std::string& id) : Resource(id) {}
    Texture::~Texture() { Unload(); }

	bool Texture::doLoad()
    {
        // Step 2a: Construct file path using resource ID and expected format
        // 1. Build a root-relative path layout string cleanly
        std::string filePath = std::format("assets/textures/{}.ktx2", GetId());

        // 2. Comprehensive local parent directory traversal checks
        if (!std::filesystem::exists(filePath)) {
            if (std::filesystem::exists("../" + filePath)) filePath = "../" + filePath;
            else if (std::filesystem::exists("../../" + filePath)) filePath = "../../" + filePath;
            else if (std::filesystem::exists("../../../" + filePath)) filePath = "../../../" + filePath;
            else {
                // Log a descriptive runtime warning instead of passing an invalid string to KTX
                std::cout << "[ERROR]: Texture path not found anywhere: " << filePath << std::endl;
                return false; // Exit gracefully; do not pass to LoadImageData
            }
        }

        try {
            pixels = LoadImageData(filePath, &width, &height, &channels);
        }
        catch (const std::exception& e) {
            std::cout << "[EXCEPTION]: " << e.what() << std::endl;
            return false;
        }

        if (!pixels) {
            std::cout << "[ERROR]: ktxTexture allocation returned nullptr for file: " << filePath << std::endl;
            return false;
        }

        // Extract properties directly from the allocated KTX engine container fields
        this->width = pixels->baseWidth;
        this->height = pixels->baseHeight;
        // KTX format tracking does not map to explicit channels, but you can assume 4 (RGBA) for Srgb targets
        this->channels = 4;

        // Double check that values are completely sane before passing to Vulkan
        if (this->width == 0 || this->height == 0) {
            std::cout << "[ERROR]: KTX2 Texture contains invalid 0x0 base dimensions!" << std::endl;
            return false;
        }

        // --- REAL KTX2 VULKAN GPU UPLOAD ---
        auto& renderer = VulkanRenderer::Get();
        auto& customVulkanDevice = renderer.vulkanContext->GetDevice(); // Pull out the custom engine class wrapper reference
        auto* imgUtil = renderer.GetVulkanImageSubsystem();
        auto* cmdUtil = renderer.GetCommandBuffer();

        // Query dimensions and total footprint size straight from the loaded KTX object container
        // total memory footprint across all combined mip-levels to back your staging allocations
        vk::DeviceSize imageSize = pixels->dataSize;
        vk::Format imageFormat = vk::Format::eR8G8B8A8Srgb; // Map to standard layout configuration

        // Allocate host-visible staging block elements
        auto [stagingBuf, stagingBufMem] = cmdUtil->CreateBuffer(
            imageSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        void* mappedData = stagingBufMem.mapMemory(0, imageSize);
        memcpy(mappedData, ktxTexture_GetData(pixels), imageSize);
        stagingBufMem.unmapMemory();

        // Request the stateless VulkanImage component factory to generate allocations
        auto [gpuImage, gpuMemory] = imgUtil->CreateImage(
            width, height, 1,
            vk::SampleCountFlagBits::e1,
            imageFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        vkData.image = std::move(gpuImage);
        vkData.memory = std::move(gpuMemory);

        // Transition layouts and copy bytes onto the GPU
        vk::raii::CommandBuffer texCmd = cmdUtil->BeginSingleTimeCommands();
        imgUtil->TransitionImageLayout(texCmd, vkData.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
        BufferUtils::CopyBufferToImage(texCmd, stagingBuf, vkData.image, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        imgUtil->TransitionImageLayout(texCmd, vkData.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);
        cmdUtil->EndSingleTimeCommands(std::move(texCmd));

        // Generate ImageView wrappers via your helper components
        vkData.imageView = imgUtil->CreateImageView(*vkData.image, imageFormat, vk::ImageAspectFlagBits::eColor, 1);
        auto& logicalDevice = renderer.GetLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.anisotropyEnable = vk::False;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = vk::False;

        // Allocate texture specific sampler cleanly
        vkData.sampler = vk::raii::Sampler(logicalDevice, samplerInfo);

        // Clean up temporary host system file buffers cleanly
        ktxTexture_Destroy(pixels);
        pixels = nullptr;

        return true;
    }

    void Texture::doUnload()
    {
        // Explicitly clear our inner RAII block structure
        vkData = {};
    }

    ktxTexture* Texture::LoadImageData(const std::string& filePath, int* w, int* h, int* ch)
    {
        // Load KTX2 texture instead of using stb_image
        ktxTexture* kTexture;
        KTX_error_code result = ktxTexture_CreateFromNamedFile(
            filePath.c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &kTexture);

        if (result != KTX_SUCCESS) {
            throw std::runtime_error("failed to load ktx texture image!");
        }

        return kTexture;
    }
    
    // --- MESH LIFECYCLE MECHANICS ---
    Mesh::Mesh(const std::string& id) : Resource(id) {}
    Mesh::~Mesh() { Unload(); }  // Ensure GPU resources are cleaned up    

    bool Mesh::doLoad()
    {
        if (IsLoaded()) return true;

        auto& renderer = VulkanRenderer::Get();
        auto* cmdBufferUtil = renderer.GetCommandBuffer();

        vk::DeviceSize vertexBufferSize = vertices.size() * sizeof(Vertex);
        vk::DeviceSize indexBufferSize = indices.size() * sizeof(uint32_t);

        auto stagingVertAllocation = cmdBufferUtil->CreateBuffer(
            vertexBufferSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        auto stagingIdxAllocation = cmdBufferUtil->CreateBuffer(
            indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        void* vertData = stagingVertAllocation.second.mapMemory(0, vertexBufferSize);
        memcpy(vertData, vertices.data(), vertexBufferSize);
        stagingVertAllocation.second.unmapMemory();

        void* idxData = stagingIdxAllocation.second.mapMemory(0, indexBufferSize);
        memcpy(idxData, indices.data(), indexBufferSize);
        stagingIdxAllocation.second.unmapMemory();

        auto vertexAlloc = cmdBufferUtil->CreateBuffer(
            vertexBufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        vertexBuffer = std::move(vertexAlloc.first);
        vertexBufferMemory = std::move(vertexAlloc.second);

        auto indexAlloc = cmdBufferUtil->CreateBuffer(
            indexBufferSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        indexBuffer = std::move(indexAlloc.first);
        indexBufferMemory = std::move(indexAlloc.second);

        vk::raii::CommandBuffer copyCmd = cmdBufferUtil->BeginSingleTimeCommands();
        vk::BufferCopy copyRegionVert{ .srcOffset = 0, .dstOffset = 0, .size = vertexBufferSize };
        copyCmd.copyBuffer(*stagingVertAllocation.first, *vertexBuffer, copyRegionVert);

        vk::BufferCopy copyRegionIdx{ .srcOffset = 0, .dstOffset = 0, .size = indexBufferSize };
        copyCmd.copyBuffer(*stagingIdxAllocation.first, *indexBuffer, copyRegionIdx);
        cmdBufferUtil->EndSingleTimeCommands(std::move(copyCmd));

        vertexCount = static_cast<uint32_t>(vertices.size());
        indexCount = static_cast<uint32_t>(indices.size());
        return true;  //Resource::Load();            // Mark resource as successfully loaded
    }

    void Mesh::doUnload()
    {
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
            //Resource::Unload();
        }
    }
    bool Shader::doLoad()
    {
        // Determine file extension based on shader stage
        std::string extension;
        switch (stage)
        {
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

    void Shader::doUnload()
    {
        // Destroy Vulkan resources
        if (IsLoaded()) {
            // Get device from somewhere (e.g., singleton or parameter)
            vk::Device device = GetDevice();

            device.destroyShaderModule(shaderModule);

            Resource::Unload();
        }
    }

    bool Shader::ReadFile(const std::string& filePath, std::vector<char>& buffer)
    {
        // Implementation to read binary file
        // ...
        return true; // Placeholder
    }

    void Shader::CreateShaderModule(const std::vector<char>& code)
    {
        // Implementation to create Vulkan shader module
        // ...
    }

    vk::Device Shader::GetDevice()
    {
        // Get device from somewhere (e.g., singleton or parameter)
        // ...
        return vk::Device(); // Placeholder
    }

}