#include "protopch.hpp"
#include "ComponentTypes.hpp"
#include "Entity.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include "Core/ServiceLocator.hpp"

namespace Proto
{

    void TransformComponent::SetPosition(const glm::vec3& pos) {
        position = pos;
        transformDirty = true;
    }

    void TransformComponent::SetRotation(const glm::quat& rot) {
        rotation = rot;
        transformDirty = true;
    }

    void TransformComponent::SetScale(const glm::vec3& s) {
        scale = s;
        transformDirty = true;
    }

    glm::mat4 TransformComponent::GetLocalTransformMatrix() const
    {
        if (transformDirty) {
            glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
            glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
            glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);

            localMatrix = translationMatrix * rotationMatrix * scaleMatrix;
            transformDirty = false;
        }
        return localMatrix;
    }

    // --- REFACTORED: Hierarchical Matrix Cascade Logic ---
    glm::mat4 TransformComponent::GetWorldTransformMatrix() const
    {
        glm::mat4 local = GetLocalTransformMatrix();

        // Check if our owner entity has been attached to a parent node
        Entity* ownerEntity = GetOwner(); // Inherited via Component base class
        if (ownerEntity)
        {
            Entity* parentEntity = ownerEntity->GetParent();
            if (parentEntity)
            {
                // Recursively fetch parent transform matrix components
                auto parentTransform = parentEntity->GetComponent<TransformComponent>();
                if (parentTransform)
                {
                    // Order is critical: Parent World Matrix * Child Local Matrix
                    return parentTransform->GetWorldTransformMatrix() * local;
                }
            }
        }
        return local; // Root object fallback layout
    }

    // MESH

    void MeshComponent::Render()
    {
        // 1. LIFETIME GUARD: Verify the asset is valid and resident before drawing []
        if (!m_MeshHandle.IsValid()) return;

        // Extract the typed underlying resource safely from the registry []
        Mesh* rawMesh = m_MeshHandle.Get();
        if (!rawMesh || !rawMesh->IsLoaded()) return;

        // Looks up the baseline abstract interface cleanly!
        auto& rendererAPI = ServiceLocator::Get<RendererAPI>();

        // 1. Fetch your global graphics device factory interfaces seamlessly through the Service Locator
        auto& renderer = static_cast<VulkanRenderer&>(rendererAPI);
        auto* cmdUtil = renderer.GetCommandBuffer();
        auto& swapChain = ServiceLocator::Get<VulkanSwapChain>();

        // 1. Safe guard check: Ensure the parent container context is fully valid
        Entity* parentEntity = GetOwner();
        if (!parentEntity) return;

        // Get transform component
        // 2. Fetch the corresponding transform metrics from the scene hierarchy
        auto transform = parentEntity->GetComponent<TransformComponent>();
        if (!transform) return;

        // 3. Render mesh geometry using the active transform matrices
        //Mesh* rawMesh = m_MeshHandle.Get();
        
        glm::mat4 absoluteWorldMatrix = transform->GetWorldTransformMatrix();
        //glm::mat4 gltfConversion = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 finalModelMatrix = absoluteWorldMatrix;

        //auto& renderer = VulkanRenderer::Get();
        uint32_t currentFrame = swapChain.GetFrameIndex();
        vk::raii::CommandBuffer& cmd = cmdUtil->commandBuffers[currentFrame];

        //Mesh* rawMesh = m_MeshHandle.Get();
        cmd.bindVertexBuffers(0, { *rawMesh->GetVertexBuffer() }, { 0 });
        cmd.bindIndexBuffer(*rawMesh->GetIndexBuffer(), 0, vk::IndexType::eUint32);

        cmd.drawIndexed(rawMesh->GetIndexCount(), 1, 0, 0, 0);
    }

    BoundingBox MeshComponent::GetBoundingBox()
    {
        BoundingBox box;

        // 🟩 FIX 1: Check if the resource handle wrapper holds a valid, loaded mesh asset
        if (m_MeshHandle.IsValid() && m_MeshHandle->IsLoaded()) {
            glm::vec3 minBound(std::numeric_limits<float>::max());
            glm::vec3 maxBound(std::numeric_limits<float>::lowest());

            // 🟩 FIX 2: Extract geometry from m_MeshHandle instead of the non-existent m_Meshes vector
            Mesh* rawMesh = m_MeshHandle.Get();

            // 🟩 FIX 3: Explicitly map the loop target container elements using Proto::Vertex type layouts
            for (const Proto::Vertex& vertex : rawMesh->GetRawVertices()) {
                // glm::min/max expect two arguments of matching vector type profiles
                minBound = glm::min(minBound, vertex.pos);
                maxBound = glm::max(maxBound, vertex.pos);
            }

            box.minBound = minBound;
            box.maxBound = maxBound;
        }
        return box;
    }

    void MeshComponent::OnEvent(Event& event)
    {
        EventDispatcher dispatcher(event);
        // Example: Components can listen for resource reloads or window shifts here
    }

    // CAMERA
    void CameraComponent::SetPerspective(float fov, float aspect, float near, float far)
    {
        fieldOfView = fov;
        aspectRatio = aspect;
        nearPlane = 0.1f;
        farPlane = 100.0f;
        projectionDirty = true;
    }

    glm::mat4 CameraComponent::GetViewMatrix() const
    {
        // Get transform component
        auto transform = GetOwner()->GetComponent<TransformComponent>();
        if (transform) {
            // Calculate view matrix from transform
            glm::vec3 position = transform->GetPosition();
            glm::quat rotation = transform->GetRotation();

            // Forward vector (local -Z)
            glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
            // Up vector (local +Y)
            glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

            return glm::lookAt(position, position + forward, up);
        }
        return glm::mat4(1.0f);
    }

    glm::mat4 CameraComponent::GetProjectionMatrix() const
    {
        if (projectionDirty) {
            projectionMatrix = glm::perspective(
                glm::radians(fieldOfView),
                aspectRatio,
                nearPlane,
                farPlane
            );
            projectionDirty = false;
        }
        return projectionMatrix;
    }

    glm::vec3 CameraComponent::GetPosition() const
    {
        if (Entity* owner = GetOwner()) {
            if (auto* transform = owner->GetComponent<TransformComponent>()) {
                return transform->GetPosition();
            }
        }
        return glm::vec3(0.0f); // Safe zeroed fallback layout if detached
    }

}