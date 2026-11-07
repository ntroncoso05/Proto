#pragma once
#include "Component.hpp"
#include "ResourceTypes.hpp"
#include "ResourceManager.hpp"

namespace Proto
{
    // 3D Axis-Aligned Bounding Box
    struct BoundingBox {
        glm::vec3 minBound = glm::vec3(-1.0f);
        glm::vec3 maxBound = glm::vec3(1.0f);

        glm::mat4 transform;

        // Explicit method to transform the AABB bounds into absolute world positions
        void Transform(const glm::mat4& transformMatrix) {
            std::array<glm::vec3, 8> corners = {
                glm::vec3(minBound.x, minBound.y, minBound.z),
                glm::vec3(maxBound.x, minBound.y, minBound.z),
                glm::vec3(minBound.x, maxBound.y, minBound.z),
                glm::vec3(maxBound.x, maxBound.y, minBound.z),
                glm::vec3(minBound.x, minBound.y, maxBound.z),
                glm::vec3(maxBound.x, minBound.y, maxBound.z),
                glm::vec3(minBound.x, maxBound.y, maxBound.z),
                glm::vec3(maxBound.x, maxBound.y, maxBound.z)
            };

            glm::vec3 newMin(std::numeric_limits<float>::max());
            glm::vec3 newMax(std::numeric_limits<float>::lowest());

            for (const auto& corner : corners) {
                glm::vec4 transformed = transformMatrix * glm::vec4(corner, 1.0f);
                glm::vec3 projected = glm::vec3(transformed) / transformed.w;
                newMin = glm::min(newMin, projected);
                newMax = glm::max(newMax, projected);
            }

            minBound = newMin;
            maxBound = newMax;
        }

        // Checks if this AABB intersects with another AABB
        bool intersects(const BoundingBox& other) const {
            return (minBound.x <= other.maxBound.x && maxBound.x >= other.minBound.x) &&
                (minBound.y <= other.maxBound.y && maxBound.y >= other.minBound.y) &&
                (minBound.z <= other.maxBound.z && maxBound.z >= other.minBound.z);
        }

        void SetTransform(glm::mat4 transformMatrix) { transform = transformMatrix; }

        // Computes a new AABB that encloses a collection of 3D points
        // if the object transforms (moves, scales, or rotates), you must recalculate the box
        BoundingBox computeAABB(const std::vector<glm::vec3>& vertices) {
            if (vertices.empty()) {
                return { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
            }

            // Initialize minBound/maxBound with the first vertex
            BoundingBox box = { vertices[0], vertices[0] };

            // Expand the box boundaries to include every vertex
            for (const auto& v : vertices) {
                box.minBound.x = std::min(box.minBound.x, v.x);
                box.minBound.y = std::min(box.minBound.y, v.y);
                box.minBound.z = std::min(box.minBound.z, v.z);

                box.maxBound.x = std::max(box.maxBound.x, v.x);
                box.maxBound.y = std::max(box.maxBound.y, v.y);
                box.maxBound.z = std::max(box.maxBound.z, v.z);
            }

            return box;
        }
    };

    // Transform component
    // Handles the position, rotation, and scale of an entity in 3D space. AffineTransform or "Pose" matrix.
    class TransformComponent : public Component {
    private:
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion
        glm::vec3 scale = glm::vec3(1.0f);

        // Cached transformation matrix
        mutable glm::mat4 localMatrix = glm::mat4(1.0f);
        mutable bool transformDirty = true;

    public:
        void SetPosition(const glm::vec3& pos);

        void SetRotation(const glm::quat& rot);

        void SetScale(const glm::vec3& s);

        const glm::vec3& GetPosition() const { return position; }
        const glm::quat& GetRotation() const { return rotation; }
        const glm::vec3& GetScale() const { return scale; }

        // --- REFACTORED: Structural Hierarchical Matrix Math Getters ---
        glm::mat4 GetLocalTransformMatrix() const;
        glm::mat4 GetWorldTransformMatrix() const;
    };

    // Mesh component
    // Manages the visual representation of an entity by handling its 3D mesh and material
    class MeshComponent : public Component {
    private:
        //Mesh* m_Mesh = nullptr;
        ResourceHandle<Mesh> m_MeshHandle;
        //Material* material = nullptr;

    public:
        MeshComponent() = default;
        explicit MeshComponent(const ResourceHandle<Mesh>& meshHandle)
            : m_MeshHandle(meshHandle) {}

        explicit MeshComponent(Mesh* rawMesh)
            : m_MeshHandle(rawMesh ? ResourceHandle<Mesh>(rawMesh->GetId(), nullptr) : ResourceHandle<Mesh>()) {}

        // 3. THE MISSING LINK CONSTRUCTOR OVERLOAD:
        // Automatically converts forwarded Proto::Node* arguments during GLTF parsing handed frames!
        explicit MeshComponent(Node* sceneGraphNode) {
            if (sceneGraphNode && !sceneGraphNode->meshResourceId.empty()) {
                // If it tracks an asset manager key ID string, hook it up!
                m_MeshHandle = ResourceHandle<Mesh>(sceneGraphNode->meshResourceId, nullptr);
            }
            else if (sceneGraphNode && sceneGraphNode->mesh != nullptr) {
                // Fallback direct extraction if the node carries a raw mesh allocation address
                m_MeshHandle = ResourceHandle<Mesh>(sceneGraphNode->mesh->GetId(), nullptr);
            }
        }

        explicit MeshComponent(const ResourceHandle<Model>& modelHandle) {
            if (modelHandle.IsValid()) {
                Model* model = modelHandle.Get();
                if (model && !model->linearNodes.empty()) {
                    // Find the first subnode in the model that contains geometry
                    for (auto* node : model->linearNodes) {
                        if (node && !node->meshResourceId.empty()) {
                            m_MeshHandle = ResourceHandle<Mesh>(node->meshResourceId, nullptr);
                            break;
                        }
                    }
                }
            }
        }

        virtual ~MeshComponent() override = default;

        //virtual void Render() override;
        void Render();

        BoundingBox GetBoundingBox();

        inline ResourceHandle<Mesh> GetMeshHandle() const { return m_MeshHandle; }
        inline void SetMeshHandle(const ResourceHandle<Mesh>& handle) { m_MeshHandle = handle; }

        // Convenience accessor to pull the inner address when drawing
        inline Mesh* GetRawMesh() const { return m_MeshHandle.Get(); }

        virtual void OnEvent(Event& event) override;
    };

    // Camera component
    // Defines a viewpoint for rendering the scene by managing view and projection matrices
    class CameraComponent : public Component {
    private:
        float fieldOfView = 45.0f;
        float aspectRatio = 16.0f / 9.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;

        glm::mat4 viewMatrix = glm::mat4(1.0f);
        // mutable: GetProjectionMatrix() is logically const (it just returns a
        // matrix) even though it lazily recomputes and caches that matrix on
        // first access after the projection parameters change.
        mutable glm::mat4 projectionMatrix = glm::mat4(1.0f);
        mutable bool projectionDirty = true;

    public:
        void SetPerspective(float fov, float aspect, float near, float far);

        glm::mat4 GetViewMatrix() const;

        glm::mat4 GetProjectionMatrix() const;

        glm::vec3 GetPosition() const;
    };

    class ModelComponent : public Component {
    private:
        ResourceHandle<Model> m_ModelHandle;
    public:
        ModelComponent() = default;
        explicit ModelComponent(const ResourceHandle<Model>& handle) : m_ModelHandle(handle) {}

        inline ResourceHandle<Model> GetModelHandle() const { return m_ModelHandle; }
    };

}