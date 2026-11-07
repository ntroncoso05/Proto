#pragma once

#include "Core.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/ApplicationEvent.hpp"
#include <GLFW/glfw3.h>
#include "KeyCodes.hpp"

namespace Proto {

    // Input system that generates events
    class Input {
    private:
        EventBus& eventBus;

        // Key states
        std::unordered_map<int, bool> keyStates;

    public:
        explicit Input(EventBus& bus) : eventBus(bus) {}

        void Update() {
            // Poll input events from the platform
            glfwPollEvents();
            // ...

            // Example: Process a key press
            //ProcessKeyPress(Key::W, false);
        }

        void ProcessKeyPress(int keyCode, bool repeat) {
            bool& keyState = keyStates[keyCode];
            PROTO_INFO("W Press!");
            if (!keyState || repeat) {
                // Key was not pressed before or this is a repeat
                KeyPressEvent event(keyCode, repeat);
                eventBus.PublishEvent(event);
            }

            keyState = true;
        }

        void ProcessKeyRelease(int keyCode) {
            bool& keyState = keyStates[keyCode];

            if (keyState) {
                // Key was pressed before
                KeyReleasedEvent event(keyCode);
                eventBus.PublishEvent(event);
            }

            keyState = false;
        }

        void ProcessWindowResize(int w, int h) {
            WindowResizeEvent event(w, h);
            eventBus.PublishEvent(event);
        }

        void ProcessWindowClose() {
            WindowCloseEvent event;
            eventBus.PublishEvent(event);
        }
    };

	//class Input
	//{
	//public:
	//	inline static bool IsKeyPressed(int keycode) { return s_Instance->IsKeyPressedImpl(keycode); }
	//
	//	inline static bool IsMouseButtonPressed(int button) { return s_Instance->IsMouseButtonPressedImpl(button); }
	//	inline static std::pair<float, float> GetMousePosition() { return s_Instance->GetMousePositionImpl(); }
	//	inline static float GetMouseX() { return s_Instance->GetMouseXImpl(); }
	//	inline static float GetMouseY() { return s_Instance->GetMouseYImpl(); }
	//protected:
	//	virtual bool IsKeyPressedImpl(int keycode) = 0;
	//
	//	virtual bool IsMouseButtonPressedImpl(int button) = 0;
	//	virtual std::pair<float, float> GetMousePositionImpl() = 0;
	//	virtual float GetMouseXImpl() = 0;
	//	virtual float GetMouseYImpl() = 0;
	//private:
	//	static Input* s_Instance;
	//};

}