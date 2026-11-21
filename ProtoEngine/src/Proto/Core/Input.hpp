#pragma once

#include "Core/KeyCodes.hpp"
#include "Core/MouseCodes.hpp"
#include <glm/glm.hpp>

namespace Proto {

    class Input {
    public:
        // EventBus& eventBus;

        static bool IsKeyPressed(KeyCode key);
        static bool IsMouseButtonPressed(MouseCode button);
        static glm::vec2 GetMousePosition();
        static float GetMouseX();
        static float GetMouseY();
    };

}