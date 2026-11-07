#include "protopch.hpp"
#include "Camera.hpp"
#include "Core/KeyCodes.hpp"
#include "Core/InputSystem.hpp"
#include "Core/Application.hpp"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/ComponentTypes.hpp"

namespace Proto
{
    Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
        :position(position), up(up), yaw(yaw), pitch(pitch)
    {
        // Calculate initial front, right, and up vectors accurately
        updateCameraVectors();
    }

    void Camera::processKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = movementSpeed * deltaTime;//1.0f * deltaTime;

        switch (direction) {
            case CameraMovement::FORWARD:
                position += front * velocity * zoom;
                break;
            case CameraMovement::BACKWARD:
                position -= front * velocity * zoom;
                break;
            case CameraMovement::LEFT:
                position -= right * velocity * zoom;
                break;
            case CameraMovement::RIGHT:
                position += right * velocity * zoom;
                break;
            case CameraMovement::UP:
                position += up * velocity * zoom;
                break;
            case CameraMovement::DOWN:
                position -= up * velocity * zoom;
                break;
        }
    }

    // Keyboard input processing for camera translation
    // Handles discrete directional commands with frame-rate independent timing
    void Camera::processInput(GLFWwindow* window, std::shared_ptr<Camera> camera, float deltaTime)
    {
        bool imguiCapturingKeyboard = InputSystem::IsImGuiCapturingKeyboard();
        // Check if ImGui is capturing mouse input
        bool imguiCapturingMouse = InputSystem::IsImGuiCapturingMouse();
        // Get the current input state
        const InputState& inputState = InputSystem::GetInputState();

        // Process keyboard input for camera movement if ImGui is not capturing keyboard
        if (!imguiCapturingKeyboard) {
            // Forward these to the camera system
            // This could be done through the action callback system
            if (InputSystem::IsActionActive(InputAction::MOVE_FORWARD))
                camera.get()->processKeyboard(CameraMovement::FORWARD, deltaTime);
            if (InputSystem::IsActionActive(InputAction::MOVE_BACKWARD))
                camera.get()->processKeyboard(CameraMovement::BACKWARD, deltaTime);
            if (InputSystem::IsActionActive(InputAction::MOVE_LEFT))
                camera.get()->processKeyboard(CameraMovement::LEFT, deltaTime);
            if (InputSystem::IsActionActive(InputAction::MOVE_RIGHT))
                camera.get()->processKeyboard(CameraMovement::RIGHT, deltaTime);
            if (InputSystem::IsActionActive(InputAction::MOVE_UP))
                camera.get()->processKeyboard(CameraMovement::UP, deltaTime);
            if (InputSystem::IsActionActive(InputAction::MOVE_DOWN))
                camera.get()->processKeyboard(CameraMovement::DOWN, deltaTime);
        }

        // Process mouse/touch input for camera rotation if ImGui is not capturing mouse
        if (!imguiCapturingMouse) {
            if (inputState.cursorDelta.x != 0.0f || inputState.cursorDelta.y != 0.0f) {
                camera->processMouseMovement(inputState.cursorDelta.x, -inputState.cursorDelta.y);
            }

            if (inputState.scrollDelta != 0.0f) {
                camera->processMouseScroll(inputState.scrollDelta);
            }
        }
    }

    void Camera::mouseCallback(double xpos, double ypos)
    {
        // State persistence for calculating movement deltas
        // Static variables maintain state between callback invocations
        static bool firstMouse = true;          // Flag to handle initial mouse position
        static float lastX = 0.0f, lastY = 0.0f;  // Previous mouse position for delta calculation

        // Handle initial mouse position to prevent sudden camera jumps
        // First callback provides absolute position, not relative movement
        if (firstMouse) {
            lastX = xpos;               // Initialize previous position
            lastY = ypos;
            firstMouse = false;         // Disable special handling for subsequent calls
        }

        // Calculate mouse movement deltas since last callback
        // These deltas represent the amount and direction of mouse movement
        float xoffset = xpos - lastX;                   // Horizontal movement (left-right)
        float yoffset = lastY - ypos;                   // Vertical movement (inverted: screen Y increases downward, camera pitch increases upward)

        // Update state for next callback iteration
        lastX = xpos;
        lastY = ypos;

        // Convert mouse movement to camera rotation
        // Delta values drive continuous camera orientation changes
        processMouseMovement(xoffset, yoffset);
    }

    void Camera::scrollCallback(double xoffset, double yoffset)
    {
        // Direct scroll-to-zoom mapping
        // Positive yoffset (scroll up) typically zooms in, negative (scroll down) zooms out
        processMouseScroll(yoffset);
    }

    void Camera::setupInputCallbacks(Event& e)
    {
        // Register callback functions with the windowing system
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseMovedEvent>([this](const MouseMovedEvent& e) {this->mouseCallback(e.GetX(), e.GetY()); });
        dispatcher.Dispatch<MouseScrolledEvent>([this](const MouseScrolledEvent& e) {this->scrollCallback(e.GetXOffset(), e.GetYOffset()); });
    }

    void Camera::processMouseMovement(float xOffset, float yOffset, bool constrainPitch) {
        xOffset *= mouseSensitivity;
        yOffset *= mouseSensitivity;

        yaw += xOffset * zoom;
        pitch += yOffset * zoom;

        // Constrain pitch to avoid flipping
        if (constrainPitch) {
            pitch = std::clamp(pitch, -89.0f, 89.0f);
        }

        // Update camera vectors based on updated Euler angles
        updateCameraVectors();
    }

    void Camera::processMouseScroll(float yOffset)
    {
        zoom -= yOffset * 0.25f;
        zoom = std::max(zoom, 0.25f);
        // Re-sync frustum bounds since the FOV changed
        updateCameraVectors();
    }

    void Camera::updateCameraVectors() {
        // 1. Compute clean spatial orientation changes from Euler angles
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);

        // 2. Standard Right-Handed System cross products:
        // If Y is World Up, cross(front, worldUp) generates a mathematically clean Right vector
        right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

        // Recalculate true Local Up vector seamlessly
        up = glm::normalize(glm::cross(right, front));

        // 3. Keep your Frustum accurately locked to the unified matrix viewports
        frustum.update(getProjectionMatrix(1280.0f / 720.0f) * getViewMatrix());
    }


    glm::mat4 Camera::getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    glm::mat4 Camera::getProjectionMatrix(float aspect, float nearPlane, float farPlane) const {
        // Fix: Zoom should dynamically adjust the vertical FOV angle!
        float dynamicFOV = glm::radians(45.0f / zoom);

        glm::mat4 proj = glm::perspective(dynamicFOV, aspect, nearPlane, farPlane);
        //proj[1][1] *= -1; // Safe Vulkan Y-flip mapping configuration
        return proj;
    }

    void Camera::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);

        // Dispatch returns true to mark the event handled if processed
        dispatcher.Dispatch<MouseMovedEvent>([this](const MouseMovedEvent& ev) { return this->OnMouseMoved(ev); });
        dispatcher.Dispatch<MouseScrolledEvent>([this](const MouseScrolledEvent& ev) { return this->OnMouseScrolled(ev); });
    }

    bool Camera::OnMouseMoved(const MouseMovedEvent& e)
    {
        // Local tracking state handles window focus transitions safely
        static bool firstMouseFrame = true;
        static float lastX = 0.0f, lastY = 0.0f;

        // Reinitialize baselines instantly on the very first frame of entering lock mode
        if (firstMouseFrame) {
            lastX = e.GetX();
            lastY = e.GetY();
            firstMouseFrame = false;
            return true;
        }

        float xOffset = e.GetX() - lastX;
        float yOffset = lastY - e.GetY(); // Inverted screen coordinate space map

        lastX = e.GetX();
        lastY = e.GetY();

        xOffset *= mouseSensitivity;
        yOffset *= mouseSensitivity;

        yaw += xOffset;
        pitch += yOffset;

        // Apply viewport bounds clamp protection filters
        pitch = std::clamp(pitch, -89.0f, 89.0f);

        // Compute updated geometric directional viewing vectors
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);

        right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        up = glm::normalize(glm::cross(right, front));
        return true;
    }

    bool Camera::OnMouseScrolled(const MouseScrolledEvent& e)
    {
        zoom -= e.GetYOffset() * 0.25f;
        zoom = std::max(zoom, 0.25f);
        return true;
    }

    void Camera::ProcessKeyboardMovement(CameraMovement direction, float deltaTime)
    {
        float velocity = movementSpeed * deltaTime;

        switch (direction) {
            case CameraMovement::FORWARD:  position += front * velocity; break;
            case CameraMovement::BACKWARD: position -= front * velocity; break;
            case CameraMovement::LEFT:     position -= right * velocity; break;
            case CameraMovement::RIGHT:    position += right * velocity; break;
            case CameraMovement::UP:       position += up * velocity;    break;
            case CameraMovement::DOWN:     position -= up * velocity;    break;
        }
    }

    void Frustum::update(const glm::mat4& viewProj)
    {
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

        // Normalize the planes so the dot product yields real world distance metrics
        for (auto& plane : m_Planes) {
            float length = glm::length(glm::vec3(plane));
            plane /= length;
        }
    }

    bool Frustum::Intersects(const BoundingBox& box) const
    {
        // Access bounds directly from your BoundingBox layout structure
        glm::vec3 minBound = box.minBound;
        glm::vec3 maxBound = box.maxBound;

        for (const auto& plane : m_Planes) {
            glm::vec3 positiveVertex = minBound;
            if (plane.x >= 0) positiveVertex.x = maxBound.x;
            if (plane.y >= 0) positiveVertex.y = maxBound.y;
            if (plane.z >= 0) positiveVertex.z = maxBound.z;

            // If the positive vertex is outside the plane, the box is completely culled
            if (glm::dot(glm::vec3(plane), positiveVertex) + plane.w < 0.0f) {
                return false;
            }
        }
        return true; // Box intersects or lies inside the frustum safely!
    }

    void Plane::normalize()
    {
        float length = glm::length(normal);
        normal /= length;
        distance /= length;
    }

}