#include "Core/InputSystem.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>

namespace Proto
{
    // Store the GLFW window pointer
    static GLFWwindow* gWindow = nullptr;
    static bool mouseCaptureMode = false;
    // Define the static member variable here
    InputState InputSystem::inputState;
    std::unordered_map<InputAction, std::function<void(float)>> InputSystem::actionCallbacks;

    // GLFW callback functions
    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
        if (button >= 0 && button < 3) {
            InputState& state = InputSystem::GetInputState();
            state.mouseButtons[button] = action == GLFW_PRESS;
        }
    }

    static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
        InputState& state = InputSystem::GetInputState();

        // Calculate delta from last position
        glm::vec2 newPos(static_cast<float>(xpos), static_cast<float>(ypos));
        state.cursorDelta = newPos - state.cursorPosition;
        state.cursorPosition = newPos;
    }

    static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
        InputState& state = InputSystem::GetInputState();
        state.scrollDelta = static_cast<float>(yoffset);
    }

    void toggleInputMode();
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        // Map GLFW keys to our input actions
        if (action == GLFW_PRESS || action == GLFW_RELEASE) {
            bool pressed = (action == GLFW_PRESS);

            // Toggle mouse capture mode with Escape key
            if (key == GLFW_KEY_ESCAPE && pressed) {
                mouseCaptureMode = !mouseCaptureMode;
                toggleInputMode();
            }

            // Map other keys to actions
            // ...
        }
    }

    void InputSystem::Initialize(void* window) {
        gWindow = (GLFWwindow*)window;
        // Initially capture the cursor for camera control
        mouseCaptureMode = true;
    }

    void InputSystem::Update(float deltaTime) {
        // Poll for input events
        glfwPollEvents();

        // Update key states for continuous actions (like movement)
        if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS) {
            if (auto it = actionCallbacks.find(InputAction::MOVE_FORWARD); it != actionCallbacks.end()) {
                it->second(deltaTime);
            }
        }

        // ... other keys ...

        // Reset delta values after processing
        inputState.resetDeltas();
    }

    bool InputSystem::IsImGuiCapturingKeyboard() {
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool InputSystem::IsImGuiCapturingMouse() {
        return ImGui::GetIO().WantCaptureMouse;
    }

    // Current input mode
    static InputMode currentInputMode = InputMode::CAMERA_CONTROL;

    // Set the input mode
    void setInputMode(InputMode mode) {
        currentInputMode = mode;

        // Update platform-specific settings based on the mode
        // This example shows how to implement this with GLFW
        if (mode == InputMode::CAMERA_CONTROL) {
            // In GLFW, we can disable the cursor for camera control
            glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else {
            // For UI interaction, we want the cursor to be visible
            glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // With other windowing libraries, you would use their equivalent APIs
    }

    // Toggle between camera control and UI interaction modes
    void toggleInputMode() {
        if (currentInputMode == InputMode::CAMERA_CONTROL) {
            setInputMode(InputMode::UI_INTERACTION);
        }
        else {
            setInputMode(InputMode::CAMERA_CONTROL);
        }
    }

    void InputSystem::Shutdown(){}
    void InputSystem::RegisterActionCallback(InputAction action, std::function<void(float)> callback) {}
    bool InputSystem::ProcessInputEvent(void* event) { 
        return true; }
    InputState& InputSystem::GetInputState() { return InputSystem::inputState; }
    bool InputSystem::IsActionActive(InputAction inputAction)
    { 
        if (inputAction == InputAction::MOVE_FORWARD) return true;
        if (inputAction ==InputAction::MOVE_BACKWARD)return true;
        if (inputAction ==InputAction::MOVE_LEFT)return true;
        if (inputAction ==InputAction::MOVE_RIGHT)return true;
        if (inputAction ==InputAction::MOVE_UP)return true;
        if (inputAction ==InputAction::MOVE_DOWN)return true;

        return false; 
    }

}