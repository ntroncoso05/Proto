#include "engine.hpp"
#include "HelpersGLFW.hpp"
#include <stdexcept>

namespace Core {
    void Engine::Initialize() {
        //glfwInit();
        //glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Disable OpenGL
        //window = glfwCreateWindow(800, 600, "Fuse Engine", nullptr, nullptr);
        uint32_t width = 1280;
        uint32_t height = 800;
        window = initWindow("GLFW example", width, height);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Fuse Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        createInfo.enabledExtensionCount = glfwExtensionCount;
        createInfo.ppEnabledExtensionNames = glfwExtensions;

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan instance!");
        }
    }

    void Engine::Run() {
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
        }
    }

    void Engine::CleanUp() {
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
}