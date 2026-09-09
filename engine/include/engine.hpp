#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace Core {
    class Engine {
    public:
        void Initialize();
        void Run();
        void CleanUp();
    private:
        GLFWwindow* window = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
    };
}