#include "engine.hpp"

#include <GLFW/glfw3.h>

namespace Proto {

    void Engine::Initialize() {
		renderer = new VulkanRenderer();
        renderer->initVulkan();
    }

    void Engine::Run() {
		while (!glfwWindowShouldClose(renderer->platform->window))
		{
			glfwPollEvents();
			renderer->drawFrame();
		}
		renderer->vulkanDevice->logicalDevice.waitIdle();        // wait for device to finish operations before destroying resources
    }

    void Engine::CleanUp() {
		renderer->platform->CleanUp();
    }

}