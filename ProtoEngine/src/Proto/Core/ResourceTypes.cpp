#include "protopch.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "ResourceTypes.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include "Platform/Vulkan/VK_BufferUtils.hpp"
#include "Renderer/RendererPipeline.hpp"
#include "Core/ServiceLocator.hpp"
#include "Core/AssetIO.hpp"
#include "Core/Log.hpp"
#include <ShaderConfig.hpp>

#include <filesystem>
#include <format>
#include <iostream>
#include <glm/gtx/quaternion.hpp> 
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/packing.hpp>

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
        Unload();
    }

    // Explicit Virtual Unload Definition
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

    void Model::updateAnimation(uint32_t index, float deltaTime, bool loop)
    {
        if (animations.empty() || index >= animations.size()) return;

        Animation& animation = animations[index];
        animation.currentTime += deltaTime;
        if (animation.currentTime > animation.end) {
            animation.currentTime = loop ? animation.start : animation.end;
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
            }
        }
    }

    std::shared_ptr<Model> Model::CreateGroundPlane(const std::string& id, float halfExtent, float uvTiling)
    {
        auto model = std::make_shared<Model>(id);

        // 1. Default matte-concrete PBR material
        Material mat{};
        mat.name = "GroundMaterial";
        mat.baseColorFactor = glm::vec4(0.06f, 0.065f, 0.075f, 1.0f);
        mat.metallicFactor = 0.25f;
        mat.roughnessFactor = 0.12f;
        mat.baseColorTextureIndex = 0; // Falls back to "default_white" in SceneRenderer
        model->textureResourceIds.push_back("default_white");
        model->materials.push_back(std::move(mat));

        // 2. Build upward-facing (+Y) quad mesh
        Mesh* planeMesh = new Mesh(id + "_mesh");
        planeMesh->isSubmesh = true;

        auto& verts = planeMesh->GetRawVertices();
        auto& idxs = planeMesh->GetRawIndices();

        verts = {
            { { -halfExtent, 0.0f,  halfExtent },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f,     0.0f } },
            { { halfExtent, 0.0f,  halfExtent },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ uvTiling, 0.0f } },
            { { halfExtent, 0.0f, -halfExtent },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ uvTiling, uvTiling } },
            { { -halfExtent, 0.0f, -halfExtent },{ 0.0f, 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f,     uvTiling } }
        };
        idxs = { 0, 1, 2, 2, 3, 0 };

        if (planeMesh->doLoad()) {
            struct ResourceAccessor : public Resource { static void Confirm(Resource* r) { r->loaded = true; } };
            ResourceAccessor::Confirm(planeMesh);
            ResourceAccessor::Confirm(model.get());
        }
        else {
            delete planeMesh;
            return nullptr;
        }

        // 3. Attach to root scene graph node
        Node* rootNode = new Node();
        rootNode->name = "GroundPlaneNode";
        rootNode->index = 0;
        rootNode->mesh = planeMesh;
        rootNode->materialIndex = 0;

        model->linearNodes.push_back(rootNode);
        model->nodes.push_back(rootNode);

        return model;
    }

    bool Model::doLoad()
    {
        std::string filePath;
        bool isBinary = false;
        const std::vector<std::string> searchPrefixes = { "", "../", "../../", "../../../" };
                
        tinygltf::Model gltfModel;
        tinygltf::TinyGLTF loader;
        std::string err, warn;

        loader.SetImageWriter(nullptr, nullptr); // Resets standard handler extensions
        
        bool ret = false;

#ifdef PROTO_PLATFORM_ANDROID
        std::string modelBaseDir;
        tinygltf::FsCallbacks fsCallbacks{};
        fsCallbacks.user_data = &modelBaseDir;
        fsCallbacks.FileExists = [](const std::string& abs_filename, void* user_data) -> bool {
            if (AssetIO::FileExists(abs_filename)) return true;
            auto* dir = static_cast<std::string*>(user_data);
            return (dir && !dir->empty()) ? AssetIO::FileExists(*dir + "/" + abs_filename) : false;
            };
        fsCallbacks.ExpandFilePath = [](const std::string& filepath, void*) -> std::string {
            return filepath;
            };
        fsCallbacks.ReadWholeFile = [](std::vector<unsigned char>* out, std::string* err,
            const std::string& filepath, void* user_data) -> bool {
                auto bytes = AssetIO::ReadBinaryFile(filepath);
                if (bytes.empty()) {
                    auto* dir = static_cast<std::string*>(user_data);
                    if (dir && !dir->empty()) {
                        bytes = AssetIO::ReadBinaryFile(*dir + "/" + filepath);
                    }
                }
                if (bytes.empty()) {
                    if (err) *err += "Failed to read Android asset: " + filepath + "\n";
                    return false;
                }
                out->assign(reinterpret_cast<const unsigned char*>(bytes.data()),
                    reinterpret_cast<const unsigned char*>(bytes.data()) + bytes.size());
                return true;
            };
        fsCallbacks.WriteWholeFile = [](std::string*, const std::string&,
            const std::vector<unsigned char>&, void*) -> bool {
                return false;
            };
        loader.SetFsCallbacks(fsCallbacks);

        std::string registeredPath = ServiceLocator::Get<ResourceManager>().GetAssetPath(GetId());
        std::vector<std::string> candidates = {
            registeredPath,
            std::format("assets/models/{}.glb", GetId()),
            std::format("assets/models/{}.gltf", GetId()),
            std::format("assets/models/{}/{}.glb", GetId(), GetId()),
            std::format("assets/models/{}/{}.gltf", GetId(), GetId()),
            std::format("assets/models/{}/scene.gltf", GetId())
        };

        for (const auto& c : candidates) {
            if (!c.empty() && AssetIO::FileExists(c)) {
                filePath = c;
                modelBaseDir = std::filesystem::path(filePath).parent_path().generic_string();
                isBinary = (std::filesystem::path(filePath).extension() == ".glb");
                break;
            }
        }

        if (filePath.empty()) {
            //std::cerr << "[ERROR]: Android Model (.glb or .gltf) not found for ID: " << GetId() << std::endl;
            PROTO_CORE_ERROR("Android Model not found for ID: {}", GetId());
            return false;
        }

        auto modelBytes = AssetIO::ReadBinaryFile(filePath);
        if (modelBytes.empty()) {
            PROTO_CORE_ERROR("Failed to read model bytes from Android asset: {}", filePath);
            return false;
        }

        if (isBinary) {
            ret = loader.LoadBinaryFromMemory(
                &gltfModel, &err, &warn,
                reinterpret_cast<const unsigned char*>(modelBytes.data()),
                static_cast<unsigned int>(modelBytes.size()),
                modelBaseDir, 0);
        }
        else {
            ret = loader.LoadASCIIFromString(
                &gltfModel, &err, &warn,
                modelBytes.data(),
                static_cast<unsigned int>(modelBytes.size()),
                modelBaseDir, 0);
        }
#else
        // 1. Check for .glb first, then fallback to .gltf
        for (const auto& prefix : searchPrefixes) {
            std::string glbCandidate = std::format("{}assets/models/{}.glb", prefix, GetId());
            if (std::filesystem::exists(glbCandidate)) {
                filePath = glbCandidate;
                isBinary = true;
                break;
            }

            std::string gltfCandidate = std::format("{}assets/models/{}.gltf", prefix, GetId());
            if (std::filesystem::exists(gltfCandidate)) {
                filePath = gltfCandidate;
                isBinary = false;
                break;
            }
        }

        std::string registeredPath = ServiceLocator::Get<ResourceManager>().GetAssetPath(GetId());
        if (!registeredPath.empty() && std::filesystem::exists(registeredPath)) {
            filePath = registeredPath;
            isBinary = (std::filesystem::path(filePath).extension() == ".glb");
        }

        if (filePath.empty()) {
            PROTO_CORE_ERROR("Android Model not found for ID: {}", GetId());
            return false;
        }

        if (isBinary) {
            ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filePath, 0);
        }
        else {
            ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filePath, 0);
        }
#endif

        if (!warn.empty()) PROTO_CORE_WARN("glTF load error for '{}': {}", GetId(), warn);
        if (!err.empty()) PROTO_CORE_ERROR("glTF load error for '{}': {}", GetId(), err);
        if (!ret) return false;

        std::cout << "[ENGINE DEBUG]: glTF Loaded Successfully!" << std::endl;
        std::cout << " -> Meshes count: " << gltfModel.meshes.size() << std::endl;
        std::cout << " -> Images count: " << gltfModel.images.size() << std::endl;
        std::cout << " -> Materials count: " << gltfModel.materials.size() << std::endl;

        auto& resManager = ServiceLocator::Get<ResourceManager>();

        // 1. Determine which glTF image indices require sRGB (only baseColor and emissive)
        std::vector<bool> imageIsSRGB(gltfModel.images.size(), false);
        auto markSRGB = [&](int gltfTexIdx)
            {
                if (gltfTexIdx >= 0 && gltfTexIdx < static_cast<int>(gltfModel.textures.size())) {
                    int imgIdx = gltfModel.textures[gltfTexIdx].source;
                    if (imgIdx >= 0 && imgIdx < static_cast<int>(imageIsSRGB.size())) {
                        imageIsSRGB[imgIdx] = true;
                    }
                }
            };

        for (const auto& gltfMat : gltfModel.materials) {
            markSRGB(gltfMat.pbrMetallicRoughness.baseColorTexture.index);
            markSRGB(gltfMat.emissiveTexture.index);
        }

        textureResourceIds.clear();
        if (!gltfModel.images.empty()) {
            textureResourceIds.resize(gltfModel.images.size());
            for (size_t i = 0; i < gltfModel.images.size(); i++) {
                const auto& image = gltfModel.images[i];

                if (!image.uri.empty()) {
                    // External texture file (standard .gltf workflow)
                    std::string externalFilename = image.uri;
                    size_t lastDot = externalFilename.find_last_of(".");
                    std::string genericId = (lastDot != std::string::npos) ? externalFilename.substr(0, lastDot) : externalFilename;

                    textureResourceIds[i] = genericId;
                    resManager.Load<Texture>(genericId); // Ensure refCount is incremented for Model::doUnload()
                }
                else if (!image.image.empty() && image.width > 0 && image.height > 0) {
                    std::string embeddedId = std::format("{}_embedded_tex_{}", GetId(), i);
                    auto embeddedTex = std::make_shared<Texture>(embeddedId);

                    size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
                    std::vector<unsigned char> rgbaBuffer(pixelCount * 4, 255);
                    int comp = image.component;
                    bool is16Bit = (image.bits == 16);

                    auto readChannel = [&](size_t p, int c) -> unsigned char {
                        if (c >= comp) return (c == 3) ? 255 : 0;
                        if (is16Bit) {
                            const uint16_t* src16 = reinterpret_cast<const uint16_t*>(image.image.data());
                            return static_cast<unsigned char>(src16[p * comp + c] >> 8);
                        }
                        return image.image[p * comp + c];
                        };

                    for (size_t p = 0; p < pixelCount; ++p) {
                        if (comp == 1) {
                            unsigned char v = readChannel(p, 0);
                            rgbaBuffer[p * 4 + 0] = v;
                            rgbaBuffer[p * 4 + 1] = v;
                            rgbaBuffer[p * 4 + 2] = v;
                            rgbaBuffer[p * 4 + 3] = 255;
                        }
                        else if (comp == 2) {
                            rgbaBuffer[p * 4 + 0] = readChannel(p, 0);
                            rgbaBuffer[p * 4 + 1] = readChannel(p, 1);
                            rgbaBuffer[p * 4 + 2] = 0;
                            rgbaBuffer[p * 4 + 3] = 255;
                        }
                        else {
                            rgbaBuffer[p * 4 + 0] = readChannel(p, 0);
                            rgbaBuffer[p * 4 + 1] = readChannel(p, 1);
                            rgbaBuffer[p * 4 + 2] = readChannel(p, 2);
                            rgbaBuffer[p * 4 + 3] = (comp == 4) ? readChannel(p, 3) : 255;
                        }
                    }

                    if (embeddedTex->LoadFromMemory(rgbaBuffer.data(), image.width, image.height, imageIsSRGB[i])) {
                        resManager.RegisterExternal<Texture>(embeddedId, embeddedTex);
                        textureResourceIds[i] = embeddedId;
                    }
                    else {
                        textureResourceIds[i] = "default_white";
                        resManager.Load<Texture>("default_white");
                    }
                }
                else {
                    textureResourceIds[i] = "default_white";
                    resManager.Load<Texture>("default_white");
                }
            }
        }
        else {
            textureResourceIds.push_back("viking_room");
            resManager.Load<Texture>("viking_room");
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

            // INDICES GUARANTEED ASSIGNMENT FALLBACK
            if (gltfModel.images.empty()) {
                mat.baseColorTextureIndex = 0;
                mat.metallicRoughnessTextureIndex = -1;
                mat.normalTextureIndex = -1;
                mat.occlusionTextureIndex = -1;
                mat.emissiveTextureIndex = -1;
            }
            else {
                auto resolveImageIndex = [&](int gltfTextureIndex) -> int {
                    if (gltfTextureIndex < 0 || gltfTextureIndex >= static_cast<int>(gltfModel.textures.size())) {
                        return -1;
                    }
                    return gltfModel.textures[gltfTextureIndex].source; // Maps texture -> image index!
                    };

                mat.baseColorTextureIndex = resolveImageIndex(gltfMat.pbrMetallicRoughness.baseColorTexture.index);
                mat.metallicRoughnessTextureIndex = resolveImageIndex(gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index);
                mat.normalTextureIndex = resolveImageIndex(gltfMat.normalTexture.index);
                mat.occlusionTextureIndex = resolveImageIndex(gltfMat.occlusionTexture.index);
                mat.emissiveTextureIndex = resolveImageIndex(gltfMat.emissiveTexture.index);

                mat.emissiveFactor = glm::vec3(0.0f);
                if (gltfMat.emissiveFactor.size() == 3) {
                    mat.emissiveFactor = glm::vec3(
                        static_cast<float>(gltfMat.emissiveFactor[0]),
                        static_cast<float>(gltfMat.emissiveFactor[1]),
                        static_cast<float>(gltfMat.emissiveFactor[2])
                    );
                }

                auto extIt = gltfMat.extensions.find("KHR_materials_emissive_strength");
                if (extIt != gltfMat.extensions.end() && extIt->second.Has("emissiveStrength")) {
                    float strength = static_cast<float>(extIt->second.Get("emissiveStrength").GetNumberAsDouble());
                    mat.emissiveFactor *= strength;
                }
                // Ignore non-emissive parts (Rim, Brake, Mirror, Hardware) that mistakenly reference the ORM texture (index 3) at 1.0 strength
                if (mat.emissiveTextureIndex == 3 && mat.emissiveFactor == glm::vec3(1.0f)) {
                    mat.emissiveFactor = glm::vec3(0.0f);
                }
                if (mat.emissiveTextureIndex >= 0 || glm::length(mat.emissiveFactor) > 0.0f) {
                    std::cout << "[EMISSIVE MATERIAL]: " << mat.name
                        << " | texIdx=" << mat.emissiveTextureIndex
                        << " | factor=(" << mat.emissiveFactor.r << ", "
                        << mat.emissiveFactor.g << ", " << mat.emissiveFactor.b << ")\n";
                }

                // Parse KHR_materials_clearcoat
                auto ccIt = gltfMat.extensions.find("KHR_materials_clearcoat");
                if (ccIt != gltfMat.extensions.end()) {
                    if (ccIt->second.Has("clearcoatFactor")) {
                        mat.clearcoatFactor = static_cast<float>(ccIt->second.Get("clearcoatFactor").GetNumberAsDouble());
                    }
                    if (ccIt->second.Has("clearcoatRoughnessFactor")) {
                        mat.clearcoatRoughness = static_cast<float>(ccIt->second.Get("clearcoatRoughnessFactor").GetNumberAsDouble());
                    }
                }

                // Parse KHR_materials_iridescence
                auto iridIt = gltfMat.extensions.find("KHR_materials_iridescence");
                if (iridIt != gltfMat.extensions.end()) {
                    if (iridIt->second.Has("iridescenceFactor")) {
                        mat.iridescenceFactor = static_cast<float>(iridIt->second.Get("iridescenceFactor").GetNumberAsDouble());
                    }
                    if (iridIt->second.Has("iridescenceIor")) {
                        mat.iridescenceIor = static_cast<float>(iridIt->second.Get("iridescenceIor").GetNumberAsDouble());
                    }
                }
            }

            if (gltfMat.alphaMode == "MASK") {
                mat.alphaMode = AlphaMode::MASK;
            }
            else if (gltfMat.alphaMode == "BLEND") {
                mat.alphaMode = AlphaMode::BLEND;
            }
            else if (gltfMat.extensions.contains("KHR_materials_transmission")) {
                mat.alphaMode = AlphaMode::BLEND;
                mat.baseColorFactor.a = 0.15f; // Tinted see-through glass alpha
            }
            else {
                mat.alphaMode = AlphaMode::OPAQUE_MODE;
            }

            mat.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
            mat.doubleSided = gltfMat.doubleSided;
            materials.push_back(std::move(mat));
        }

        // 3. GENERATE STRUCTURAL SCENE NODES
        linearNodes.resize(gltfModel.nodes.size());
        for (size_t i = 0; i < gltfModel.nodes.size(); i++) {
            const auto& node = gltfModel.nodes[i];
            Node* newNode = new Node();
            newNode->name = node.name;
            newNode->index = static_cast<uint32_t>(i);

            //if (node.translation.size() == 3) newNode->translation = glm::make_vec3(node.translation.data());
            if (node.translation.size() == 3) {
                newNode->translation = glm::vec3(
                    static_cast<float>(node.translation[0]),
                    static_cast<float>(node.translation[1]),
                    static_cast<float>(node.translation[2])
                );
            }
            //if (node.rotation.size() == 4)    newNode->rotation = glm::make_quat(node.rotation.data());
            if (node.rotation.size() == 4) {
                newNode->rotation = glm::quat(
                    static_cast<float>(node.rotation[3]), // w
                    static_cast<float>(node.rotation[0]), // x
                    static_cast<float>(node.rotation[1]), // y
                    static_cast<float>(node.rotation[2])  // z
                );
            }
            //if (node.scale.size() == 3)       newNode->scale = glm::make_vec3(node.scale.data());
            if (node.scale.size() == 3) {
                newNode->scale = glm::vec3(
                    static_cast<float>(node.scale[0]),
                    static_cast<float>(node.scale[1]),
                    static_cast<float>(node.scale[2])
                );
            }
            //if (node.matrix.size() == 16)     newNode->matrix = glm::make_mat4(node.matrix.data());
            if (node.matrix.size() == 16) {
                float matFloats[16];
                for (int m = 0; m < 16; ++m) {
                    matFloats[m] = static_cast<float>(node.matrix[m]);
                }
                newNode->matrix = glm::make_mat4(matFloats);
            }
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
        size_t originalNodeCount = gltfModel.nodes.size();
        for (size_t i = 0; i < originalNodeCount; i++) {
            const auto& node = gltfModel.nodes[i];
            if (node.mesh < 0) continue;

            const auto& gltfMesh = gltfModel.meshes[node.mesh];
            if (gltfMesh.primitives.empty()) continue;

            for (size_t p = 0; p < gltfMesh.primitives.size(); p++) {
                const auto& primitive = gltfMesh.primitives[p];
                std::string submeshId = std::format("{}_submesh_node_{}_prim_{}", GetId(), i, p);

                Mesh* meshAsset = new Mesh(submeshId);
                meshAsset->isSubmesh = true;

                auto& indices = meshAsset->GetRawIndices();
                auto& vertices = meshAsset->GetRawVertices();

                // --- INDICES (Supports UINT32, UINT16, and UINT8) ---
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
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        for (size_t k = 0; k < accessor.count; ++k) indices[k] = ptr[k];
                    }
                }

                // --- POSITIONS ---
                auto posIt = primitive.attributes.find("POSITION");
                if (posIt != primitive.attributes.end()) {
                    const auto& accessor = gltfModel.accessors[posIt->second];
                    const auto& view = gltfModel.bufferViews[accessor.bufferView];
                    const auto& buf = gltfModel.buffers[view.buffer];
                    size_t byteStride = accessor.ByteStride(view) ? accessor.ByteStride(view) : sizeof(float) * 3;
                    const uint8_t* basePtr = buf.data.data() + view.byteOffset + accessor.byteOffset;

                    vertices.resize(accessor.count);
                    for (size_t v = 0; v < accessor.count; ++v) {
                        const float* rawPos = reinterpret_cast<const float*>(basePtr + v * byteStride);
                        vertices[v].pos = glm::vec3(rawPos[0], rawPos[1], rawPos[2]);
                        vertices[v].normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up-normal fallback
                        vertices[v].color = glm::vec3(1.0f);
                    }
                }

                // --- NORMALS ---
                auto normIt = primitive.attributes.find("NORMAL");
                if (normIt != primitive.attributes.end()) {
                    const auto& accessor = gltfModel.accessors[normIt->second];
                    const auto& view = gltfModel.bufferViews[accessor.bufferView];
                    const auto& buf = gltfModel.buffers[view.buffer];
                    size_t byteStride = accessor.ByteStride(view) ? accessor.ByteStride(view) : sizeof(float) * 3;
                    const uint8_t* basePtr = buf.data.data() + view.byteOffset + accessor.byteOffset;

                    for (size_t v = 0; v < accessor.count && v < vertices.size(); ++v) {
                        const float* rawNorm = reinterpret_cast<const float*>(basePtr + v * byteStride);
                        vertices[v].normal = glm::vec3(rawNorm[0], rawNorm[1], rawNorm[2]);
                    }
                }

                // --- UV COORDINATES ---
                auto uvIt = primitive.attributes.find("TEXCOORD_0");
                if (uvIt != primitive.attributes.end()) {
                    const auto& accessor = gltfModel.accessors[uvIt->second];
                    const auto& view = gltfModel.bufferViews[accessor.bufferView];
                    const auto& buf = gltfModel.buffers[view.buffer];
                    size_t byteStride = accessor.ByteStride(view) ? accessor.ByteStride(view) : sizeof(float) * 2;
                    const uint8_t* basePtr = buf.data.data() + view.byteOffset + accessor.byteOffset;

                    for (size_t v = 0; v < accessor.count && v < vertices.size(); ++v) {
                        const float* rawUv = reinterpret_cast<const float*>(basePtr + v * byteStride);
                        vertices[v].texCoord = glm::vec2(rawUv[0], rawUv[1]);
                    }
                }

                // Skip empty primitives safely
                if (vertices.empty() || indices.empty()) {
                    delete meshAsset;
                    continue;
                }

                if (meshAsset->doLoad()) {
                    struct ResourceAccessor : public Resource { static void Confirm(Resource* r) { r->loaded = true; } };
                    ResourceAccessor::Confirm(meshAsset);

                    int parsedMaterialIdx = primitive.material;
                    uint32_t safeMaterialIdx = (parsedMaterialIdx < 0 || parsedMaterialIdx >= static_cast<int>(materials.size()))
                        ? 0
                        : static_cast<uint32_t>(parsedMaterialIdx);

                    if (p == 0) {
                        // Attach first primitive directly to the existing scene node
                        linearNodes[i]->mesh = meshAsset;
                        linearNodes[i]->materialIndex = safeMaterialIdx;
                    }
                    else {
                        // Create a child sub-node with identity local transform for additional primitives
                        Node* primNode = new Node();
                        primNode->name = std::format("{}_prim_{}", linearNodes[i]->name, p);
                        primNode->index = static_cast<uint32_t>(linearNodes.size());
                        primNode->parent = linearNodes[i];
                        primNode->mesh = meshAsset;
                        primNode->materialIndex = safeMaterialIdx;

                        linearNodes[i]->children.push_back(primNode);
                        linearNodes.push_back(primNode); // Ensures Model::doUnload() frees it automatically
                    }
                }
                else {
                    delete meshAsset;
                }
            }
        }

        // Animations
        animations.clear();
        for (const auto& gltfAnim : gltfModel.animations) {
            Animation anim{};
            anim.name = gltfAnim.name;

            for (const auto& samp : gltfAnim.samplers) {
                AnimationSampler s{};
                if (samp.interpolation == "STEP") s.interpolation = AnimationSampler::STEP;
                else if (samp.interpolation == "CUBICSPLINE") s.interpolation = AnimationSampler::CUBICSPLINE;
                else s.interpolation = AnimationSampler::LINEAR;

                // Read keyframe timestamps
                const auto& inAcc = gltfModel.accessors[samp.input];
                const auto& inView = gltfModel.bufferViews[inAcc.bufferView];
                const float* inPtr = reinterpret_cast<const float*>(gltfModel.buffers[inView.buffer].data.data() + inView.byteOffset + inAcc.byteOffset);
                s.inputs.assign(inPtr, inPtr + inAcc.count);
                for (float t : s.inputs) {
                    anim.start = std::min(anim.start, t);
                    anim.end = std::max(anim.end, t);
                }

                // Read keyframe outputs (VEC3 for translation/scale, VEC4 for rotation)
                const auto& outAcc = gltfModel.accessors[samp.output];
                const auto& outView = gltfModel.bufferViews[outAcc.bufferView];
                const float* outPtr = reinterpret_cast<const float*>(gltfModel.buffers[outView.buffer].data.data() + outView.byteOffset + outAcc.byteOffset);

                if (outAcc.type == TINYGLTF_TYPE_VEC3) {
                    s.outputsVec3.resize(outAcc.count);
                    memcpy(s.outputsVec3.data(), outPtr, outAcc.count * sizeof(glm::vec3));
                }
                else if (outAcc.type == TINYGLTF_TYPE_VEC4) {
                    s.outputsVec4.resize(outAcc.count);
                    memcpy(s.outputsVec4.data(), outPtr, outAcc.count * sizeof(glm::vec4));
                }
                anim.samplers.push_back(std::move(s));
            }

            for (const auto& chan : gltfAnim.channels) {
                if (chan.target_node < 0 || chan.target_node >= static_cast<int>(linearNodes.size())) continue;
                AnimationChannel c{};
                c.node = linearNodes[chan.target_node];
                c.samplerIndex = static_cast<uint32_t>(chan.sampler);
                if (chan.target_path == "translation") c.path = AnimationChannel::TRANSLATION;
                else if (chan.target_path == "rotation") c.path = AnimationChannel::ROTATION;
                else if (chan.target_path == "scale") c.path = AnimationChannel::SCALE;
                else continue;
                anim.channels.push_back(c);
            }

            if (!anim.channels.empty()) animations.push_back(std::move(anim));
        }
        return true;
    }

    // --- TEXTURE LIFECYCLE MECHANICS ---
    Texture::Texture(const std::string& id) : Resource(id) {}
    Texture::~Texture() { Unload(); }

	bool Texture::doLoad()
    {
#ifndef PROTO_PLATFORM_ANDROID
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
            PROTO_CORE_ERROR("[ERROR]: KTX2 Texture contains invalid 0x0 base dimensions!");
            return false;
        }

        bool ok = UploadToGpu(ktxTexture_GetData(pixels), pixels->dataSize, width, height, m_IsSRGB);

        // Clean up temporary host system file buffers cleanly
        ktxTexture_Destroy(pixels);
        pixels = nullptr;

        return ok; //true;
#else 
        std::vector<std::string> texCandidates = {
            std::format("assets/textures/{}.png", GetId()),
            std::format("assets/textures/{}.jpg", GetId()),
            std::format("assets/textures/{}.jpeg", GetId())
        };
        for (const auto& path : texCandidates) {
            auto bytes = AssetIO::ReadBinaryFile(path);
            if (bytes.empty()) continue;

            int w = 0, h = 0, ch = 0;
            stbi_uc* decoded = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(bytes.data()),
                static_cast<int>(bytes.size()), &w, &h, &ch, 4);
            if (decoded && w > 0 && h > 0) {
                this->width = w;
                this->height = h;
                this->channels = 4;
                bool ok = UploadToGpu(decoded, static_cast<vk::DeviceSize>(w) * h * 4, w, h, m_IsSRGB);
                stbi_image_free(decoded);
                return ok;
            }
        }
        return false;
#endif
    }

    void Texture::doUnload()
    {
        // Explicitly clear our inner RAII block structure
        vkData = {};
    }

    bool Texture::LoadFromMemory(const unsigned char* pixelData, int texWidth, int texHeight, bool srgb)
    {
        if (!pixelData || texWidth <= 0 || texHeight <= 0) {
            PROTO_CORE_ERROR("[ERROR]: Invalid embedded texture pixel buffer for: {}", GetId());
            return false;
        }

        this->m_IsSRGB = srgb;
        this->width = texWidth;
        this->height = texHeight;
        this->channels = 4; // TinyGLTF decodes embedded images to 4-channel RGBA by default

        vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(texWidth) * texHeight * 4;

        if (UploadToGpu(pixelData, imageSize, texWidth, texHeight, srgb)) {
            struct ResourceAccessor : public Resource {
                static void Confirm(Resource* r) { r->loaded = true; }
            };
            ResourceAccessor::Confirm(this);
            return true;
        }
        return false;
    }

    bool Texture::LoadCubemap(const std::string& filePath)
    {
        auto& renderer = VulkanRenderer::Get();
        auto& logicalDevice = renderer.GetLogicalDevice();
        auto* cmdUtil = renderer.GetCommandBuffer();
#ifndef PROTO_PLATFORM_ANDROID
        ktxTexture* kTexture = nullptr;
        if (ktxTexture_CreateFromNamedFile(filePath.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture) != KTX_SUCCESS || !kTexture) {
            PROTO_CORE_ERROR("[CUBEMAP ERROR]: Failed to load KTX2 cubemap: {}", filePath);
            return false;
        }

        uint32_t texWidth = kTexture->baseWidth;
        uint32_t texHeight = kTexture->baseHeight;
        uint32_t mipLevels = kTexture->numLevels;
        vk::Format format = vk::Format::eR16G16B16A16Sfloat;

        auto [stagingBuf, stagingMem] = cmdUtil->CreateBuffer(
            kTexture->dataSize, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        void* mapped = stagingMem.mapMemory(0, kTexture->dataSize);
        memcpy(mapped, ktxTexture_GetData(kTexture), kTexture->dataSize);
        stagingMem.unmapMemory();

        vk::ImageCreateInfo imageInfo{
            .flags = vk::ImageCreateFlagBits::eCubeCompatible,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = { texWidth, texHeight, 1 },
            .mipLevels = mipLevels,
            .arrayLayers = 6,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eUndefined
        };
        vkData.image = logicalDevice.createImage(imageInfo);
        
        vk::MemoryRequirements memReq = vkData.image.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memReq.size,
            .memoryTypeIndex = renderer.vulkanContext->GetDevice().findMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
        };
        vkData.memory = logicalDevice.allocateMemory(allocInfo);
        vkData.image.bindMemory(*vkData.memory, 0);

        std::vector<vk::BufferImageCopy> copyRegions;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t mip = 0; mip < mipLevels; ++mip) {
                ktx_size_t offset = 0;
                ktxTexture_GetImageOffset(kTexture, mip, 0, face, &offset);
                copyRegions.push_back(vk::BufferImageCopy{
                    .bufferOffset = offset,
                    .imageSubresource = { vk::ImageAspectFlagBits::eColor, mip, face, 1 },
                    .imageExtent = { std::max(1u, texWidth >> mip), std::max(1u, texHeight >> mip), 1 }
                    });
            }
        }

        vk::raii::CommandBuffer cmd = cmdUtil->BeginSingleTimeCommands();
        vk::ImageMemoryBarrier toDst{
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = *vkData.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { toDst });
        cmd.copyBufferToImage(*stagingBuf, *vkData.image, vk::ImageLayout::eTransferDstOptimal, copyRegions);

        vk::ImageMemoryBarrier toRead{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = *vkData.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { toRead });
        cmdUtil->EndSingleTimeCommands(std::move(cmd));

        vkData.imageView = logicalDevice.createImageView(vk::ImageViewCreateInfo{
            .image = *vkData.image,
            .viewType = vk::ImageViewType::eCube,
            .format = format,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
            });

        vkData.sampler = logicalDevice.createSampler(vk::SamplerCreateInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = static_cast<float>(mipLevels)
            });

        ktxTexture_Destroy(kTexture);
        struct ResourceAccessor : public Resource { static void Confirm(Resource* r) { r->loaded = true; } };
        ResourceAccessor::Confirm(this);
        return true;
#else
        return false;
#endif
    }

    bool Texture::CreateProceduralStudioCubemap(uint32_t size)
    {
        auto& renderer = VulkanRenderer::Get();
        auto& logicalDevice = renderer.GetLogicalDevice();
        auto* cmdUtil = renderer.GetCommandBuffer();

        mipLevels = 5; // 64 -> 32 -> 16 -> 8 -> 4 roughness mips
        vk::Format format = vk::Format::eR16G16B16A16Sfloat;

        // Helper: evaluate a crisp automotive studio environment for world direction `dir`
        auto evalStudioSky = [](glm::vec3 d, float blur) -> glm::vec3 {
            d = glm::normalize(d);
            glm::vec3 ground(0.05f, 0.05f, 0.05f);
            glm::vec3 horizon(0.18f, 0.20f, 0.24f);
            glm::vec3 zenith(0.12f, 0.15f, 0.22f);
            glm::vec3 col = (d.y < 0.0f)
                ? glm::mix(ground, horizon, glm::clamp(d.y + 1.0f, 0.0f, 1.0f))
                : glm::mix(horizon, zenith, glm::pow(d.y, 0.7f));

            // Crisp overhead softbox strip for clearcoat & metallic highlights
            float softbox = glm::smoothstep(0.72f - blur * 0.40f, 0.96f, d.y)
                * (1.0f - glm::smoothstep(0.30f + blur * 0.40f, 0.85f, std::abs(d.x)));
            col += glm::vec3(1.2f, 1.25f, 1.35f) * softbox * (1.0f - blur * 0.65f);
            return col;
        };

        // Pack all 6 faces x 5 mips into a single staging buffer (RGBA16F = 8 bytes/pixel)
        std::vector<uint16_t> halfData;
        std::vector<vk::BufferImageCopy> copyRegions;

        static const glm::vec3 faceForward[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
        static const glm::vec3 faceUp[] = {{0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}};
        static const glm::vec3 faceRight[] = {{0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}, {1,0,0}, {-1,0,0}};

        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            uint32_t mipSize = std::max(1u, size >> mip);
            float blur = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);

            for (uint32_t face = 0; face < 6; ++face) {
                vk::DeviceSize byteOffset = halfData.size() * sizeof(uint16_t);
                copyRegions.push_back(vk::BufferImageCopy{
                    .bufferOffset = byteOffset,
                    .imageSubresource = { vk::ImageAspectFlagBits::eColor, mip, face, 1 },
                    .imageExtent = { mipSize, mipSize, 1 }
                    });

                for (uint32_t y = 0; y < mipSize; ++y) {
                    for (uint32_t x = 0; x < mipSize; ++x) {
                        float u = ((x + 0.5f) / mipSize) * 2.0f - 1.0f;
                        float v = ((y + 0.5f) / mipSize) * 2.0f - 1.0f;
                        glm::vec3 dir = faceForward[face] + u * faceRight[face] + v * faceUp[face];
                        glm::vec3 c = evalStudioSky(dir, blur);

                        halfData.push_back(glm::packHalf1x16(c.r));
                        halfData.push_back(glm::packHalf1x16(c.g));
                        halfData.push_back(glm::packHalf1x16(c.b));
                        halfData.push_back(glm::packHalf1x16(1.0f));
                    }
                }
            }
        }

        vk::DeviceSize totalBytes = halfData.size() * sizeof(uint16_t);
        auto [stagingBuf, stagingMem] = cmdUtil->CreateBuffer(
            totalBytes, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        void* mapped = stagingMem.mapMemory(0, totalBytes);
        memcpy(mapped, halfData.data(), totalBytes);
        stagingMem.unmapMemory();

        vkData.image = logicalDevice.createImage(vk::ImageCreateInfo{
            .flags = vk::ImageCreateFlagBits::eCubeCompatible,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = { size, size, 1 },
            .mipLevels = mipLevels,
            .arrayLayers = 6,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eUndefined
            });

        vk::MemoryRequirements memReq = vkData.image.getMemoryRequirements();
        vkData.memory = logicalDevice.allocateMemory(vk::MemoryAllocateInfo{
            .allocationSize = memReq.size,
            .memoryTypeIndex = renderer.vulkanContext->GetDevice().findMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
            });
        vkData.image.bindMemory(*vkData.memory, 0);

        vk::raii::CommandBuffer cmd = cmdUtil->BeginSingleTimeCommands();
        vk::ImageMemoryBarrier toDst{
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = *vkData.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { toDst });
        cmd.copyBufferToImage(*stagingBuf, *vkData.image, vk::ImageLayout::eTransferDstOptimal, copyRegions);

        vk::ImageMemoryBarrier toRead{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = *vkData.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { toRead });
        cmdUtil->EndSingleTimeCommands(std::move(cmd));

        vkData.imageView = logicalDevice.createImageView(vk::ImageViewCreateInfo{
            .image = *vkData.image,
            .viewType = vk::ImageViewType::eCube,
            .format = format,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
            });

        vkData.sampler = logicalDevice.createSampler(vk::SamplerCreateInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = static_cast<float>(mipLevels)
            });

        struct ResourceAccessor : public Resource { static void Confirm(Resource* r) { r->loaded = true; } };
        ResourceAccessor::Confirm(this);
        return true;
    }

    bool Texture::CreateProceduralBRDFLUT(uint32_t size)
    {
        // Analytic Schlick-GGX split-sum BRDF LUT approximation (x = scale, y = bias)
        std::vector<unsigned char> pixelsRGBA(size * size * 4, 255);
        for (uint32_t y = 0; y < size; ++y) {
            float roughness = (y + 0.5f) / static_cast<float>(size);
            for (uint32_t x = 0; x < size; ++x) {
                float NdotV = (x + 0.5f) / static_cast<float>(size);
                float f = std::pow(1.0f - NdotV, 5.0f);
                float scale = glm::clamp((1.0f - f) * (1.0f - roughness * 0.35f), 0.0f, 1.0f);
                float bias = glm::clamp(f * (1.0f - roughness * 0.65f), 0.0f, 1.0f);

                size_t idx = (y * size + x) * 4;
                pixelsRGBA[idx + 0] = static_cast<unsigned char>(scale * 255.0f);
                pixelsRGBA[idx + 1] = static_cast<unsigned char>(bias * 255.0f);
                pixelsRGBA[idx + 2] = 0;
                pixelsRGBA[idx + 3] = 255;
            }
        }
        return LoadFromMemory(pixelsRGBA.data(), static_cast<int>(size), static_cast<int>(size), false);
    }

    bool Texture::LoadHDRI(const std::string& filepath, uint32_t faceSize)
    {
        int srcW = 0, srcH = 0, srcChannels = 0;

#ifdef PROTO_PLATFORM_ANDROID
        auto hdrBytes = AssetIO::ReadBinaryFile(filepath);
        float* hdrPixels = hdrBytes.empty() ? nullptr : stbi_loadf_from_memory(
            reinterpret_cast<const stbi_uc*>(hdrBytes.data()),
            static_cast<int>(hdrBytes.size()), &srcW, &srcH, &srcChannels, 4);
#else
        std::string resolvedPath = filepath;
        if (!std::filesystem::exists(resolvedPath)) {
            resolvedPath = std::string(ROOT_WORKSPACE_DIR) + filepath;
        }
        float* hdrPixels = stbi_loadf(resolvedPath.c_str(), &srcW, &srcH, &srcChannels, 4);
#endif

        if (!hdrPixels || srcW <= 0 || srcH <= 0) {
            PROTO_CORE_ERROR("[HDRI WARN]: Failed to load '{}'. Using procedural studio fallback.", filepath);
            CreateProceduralStudioCubemap(64);
            return false;
        }

        auto& lowLevelRenderer = VulkanRenderer::Get();
        auto& device = lowLevelRenderer.GetLogicalDevice();
        auto* imgUtil = lowLevelRenderer.GetVulkanImageSubsystem();
        auto* cmdUtil = lowLevelRenderer.GetCommandBuffer();

        mipLevels = static_cast<uint32_t>(std::floor(std::log2(faceSize))) + 1;
        width = faceSize;
        height = faceSize;

        // Helper: Convert cubemap (face 0..5, uv in [-1, 1]) to 3D direction
        auto faceUVToDir = [](uint32_t face, float u, float v) -> glm::vec3 {
            switch (face) {
                case 0: return glm::normalize(glm::vec3(1.0f, -v, -u)); // +X
                case 1: return glm::normalize(glm::vec3(-1.0f, -v, u)); // -X
                case 2: return glm::normalize(glm::vec3(u, 1.0f, v)); // +Y
                case 3: return glm::normalize(glm::vec3(u, -1.0f, -v)); // -Y
                case 4: return glm::normalize(glm::vec3(u, -v, 1.0f)); // +Z
                default:return glm::normalize(glm::vec3(-u, -v, -1.0f)); // -Z
            }
        };

        // Bilinear sample from equirectangular float buffer (clamped to safe HDR range)
        auto sampleEquirect = [&](const glm::vec3& dir) -> glm::vec4 {
            float phi = std::atan2(dir.z, dir.x);
            float theta = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
            float u = phi * 0.159154943f + 0.5f;   // 1 / (2*pi)
            float v = 0.5f - theta * 0.318309886f; // 1 / pi

            float fx = glm::clamp(u * static_cast<float>(srcW - 1), 0.0f, static_cast<float>(srcW - 1));
            float fy = glm::clamp(v * static_cast<float>(srcH - 1), 0.0f, static_cast<float>(srcH - 1));
            int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
            int x1 = std::min(x0 + 1, srcW - 1), y1 = std::min(y0 + 1, srcH - 1);
            float tx = fx - x0, ty = fy - y0;

            auto fetch = [&](int x, int y) -> glm::vec4 {
                const float* p = hdrPixels + (y * srcW + x) * 4;
                // Clamp extreme sun spikes to 65000 (half-float max) so NaN/Inf never occurs
                return glm::min(glm::vec4(p[0], p[1], p[2], 1.0f), glm::vec4(65000.0f));
            };

            glm::vec4 c00 = fetch(x0, y0), c10 = fetch(x1, y0);
            glm::vec4 c01 = fetch(x0, y1), c11 = fetch(x1, y1);
            return glm::mix(glm::mix(c00, c10, tx), glm::mix(c01, c11, tx), ty);
        };

        // Pack all 6 faces into a single staging buffer (RGBA32F -> R16G16B16A16Sfloat via glm::packHalf4x16)
        size_t pixelsPerFace = static_cast<size_t>(faceSize) * faceSize;
        vk::DeviceSize totalBytes = pixelsPerFace * 6 * sizeof(uint64_t);

        auto [stagingBuf, stagingMem] = cmdUtil->CreateBuffer(
            totalBytes,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        uint64_t* dst = static_cast<uint64_t*>(stagingMem.mapMemory(0, totalBytes));
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < faceSize; ++y) {
                float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;
                for (uint32_t x = 0; x < faceSize; ++x) {
                    float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
                    glm::vec4 rgba = sampleEquirect(faceUVToDir(face, u, v));
                    dst[face * pixelsPerFace + y * faceSize + x] = glm::packHalf4x16(rgba);
                }
            }
        }
        stagingMem.unmapMemory();
        stbi_image_free(hdrPixels);

        // Create 6-layer Cubemap Image with eTransferSrc | eTransferDst | eSampled
        vk::Format format = vk::Format::eR16G16B16A16Sfloat;
        vk::ImageCreateInfo imageInfo{
            .flags = vk::ImageCreateFlagBits::eCubeCompatible,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = { faceSize, faceSize, 1 },
            .mipLevels = mipLevels,
            .arrayLayers = 6,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined
        };
        //VulkanGpuData dataImage;
        this->vkData.image = device.createImage(imageInfo);

        vk::MemoryRequirements memReqs = this->vkData.image.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memReqs.size,
            .memoryTypeIndex = lowLevelRenderer.vulkanContext->GetDevice().findMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
        };
        this->vkData.memory = device.allocateMemory(allocInfo);
        this->vkData.image.bindMemory(*this->vkData.memory, 0);

        // Upload Mip 0 for all 6 faces, then generate Mips 1..N via GPU blitImage
        vk::raii::CommandBuffer cmd = cmdUtil->BeginSingleTimeCommands();

        vk::ImageMemoryBarrier toTransferDst{
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = *this->vkData.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { toTransferDst });

        vk::BufferImageCopy region{
            .bufferOffset = 0,
            .imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 6 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { faceSize, faceSize, 1 }
        };
        cmd.copyBufferToImage(*stagingBuf, *this->vkData.image, vk::ImageLayout::eTransferDstOptimal, { region });

        int32_t mipW = static_cast<int32_t>(faceSize);
        int32_t mipH = static_cast<int32_t>(faceSize);
        for (uint32_t i = 1; i < mipLevels; ++i) {
            vk::ImageMemoryBarrier mipToSrc{
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .image = *this->vkData.image,
                .subresourceRange = { vk::ImageAspectFlagBits::eColor, i - 1, 1, 0, 6 }
            };
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, { mipToSrc });

            int32_t nextW = std::max(1, mipW / 2);
            int32_t nextH = std::max(1, mipH / 2);

            vk::ImageBlit blit{
                .srcSubresource = { vk::ImageAspectFlagBits::eColor, i - 1, 0, 6 },
                .srcOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ mipW, mipH, 1 } },
                .dstSubresource = { vk::ImageAspectFlagBits::eColor, i, 0, 6 },
                .dstOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ nextW, nextH, 1 } }
            };
            cmd.blitImage(*this->vkData.image, vk::ImageLayout::eTransferSrcOptimal, *this->vkData.image, vk::ImageLayout::eTransferDstOptimal, { blit }, vk::Filter::eLinear);

            vk::ImageMemoryBarrier mipToRead{
                .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .image = *this->vkData.image,
                .subresourceRange = { vk::ImageAspectFlagBits::eColor, i - 1, 1, 0, 6 }
            };
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { mipToRead });

            mipW = nextW;
            mipH = nextH;
        }

        vk::ImageMemoryBarrier lastMipToRead{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = *this->vkData.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, mipLevels - 1, 1, 0, 6 }
        };
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, { lastMipToRead });
        cmdUtil->EndSingleTimeCommands(std::move(cmd));

        // Create Cube ImageView & Trilinear Sampler across all mip levels
        vk::ImageViewCreateInfo viewInfo{
            .image = *this->vkData.image,
            .viewType = vk::ImageViewType::eCube,
            .format = format,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 6 }
        };
        this->vkData.imageView = device.createImageView(viewInfo);

        vk::SamplerCreateInfo samplerInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = static_cast<float>(mipLevels)
        };
        this->vkData.sampler = device.createSampler(samplerInfo);
        
        struct ResourceAccessor : public Resource { static void Confirm(Resource* r) { r->loaded = true; } };
        ResourceAccessor::Confirm(this);
        return true;
    }

#ifndef PROTO_PLATFORM_ANDROID
    ktxTexture* Texture::LoadImageData(const std::string& filePath, int* w, int* h, int* ch)
    {
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
#endif

    bool Texture::UploadToGpu(const void* data, vk::DeviceSize imageSize, int texWidth, int texHeight, bool srgb)
    {
        auto& renderer = VulkanRenderer::Get();
        auto* imgUtil = renderer.GetVulkanImageSubsystem();
        auto* cmdUtil = renderer.GetCommandBuffer();

        vk::Format imageFormat = srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;;

        auto [stagingBuf, stagingBufMem] = cmdUtil->CreateBuffer(
            imageSize,
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );

        void* mappedData = stagingBufMem.mapMemory(0, imageSize);
        memcpy(mappedData, data, imageSize);
        stagingBufMem.unmapMemory();

        auto [gpuImage, gpuMemory] = imgUtil->CreateImage(
            texWidth, texHeight, 1,
            vk::SampleCountFlagBits::e1,
            imageFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        vkData.image = std::move(gpuImage);
        vkData.memory = std::move(gpuMemory);

        vk::raii::CommandBuffer texCmd = cmdUtil->BeginSingleTimeCommands();
        imgUtil->TransitionImageLayout(texCmd, vkData.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);
        BufferUtils::CopyBufferToImage(texCmd, stagingBuf, vkData.image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
        imgUtil->TransitionImageLayout(texCmd, vkData.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);
        cmdUtil->EndSingleTimeCommands(std::move(texCmd));

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

        vkData.sampler = vk::raii::Sampler(logicalDevice, samplerInfo);

        return true;
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