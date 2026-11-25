#pragma once
#include "Core/Window.hpp"
#include "Renderer/GraphicsContext.hpp"
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <glm/glm.hpp>

namespace Proto {
    class AndroidWindow : public Window {
    public:
        explicit AndroidWindow(const WindowProps& props);
        ~AndroidWindow() override;

        void OnUpdate() override;
        unsigned int GetWidth() const override { return m_Width; }
        unsigned int GetHeight() const override { return m_Height; }

        void SetEventCallback(const EventCallbackFn& callback) override { m_EventCallback = callback; }
        void SetVSync(bool enabled) override { m_VSync = enabled; }
        bool IsVSync() const override { return m_VSync; }
        void* GetNativeWindow() const override { return m_NativeWindow; }

        static void SetAndroidApp(android_app* app) { s_AndroidApp = app; }
        static android_app* GetAndroidApp() { return s_AndroidApp; }

        static bool IsTouchDown() { return s_TouchDown; }
        static glm::vec2 GetTouchPos() { return s_TouchPos; }

        void RecreateSurface();
    private:
        void Init(const WindowProps& props);
        void createSurface();

        static inline android_app* s_AndroidApp = nullptr;
        ANativeWindow* m_NativeWindow = nullptr;
        GraphicsContext* m_Context = nullptr;
        EventCallbackFn m_EventCallback;
        unsigned int m_Width = 1280;
        unsigned int m_Height = 720;
        bool m_VSync = true;

        static inline bool s_TouchDown = false;
        static inline glm::vec2 s_TouchPos{ 0.0f, 0.0f };
    };
}
