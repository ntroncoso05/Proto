#include "protopch.hpp"
#include "Renderer/EditorCameraController.hpp"
#include "Core/Input.hpp"
#include "Core/Scene.hpp"
#include "Core/Entity.hpp"
#include "Core/ComponentTypes.hpp"
#include "Platform/Vulkan/VK_Renderer.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Proto {

    Entity* EditorCameraController::FindActiveCameraEntity(Scene& scene) const
    {
        for (const auto& entPtr : scene.GetEntities()) {
            if (entPtr && entPtr->IsActive() && entPtr->GetComponent<CameraComponent>()) {
                return entPtr.get();
            }
        }
        return nullptr;
    }

    bool EditorCameraController::IsCursorInViewport(const glm::vec2& cursorPos) const
    {
        auto imGuiUtil = VulkanRenderer::Get().imGui;
        if (!imGuiUtil) return false;
        glm::vec2 vpMin = imGuiUtil->GetViewportMin();
        glm::vec2 vpSize = imGuiUtil->GetViewportSize();
        return cursorPos.x >= vpMin.x && cursorPos.x <= vpMin.x + vpSize.x &&
            cursorPos.y >= vpMin.y && cursorPos.y <= vpMin.y + vpSize.y;
    }

    void EditorCameraController::ComputeEntityFocusBounds(Entity* ent, glm::vec3& outCenter, float& outRadius) const
    {
        auto* t = ent->GetComponent<TransformComponent>();
        outCenter = t ? glm::vec3(t->GetWorldTransformMatrix()[3]) : glm::vec3(0.0f);
        outRadius = 2.0f;

        if (auto* mc = ent->GetComponent<ModelComponent>(); mc && mc->GetModelHandle().IsValid() && t) {
            Model* model = mc->GetModelHandle().Get();
            if (model && !model->nodes.empty()) {
                glm::vec3 minB(std::numeric_limits<float>::max());
                glm::vec3 maxB(std::numeric_limits<float>::lowest());
                bool hasVerts = false;

                std::function<void(Node*, const glm::mat4&)> traverse = [&](Node* node, const glm::mat4& parentMat) {
                    if (!node) return;
                    glm::mat4 worldNodeMat = parentMat * node->getLocalMatrix();
                    if (node->mesh) {
                        for (const auto& v : node->mesh->GetRawVertices()) {
                            glm::vec3 p = glm::vec3(worldNodeMat * glm::vec4(v.pos, 1.0f));
                            minB = glm::min(minB, p);
                            maxB = glm::max(maxB, p);
                            hasVerts = true;
                        }
                    }
                    for (auto* child : node->children) traverse(child, worldNodeMat);
                    };
                for (auto* root : model->nodes) traverse(root, glm::mat4(1.0f));

                if (hasVerts) {
                    BoundingBox worldBox(minB, maxB);
                    worldBox.Transform(t->GetWorldTransformMatrix());
                    outCenter = (worldBox.minBound + worldBox.maxBound) * 0.5f;
                    outRadius = std::max(glm::length(worldBox.maxBound - worldBox.minBound) * 0.5f, 0.75f);
                }
            }
        }
    }

    void EditorCameraController::OnUpdate(Timestep ts, Scene& scene)
    {
        Entity* cameraEntity = FindActiveCameraEntity(scene);
        if (!cameraEntity) return;

        auto* transform = cameraEntity->GetComponent<TransformComponent>();
        auto* camComp = cameraEntity->GetComponent<CameraComponent>();
        auto  imGuiUtil = VulkanRenderer::Get().imGui;
        if (!transform || !camComp || !imGuiUtil) return;

        // 1. Continuous WASD / QE Flight (disabled while typing in UI fields)
        if (!ImGui::GetIO().WantTextInput) {
            float speed = camComp->MovementSpeed * static_cast<float>(ts);
            glm::vec3 pos = transform->GetPosition();
            glm::quat rot = transform->GetRotation();
            glm::vec3 forward = rot * glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec3 right = rot * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 up = rot * glm::vec3(0.0f, 1.0f, 0.0f);

            if (Input::IsKeyPressed(Key::W)) pos += forward * speed;
            if (Input::IsKeyPressed(Key::S)) pos -= forward * speed;
            if (Input::IsKeyPressed(Key::A)) pos -= right * speed;
            if (Input::IsKeyPressed(Key::D)) pos += right * speed;
            if (Input::IsKeyPressed(Key::Q)) pos += up * speed;
            if (Input::IsKeyPressed(Key::E)) pos -= up * speed;

            transform->SetPosition(pos);
        }

        // 2. Smooth 'F' Framing Interpolation
        if (m_IsFraming) {
            float blend = 1.0f - std::exp(-12.0f * static_cast<float>(ts));
            glm::vec3 newPos = glm::mix(transform->GetPosition(), m_FrameTargetPos, blend);
            glm::quat newRot = glm::slerp(transform->GetRotation(), m_FrameTargetRot, blend);
            transform->SetPosition(newPos);
            transform->SetRotation(glm::normalize(newRot));

            if (glm::distance(newPos, m_FrameTargetPos) < 0.01f) {
                transform->SetPosition(m_FrameTargetPos);
                transform->SetRotation(m_FrameTargetRot);
                m_IsFraming = false;
            }
        }

        // 3. Auto-Turntable Showcase Spin
        Entity* selected = imGuiUtil->GetSelectedEntity();
        if (imGuiUtil->autoTurntable && !m_PrevAutoTurntable && selected && selected != cameraEntity) {
            float dummyR = 2.0f;
            ComputeEntityFocusBounds(selected, m_OrbitPivot, dummyR);
            m_OrbitDistance = std::max(glm::distance(transform->GetPosition(), m_OrbitPivot), 0.75f);
        }
        m_PrevAutoTurntable = imGuiUtil->autoTurntable;

        if (imGuiUtil->autoTurntable && !m_IsOrbiting && !m_IsPanning && !m_IsFraming) {
            float angleRad = glm::radians(imGuiUtil->turntableSpeed) * static_cast<float>(ts);
            glm::quat qSpin = glm::angleAxis(angleRad, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat newRot = glm::normalize(qSpin * transform->GetRotation());
            glm::vec3 newFwd = newRot * glm::vec3(0.0f, 0.0f, -1.0f);

            transform->SetRotation(newRot);
            transform->SetPosition(m_OrbitPivot - newFwd * m_OrbitDistance);
        }
    }

    void EditorCameraController::OnEvent(Event& e, Scene& scene)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<KeyPressEvent>([&](const KeyPressEvent& ev) { return OnKeyPressed(ev, scene); });
        dispatcher.Dispatch<MouseButtonPressedEvent>([&](const MouseButtonPressedEvent& ev) { return OnMouseButtonPressed(ev, scene); });
        dispatcher.Dispatch<MouseButtonReleasedEvent>([&](const MouseButtonReleasedEvent& ev) { return OnMouseButtonReleased(ev); });
        dispatcher.Dispatch<MouseMovedEvent>([&](const MouseMovedEvent& ev) { return OnMouseMoved(ev, scene); });
        dispatcher.Dispatch<MouseScrolledEvent>([&](const MouseScrolledEvent& ev) { return OnMouseScrolled(ev, scene); });
    }

    bool EditorCameraController::OnKeyPressed(const KeyPressEvent& e, Scene& scene)
    {
        if (e.IsRepeat() || ImGui::GetIO().WantTextInput) return false;

        auto imGuiUtil = VulkanRenderer::Get().imGui;
        Entity* cameraEntity = FindActiveCameraEntity(scene);
        if (!imGuiUtil || !cameraEntity) return false;

        auto* transform = cameraEntity->GetComponent<TransformComponent>();
        auto* camComp = cameraEntity->GetComponent<CameraComponent>();
        Entity* selected = imGuiUtil->GetSelectedEntity();

        if (e.GetKeyCode() == Key::F && selected && selected != cameraEntity && transform && camComp) {
            glm::vec3 center;
            float radius;
            ComputeEntityFocusBounds(selected, center, radius);

            m_OrbitPivot = center;
            float halfFov = glm::radians(camComp->GetFOV() * 0.5f);
            m_OrbitDistance = std::clamp((radius / std::max(std::sin(halfFov), 0.15f)) * 1.15f, 1.5f, 60.0f);

            glm::vec3 viewDir = glm::normalize(transform->GetRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
            m_FrameTargetPos = m_OrbitPivot - viewDir * m_OrbitDistance;
            m_FrameTargetRot = glm::quatLookAt(viewDir, glm::vec3(0.0f, 1.0f, 0.0f));
            m_IsFraming = true;
            return true;
        }

        if (e.GetKeyCode() == Key::Tab) {
            imGuiUtil->autoTurntable = !imGuiUtil->autoTurntable;
            return true;
        }

        return false;
    }

    bool EditorCameraController::OnMouseButtonPressed(const MouseButtonPressedEvent& e, Scene& scene)
    {
        glm::vec2 cursorPos = Input::GetMousePosition();
        if (!IsCursorInViewport(cursorPos)) return false;

        m_LastMousePos = cursorPos;
        bool altDown = Input::IsKeyPressed(Key::LeftAlt) || Input::IsKeyPressed(Key::RightAlt);

        if (altDown && (e.GetMouseButton() == Mouse::ButtonLeft || e.GetMouseButton() == Mouse::ButtonMiddle)) {
            m_IsFraming = false;
            m_IsOrbiting = (e.GetMouseButton() == Mouse::ButtonLeft);
            m_IsPanning = (e.GetMouseButton() == Mouse::ButtonMiddle);

            Entity* cameraEntity = FindActiveCameraEntity(scene);
            auto imGuiUtil = VulkanRenderer::Get().imGui;
            if (cameraEntity && imGuiUtil) {
                Entity* selected = imGuiUtil->GetSelectedEntity();
                if (selected && selected != cameraEntity) {
                    float dummyR = 2.0f;
                    ComputeEntityFocusBounds(selected, m_OrbitPivot, dummyR);
                }
                if (auto* t = cameraEntity->GetComponent<TransformComponent>()) {
                    m_OrbitDistance = std::max(glm::distance(t->GetPosition(), m_OrbitPivot), 0.75f);
                }
            }
            return true;
        }

        if (e.GetMouseButton() == Mouse::ButtonRight) {
            m_IsRightDragging = true;
            return true;
        }

        return false;
    }

    bool EditorCameraController::OnMouseButtonReleased(const MouseButtonReleasedEvent& e)
    {
        if (e.GetMouseButton() == Mouse::ButtonLeft)   m_IsOrbiting = false;
        if (e.GetMouseButton() == Mouse::ButtonMiddle) m_IsPanning = false;
        if (e.GetMouseButton() == Mouse::ButtonRight)  m_IsRightDragging = false;
        return false;
    }
    bool EditorCameraController::OnMouseMoved(const MouseMovedEvent& e, Scene& scene)
    {
        glm::vec2 currPos(e.GetX(), e.GetY());
        glm::vec2 delta = currPos - m_LastMousePos;
        m_LastMousePos = currPos;
        if (!m_IsOrbiting && !m_IsPanning && !m_IsRightDragging) return false;
        Entity* cameraEntity = FindActiveCameraEntity(scene);
        if (!cameraEntity) return false;
        auto* transform = cameraEntity->GetComponent<TransformComponent>();
        auto* camComp = cameraEntity->GetComponent<CameraComponent>();
        if (!transform) return false;
        if (m_IsOrbiting) {
            float orbitSpeed = 0.0065f;
            glm::quat qYaw = glm::angleAxis(-delta.x * orbitSpeed, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat yawed = glm::normalize(qYaw * transform->GetRotation());
            glm::quat qPitch = glm::angleAxis(-delta.y * orbitSpeed, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::quat cand = glm::normalize(yawed * qPitch);
            glm::vec3 fwd = cand * glm::vec3(0.0f, 0.0f, -1.0f);
            glm::quat finalRot = (std::abs(fwd.y) < 0.995f) ? cand : yawed;
            glm::vec3 finalFwd = finalRot * glm::vec3(0.0f, 0.0f, -1.0f);
            transform->SetRotation(finalRot);
            transform->SetPosition(m_OrbitPivot - finalFwd * m_OrbitDistance);
            return true;
        }
        if (m_IsPanning) {
            float panScale = m_OrbitDistance * 0.0015f;
            glm::vec3 camRight = transform->GetRotation() * glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 camUp = transform->GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 panDelta = (-camRight * delta.x + camUp * delta.y) * panScale;
            m_OrbitPivot += panDelta;
            transform->SetPosition(transform->GetPosition() + panDelta);
            return true;
        }
        if (m_IsRightDragging) {
            float sensitivity = camComp ? camComp->MouseSensitivity : 0.1f;
            float deltaYaw = glm::radians(-delta.x * sensitivity);
            float deltaPitch = glm::radians(-delta.y * sensitivity);
            glm::quat currentRot = transform->GetRotation();
            glm::quat qYaw = glm::angleAxis(deltaYaw, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat yawed = glm::normalize(qYaw * currentRot);
            glm::quat cand = glm::normalize(yawed * glm::angleAxis(deltaPitch, glm::vec3(1.0f, 0.0f, 0.0f)));
            glm::vec3 fwd = cand * glm::vec3(0.0f, 0.0f, -1.0f);
            transform->SetRotation((std::abs(fwd.y) < 0.9998f) ? cand : yawed);
            return true;
        }
        return false;
    }
    bool EditorCameraController::OnMouseScrolled(const MouseScrolledEvent& e, Scene& scene)
    {
        if (!IsCursorInViewport(Input::GetMousePosition())) return false;
        Entity* cameraEntity = FindActiveCameraEntity(scene);
        auto imGuiUtil = VulkanRenderer::Get().imGui;
        if (!cameraEntity || !imGuiUtil) return false;
        if (auto* camComp = cameraEntity->GetComponent<CameraComponent>()) {
            camComp->SetFOV(std::clamp(camComp->GetFOV() - e.GetYOffset() * 2.0f, 1.0f, 90.0f));
            glm::vec2 vpSize = imGuiUtil->GetViewportSize();
            if (vpSize.x > 0.0f && vpSize.y > 0.0f) {
                camComp->SetPerspective(camComp->GetFOV(), vpSize.x / vpSize.y, 0.1f, 100.0f);
            }
            return true;
        }
        return false;
    }
}