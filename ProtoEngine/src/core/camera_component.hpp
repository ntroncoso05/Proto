#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

//#include "component.hpp"
#include "entity.hpp"
#include "transform_component.hpp"

// Camera component
// Defines a viewpoint for rendering the scene by managing view and projection matrices
class CameraComponent : public Component {
private:
    float fieldOfView = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    bool projectionDirty = true;

public:
    void SetPerspective(float fov, float aspect, float near, float far) {
        fieldOfView = fov;
        aspectRatio = aspect;
        nearPlane = near;
        farPlane = far;
        projectionDirty = true;
    }

    glm::mat4 GetViewMatrix() const {
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

    glm::mat4 GetProjectionMatrix() {
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
};