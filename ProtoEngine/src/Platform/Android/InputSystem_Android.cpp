#include "protopch.hpp"
#include "Core/InputSystem.hpp"
#include <imgui.h>

namespace Proto {

    InputState InputSystem::inputState;
    std::unordered_map<InputAction, std::function<void(float)>> InputSystem::actionCallbacks;

    void InputSystem::Initialize(void* window) {}
    void InputSystem::Shutdown() {}

    void InputSystem::Update(float deltaTime) {
        inputState.resetDeltas();
    }

    void InputSystem::RegisterActionCallback(InputAction action, std::function<void(float)> callback) {
        actionCallbacks[action] = std::move(callback);
    }

    bool InputSystem::ProcessInputEvent(void* event) {
        return true;
    }

    InputState& InputSystem::GetInputState() {
        return inputState;
    }

    bool InputSystem::IsImGuiCapturingKeyboard() {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool InputSystem::IsImGuiCapturingMouse() {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool InputSystem::IsActionActive(InputAction inputAction) {
        return false;
    }
}