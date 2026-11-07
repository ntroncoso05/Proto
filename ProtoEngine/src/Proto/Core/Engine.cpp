/*#include "engine.hpp"

#include <GLFW/glfw3.h>
#include "log.hpp"

#include "Events/event.hpp"
#include "Events/application_event.hpp"
namespace Proto {

    void Engine::Initialize() {
		Proto::Log::Init();
		PROTO_CORE_WARN("Engine!");
		int a = 5;
		PROTO_INFO("App! Var={0}", a);
		WindowResizeEvent e(1820, 900);
		PROTO_CORE_TRACE(e);

		renderer = new VulkanRenderer();
        renderer->initVulkan();
    }

    void Engine::Run() {
		while (!glfwWindowShouldClose(renderer->platform->window))
		{
			glfwPollEvents();
			renderer->drawFrame();
		}
		renderer->vulkanContext->getDevice().logicalDevice.waitIdle();        // wait for device to finish operations before destroying resources
    }

    void Engine::CleanUp() {
		renderer->platform->CleanUp();
    }

}*/