#pragma once
#include "Events/Event.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Proto
{
    enum class CameraMovement {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
        UP,
        DOWN
    };

    struct Plane {
        glm::vec3 normal = { 0.f, 1.f, 0.f };
        float distance = 0.f; // Distance from origin

        // Normalize the plane so that distance calculations match real-world units
        void normalize();

        // Get signed distance from plane to a point
        float getSignedDistance(const glm::vec3& point) const {
            return glm::dot(normal, point) + distance;
        }
    };

    struct BoundingBox; // Forward declaration

    struct Frustum {
        // A frustum is defined by 6 clip planes (Left, Right, Bottom, Top, Near, Far)
        std::array<glm::vec4, 6> m_Planes;

    public:
        Frustum() = default;
        ~Frustum() = default;

        // Extract planes from a row-major or column-major View-Projection matrix
        void update(const glm::mat4& viewProj);

        // Checks if an absolute AABB bounding box intersects or sits inside the viewing frustum
        bool Intersects(const BoundingBox& box) const;
    };

    class Camera {
    private:
        // Spatial positioning and orientation vectors
        // These form the camera's local coordinate system in world space
        glm::vec3 position{ 0.0f, 0.0f, -10.0f };     // Camera's location in world coordinates
        glm::vec3 front{ 0.0f, 0.0f, 1.0f };        // Forward direction (where camera is looking)
        glm::vec3 up{ 0.0f, 1.0f, 0.0f };           // Camera's local up direction (for roll control)
        glm::vec3 right{ 1.0f, 0.0f, 0.0f };        // Camera's local right direction (perpendicular to front and up)
        glm::vec3 worldUp;      // Global up vector reference (typically Y-axis)

        // Internal coordinate system maintenance
        // Ensures mathematical consistency when orientation changes occur
        void updateCameraVectors();

        // Rotation representation using Euler angles
        // Provides intuitive control while managing gimbal lock and other rotation complexities
        float yaw = -90.0f;              // Horizontal rotation around the world up-axis (left-right looking)
        float pitch = 0.0f;            // Vertical rotation around the camera's right axis (up-down looking)

        // User interaction and behavior parameters
        // These control how the camera responds to input and environmental factors
        float movementSpeed = 5.0f;    // Units per second for translation movement
        float mouseSensitivity = 0.1f; // Multiplier for mouse input to rotation angle conversion
        float zoom = 1.0f;             // Field of view control for perspective projection

        Frustum frustum;
    public:
        // Constructor with sensible defaults for common use cases
        // Provides flexibility while ensuring the camera starts in a predictable state
        Camera(
            glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f),  // Start at world origin
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),        // Y-axis as world up
            float yaw = -90.0f,                                 // Look along negative Z-axis (OpenGL convention)
            float pitch = 0.0f                                  // Level horizon
        );

        // Matrix generation for graphics pipeline integration
        // These methods bridge between the camera's spatial representation and GPU requirements
        glm::mat4 getViewMatrix() const;
        glm::mat4 getProjectionMatrix(float aspect, float nearPlane = 0.1f, float farPlane = 100.0f) const;
        Frustum GetFrustum() const { return frustum; }

        // Input processing methods for different interaction modalities
        // Each method handles a specific type of user input with appropriate transformations
        void processKeyboard(CameraMovement direction, float deltaTime);     // Keyboard-based translation
        void processMouseMovement(float xOffset, float yOffset, bool constrainPitch = true);  // Mouse-based rotation
        void processMouseScroll(float yOffset);                              // Scroll-based zoom control

        void processInput(GLFWwindow* window, std::shared_ptr<Camera> camera, float deltaTime);

        // Property access methods for external systems
        // Provide controlled access to internal state without exposing implementation details
        glm::vec3 getPosition() const { return position; }
        glm::vec3 getFront() const { return front; }
        float getZoom() const { return zoom; }

        // Mouse movement callback for continuous camera rotation
        // Manages state persistence and coordinate transformations for smooth rotation control
        void mouseCallback(double xpos, double ypos);

        // Scroll wheel callback for zoom control
        // Provides intuitive field-of-view adjustment through scroll wheel interaction
        void scrollCallback(double xoffset, double yoffset);

        // Input system initialization and callback registration
        // Establishes the connection between windowing system and camera control callbacks
        void setupInputCallbacks(Event& e);

        void setPosition(glm::vec3 pos) { position = pos; }
        void setYaw(float _yaw) { yaw = _yaw; }
        void setPitch(float _pitch) { pitch = _pitch; }
        float getMovementSpeed() { return movementSpeed; }
        void setMovementSpeed(float moveSpeed) { movementSpeed = moveSpeed; }
        float getMouseSensitivity() { return mouseSensitivity; }
        void setMouseSensitivity(float mouseSense) { mouseSensitivity = mouseSense; }
        void setZoom(float _zoom) { zoom = _zoom; }

        // Architecture Pipeline Entrypoints
        void OnEvent(Event& e);
        void ProcessKeyboardMovement(CameraMovement direction, float deltaTime);

    private:
        bool OnMouseMoved(const MouseMovedEvent& e);
        bool OnMouseScrolled(const MouseScrolledEvent& e);
    };

}