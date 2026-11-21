#pragma once
#include "Component.hpp"
#include "ResourceTypes.hpp"
#include "ResourceManager.hpp"

namespace Proto
{
    // 3D Axis-Aligned Bounding Box
    struct BoundingBox {

        BoundingBox() = default;
        BoundingBox(const glm::vec3& minB, const glm::vec3& maxB) : minBound(minB), maxBound(maxB) {}

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
        ResourceHandle<Mesh> m_MeshHandle;

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

        void Render();

        BoundingBox GetBoundingBox();

        inline ResourceHandle<Mesh> GetMeshHandle() const { return m_MeshHandle; }
        inline void SetMeshHandle(const ResourceHandle<Mesh>& handle) { m_MeshHandle = handle; }

        // Convenience accessor to pull the inner address when drawing
        inline Mesh* GetRawMesh() const { return m_MeshHandle.Get(); }

        virtual void OnEvent(Event& event) override;
    };

    struct Plane {
        glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;

        void normalize() {
            float len = glm::length(normal);
            if (len > 0.0f) {
                normal /= len;
                distance /= len;
            }
        }
    };

    class Frustum {
    public:
        void update(const glm::mat4& viewProj) {
            // Left Plane
            m_Planes[0] = glm::vec4(viewProj[0][3] + viewProj[0][0], viewProj[1][3] + viewProj[1][0], viewProj[2][3] + viewProj[2][0], viewProj[3][3] + viewProj[3][0]);
            // Right Plane
            m_Planes[1] = glm::vec4(viewProj[0][3] - viewProj[0][0], viewProj[1][3] - viewProj[1][0], viewProj[2][3] - viewProj[2][0], viewProj[3][3] - viewProj[3][0]);
            // Bottom Plane
            m_Planes[2] = glm::vec4(viewProj[0][3] + viewProj[0][1], viewProj[1][3] + viewProj[1][1], viewProj[2][3] + viewProj[2][1], viewProj[3][3] + viewProj[3][1]);
            // Top Plane
            m_Planes[3] = glm::vec4(viewProj[0][3] - viewProj[0][1], viewProj[1][3] - viewProj[1][1], viewProj[2][3] - viewProj[2][1], viewProj[3][3] - viewProj[3][1]);
            // Near Plane
            m_Planes[4] = glm::vec4(viewProj[0][3] + viewProj[0][2], viewProj[1][3] + viewProj[1][2], viewProj[2][3] + viewProj[2][2], viewProj[3][3] + viewProj[3][2]);
            // Far Plane
            m_Planes[5] = glm::vec4(viewProj[0][3] - viewProj[0][2], viewProj[1][3] - viewProj[1][2], viewProj[2][3] - viewProj[2][2], viewProj[3][3] - viewProj[3][2]);

            for (auto& plane : m_Planes) {
                float len = glm::length(glm::vec3(plane));
                if (len > 0.0f) plane /= len;
            }
        }

        bool Intersects(const BoundingBox& box) const {
            for (const auto& plane : m_Planes) {
                glm::vec3 positiveVertex = box.minBound;
                if (plane.x >= 0) positiveVertex.x = box.maxBound.x;
                if (plane.y >= 0) positiveVertex.y = box.maxBound.y;
                if (plane.z >= 0) positiveVertex.z = box.maxBound.z;
                if (glm::dot(glm::vec3(plane), positiveVertex) + plane.w < 0.0f) {
                    return false;
                }
            }
            return true;
        }

    private:
        std::array<glm::vec4, 6> m_Planes{};
    };

    // Camera component
    // Defines a viewpoint for rendering the scene by managing view and projection matrices
    class CameraComponent : public Component {
    private:
        float aspectRatio = 1280.0f / 720.0f;
        float FOV = 45.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        glm::mat4 viewMatrix = glm::mat4(1.0f);
        // mutable: GetProjectionMatrix() is logically const (it just returns a
        // matrix) even though it lazily recomputes and caches that matrix on
        // first access after the projection parameters change.
        mutable glm::mat4 projectionMatrix = glm::mat4(1.0f);
        mutable bool projectionDirty = true;

    public:
        void SetPerspective(float fov, float aspect, float NearClip, float FarClip);
        void SetAspectRatio(float aspect) { aspectRatio = aspect; projectionDirty = true; }
        void SetFOV(float fov) { FOV = fov; projectionDirty = true; }

        glm::mat4 GetViewMatrix() const;

        glm::mat4 GetProjectionMatrix() const;

        glm::vec3 GetPosition() const;

        float GetNearClip() const { return nearPlane; }
        float GetFarClip() const { return farPlane; }
        float GetFOV() const { return FOV; }

        float MovementSpeed = 5.0f;
        float MouseSensitivity = 0.1f;

        Frustum GetFrustum() const { Frustum f; f.update(GetProjectionMatrix() * GetViewMatrix()); return f; }
    };

    class ModelComponent : public Component {
    private:
        ResourceHandle<Model> m_ModelHandle;
    public:
        ModelComponent() = default;
        explicit ModelComponent(const ResourceHandle<Model>& handle) : m_ModelHandle(handle) {}

        inline ResourceHandle<Model> GetModelHandle() const { return m_ModelHandle; }
    };

    class AnimationComponent : public Component {
    public:
        uint32_t ActiveClipIndex = 0;
        float CurrentTime = 0.0f;
        float PlaybackSpeed = 1.0f;
        bool IsPlaying = true;
        bool Loop = true;
    };

    enum class LightType { Directional = 0, Point = 1, Spot = 2 };

    class LightComponent : public Component {
    public:
        LightType Type = LightType::Directional;
        glm::vec3 Color = glm::vec3(1.0f);
        float Intensity = 3.0f;
        float Radius = 15.0f;
        float InnerConeAngle = 15.0f; // Degrees (for Spot)
        float OuterConeAngle = 30.0f; // Degrees (for Spot)
        bool CastShadows = true;
    };

    enum class BodyMotionType { Static = 0, Kinematic = 1, Dynamic = 2 };

    class RigidBodyComponent : public Component {
    public:
        BodyMotionType MotionType = BodyMotionType::Dynamic;
        float Mass = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.25f; // Bounciness [0.0, 1.0]
        float LinearDamping = 0.05f;
        float AngularDamping = 0.05f;
        bool UseGravity = true;

        // Runtime Jolt BodyID handle (uint32_t::max() = uninitialized)
        uint32_t RuntimeBodyID = 0xFFFFFFFF;
    };

    class BoxColliderComponent : public Component {
    public:
        glm::vec3 HalfExtents = glm::vec3(0.5f);
        glm::vec3 Offset = glm::vec3(0.0f);
        bool IsTrigger = false;
    };

    class ParticleEmitterComponent : public Component {
    public:
        glm::vec3 Offset = glm::vec3(0.0f, 0.35f, 2.15f); // Local offset from entity pivot
        glm::vec3 Direction = glm::vec3(0.0f, 0.35f, 1.0f);  // Local emission direction
        glm::vec3 Color = glm::vec3(1.0f, 0.45f, 0.1f);  // Base RGB tint
        float HDRIntensity = 4.0f;  // > 1.0 triggers HDR Bloom
        float ParticleSize = 0.16f;
        float InitialSpeed = 3.5f;
        float Spread = 0.45f; // Cone spread [0.0, 1.0]
        float Gravity = -6.5f;
        float MinLife = 0.8f;
        float MaxLife = 1.8f;
        int   ActiveParticles = 2048;  // Spawn rate / active count (1..4096)
        bool  Enabled = true;
    };

    struct MaterialOverride {
        bool      Enabled = false;
        glm::vec4 BaseColorFactor = glm::vec4(1.0f);
        float     MetallicFactor = 0.0f;
        float     RoughnessFactor = 0.5f;
        float     ClearcoatFactor = 0.0f;
        float     ClearcoatRoughness = 0.0f;
        float     IridescenceFactor = 0.0f;
        glm::vec3 EmissiveFactor = glm::vec3(0.0f);
    };

    class MaterialOverrideComponent : public Component {
    public:
        // Key = material index inside the entity's Model::materials vector
        std::unordered_map<uint32_t, MaterialOverride> Overrides;
    };
}