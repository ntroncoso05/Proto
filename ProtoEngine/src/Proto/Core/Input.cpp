#include "protopch.hpp"
#include "Core/Input.hpp"
#include "Core/Application.hpp"

#ifndef PROTO_PLATFORM_ANDROID
    #include <GLFW/glfw3.h>
#endif

#ifdef PROTO_PLATFORM_ANDROID
    #include "Platform/Android/AndroidWindow.hpp"
#endif

namespace Proto {

    bool Input::IsKeyPressed(KeyCode key)
    {
#ifndef PROTO_PLATFORM_ANDROID
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (!window) return false;
        int state = glfwGetKey(window, static_cast<int>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
#else
        return false;
#endif
    }

    bool Input::IsMouseButtonPressed(MouseCode button)
    {
#ifndef PROTO_PLATFORM_ANDROID
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (!window) return false;
        int state = glfwGetMouseButton(window, static_cast<int>(button));
        return state == GLFW_PRESS;
#else
        return (button == 0) && AndroidWindow::IsTouchDown();
#endif
    }

    glm::vec2 Input::GetMousePosition()
    {
#ifndef PROTO_PLATFORM_ANDROID
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (!window) return { 0.0f, 0.0f };
        double xpos = 0.0, ypos = 0.0;
        glfwGetCursorPos(window, &xpos, &ypos);
        return { static_cast<float>(xpos), static_cast<float>(ypos) };
#else
        return AndroidWindow::GetTouchPos();
#endif
    }

    float Input::GetMouseX() { return GetMousePosition().x; }
    float Input::GetMouseY() { return GetMousePosition().y; }
}