#include "protopch.hpp"
#include "AndroidWindow.hpp"
#include "Events/ApplicationEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Core/Log.hpp"
#include "Core/Application.hpp"
#include "Core/ServiceLocator.hpp"
#include "Platform/Vulkan/VK_Context.hpp"
#include <android/input.h>
#include <imgui.h>

namespace Proto {

    Window* Window::Create(const WindowProps& props)
    {
        return new AndroidWindow(props);
    }

    AndroidWindow::AndroidWindow(const WindowProps& props)
    {
        Init(props);
    }

    AndroidWindow::~AndroidWindow()
    {
        delete m_Context;
    }

    void AndroidWindow::createSurface()
    {
        auto* context = static_cast<VulkanContext*>(m_Context);
        vk::AndroidSurfaceCreateInfoKHR surfaceInfo{ .window = m_NativeWindow };
        context->SetSurface(context->GetInstance().createAndroidSurfaceKHR(surfaceInfo));
    }

    void AndroidWindow::Init(const WindowProps& props)
    {
        PROTO_CORE_ASSERT(s_AndroidApp && s_AndroidApp->window, "Android ANativeWindow is null!");
        m_NativeWindow = s_AndroidApp->window;
        m_Width = static_cast<unsigned int>(ANativeWindow_getWidth(m_NativeWindow));
        m_Height = static_cast<unsigned int>(ANativeWindow_getHeight(m_NativeWindow));

        s_AndroidApp->userData = this;
        android_app_set_motion_event_filter(s_AndroidApp, [](const GameActivityMotionEvent*) -> bool {
            return true;
        });
        s_AndroidApp->onAppCmd = [](android_app* app, int32_t cmd) {
            auto* win = static_cast<AndroidWindow*>(app->userData);
            if (!win) return;
            if (cmd == APP_CMD_TERM_WINDOW || cmd == APP_CMD_DESTROY) {
                WindowCloseEvent ev;
                Application::Get().OnEvent(ev);
            }
            else if (cmd == APP_CMD_WINDOW_RESIZED && app->window) {
                win->m_Width = static_cast<unsigned int>(ANativeWindow_getWidth(app->window));
                win->m_Height = static_cast<unsigned int>(ANativeWindow_getHeight(app->window));
                WindowResizeEvent ev(win->m_Width, win->m_Height);
                Application::Get().OnEvent(ev);
            }
            };

        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
        };
        m_Context = new VulkanContext(m_NativeWindow, extensions);
        ServiceLocator::Register<VulkanContext>(static_cast<VulkanContext*>(m_Context));
        m_Context->Init();
        createSurface();
    }

    void AndroidWindow::RecreateSurface() {
        if (s_AndroidApp && s_AndroidApp->window) {
            m_NativeWindow = s_AndroidApp->window;
            m_Width = static_cast<unsigned int>(ANativeWindow_getWidth(m_NativeWindow));
            m_Height = static_cast<unsigned int>(ANativeWindow_getHeight(m_NativeWindow));
            createSurface();
        }
    }

    void AndroidWindow::OnUpdate()
    {
        if (m_NativeWindow) {
            m_Width = static_cast<unsigned int>(ANativeWindow_getWidth(m_NativeWindow));
            m_Height = static_cast<unsigned int>(ANativeWindow_getHeight(m_NativeWindow));
        }

        static int touchClickStage = 0; // 0 = idle, 1 = send DOWN, 2 = send UP
        ImGuiIO& io = ImGui::GetIO();

        if (touchClickStage == 1) {
            io.AddMouseButtonEvent(0, true);
            touchClickStage = 2;
        }
        else if (touchClickStage == 2) {
            io.AddMouseButtonEvent(0, false);
            touchClickStage = 0;
        }

        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(s_AndroidApp, source);
        }

        if (s_AndroidApp) {
            android_input_buffer* inputBuffer = android_app_swap_input_buffers(s_AndroidApp);
            if (inputBuffer) {
                for (size_t i = 0; i < inputBuffer->motionEventsCount; ++i) {
                    GameActivityMotionEvent& motion = inputBuffer->motionEvents[i];
                    int action = motion.action & AMOTION_EVENT_ACTION_MASK;
                    float x = GameActivityPointerAxes_getX(&motion.pointers[0]);
                    float y = GameActivityPointerAxes_getY(&motion.pointers[0]);
                    s_TouchPos = { x, y };
                                        
                    float imX = x, imY = y;
                    if (m_Width > 0 && m_Height > 0) {
                        bool winPortrait = (m_Width < m_Height);
                        bool dispLandscape = (io.DisplaySize.x > io.DisplaySize.y);
                        if (winPortrait && dispLandscape) {
                            imX = (1.0f - (y / static_cast<float>(m_Height))) * io.DisplaySize.x;
                            imY = (x / static_cast<float>(m_Width)) * io.DisplaySize.y;
                        }
                        else {
                            imX = x * (io.DisplaySize.x / static_cast<float>(m_Width));
                            imY = y * (io.DisplaySize.y / static_cast<float>(m_Height));
                        }
                    }
                    s_TouchPos = { imX, imY };
                    io.MousePos = ImVec2(imX, imY);
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                    io.AddMousePosEvent(imX, imY);

                    if (action == AMOTION_EVENT_ACTION_DOWN) {
                        s_TouchDown = true;
                        touchClickStage = 1; // Hover this frame -> Click next frame!
                        MouseButtonPressedEvent pressEv(0);
                        Application::Get().OnEvent(pressEv);
                    }
                    else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
                        s_TouchDown = false;
                        if (touchClickStage == 0) {
                            io.AddMouseButtonEvent(0, false);
                        }
                        MouseButtonReleasedEvent relEv(0);
                        Application::Get().OnEvent(relEv);
                    }

                    MouseMovedEvent moveEv(imX, imY);
                    Application::Get().OnEvent(moveEv);
                }
                android_app_clear_motion_events(inputBuffer);
            }
        }
    }
}