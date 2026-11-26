#pragma once

#include "Core/Timestep.hpp"
#include "Events/Event.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Proto {

    class Scene;
    class Entity;

    enum class CameraPreset {
        PerspectiveFree = 0,
        Top,
        Bottom,
        Front,
        Back,
        Right,
        Left,
        Isometric
    };

    class EditorCameraController {
    public:
        void OnUpdate(Timestep ts, Scene& scene);
        void OnEvent(Event& e, Scene& scene);

        void ApplyPreset(CameraPreset preset, Scene& scene, bool useOrthographic = true);
        void ToggleProjectionMode(Scene& scene);

        float GetOrbitDistance() const { return m_OrbitDistance; }
    private:
        bool OnKeyPressed(const KeyPressEvent& e, Scene& scene);
        bool OnMouseButtonPressed(const MouseButtonPressedEvent& e, Scene& scene);
        bool OnMouseButtonReleased(const MouseButtonReleasedEvent& e);
        bool OnMouseMoved(const MouseMovedEvent& e, Scene& scene);
        bool OnMouseScrolled(const MouseScrolledEvent& e, Scene& scene);

        Entity* FindActiveCameraEntity(Scene& scene) const;
        bool IsCursorInViewport(const glm::vec2& cursorPos) const;
        void ComputeEntityFocusBounds(Entity* ent, glm::vec3& outCenter, float& outRadius) const;

    private:
        glm::vec3 m_OrbitPivot = glm::vec3(0.0f, 0.0f, -2.0f);
        float     m_OrbitDistance = 8.0f;
        bool      m_IsFraming = false;
        glm::vec3 m_FrameTargetPos = glm::vec3(0.0f);
        glm::quat m_FrameTargetRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        bool      m_IsOrbiting = false;
        bool      m_IsPanning = false;
        bool      m_IsRightDragging = false;
        glm::vec2 m_LastMousePos = glm::vec2(0.0f);
        bool      m_PrevAutoTurntable = false;
    };

}