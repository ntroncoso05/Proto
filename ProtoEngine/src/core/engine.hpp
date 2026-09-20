#pragma once
#include "vulkan/vulkan_renderer.hpp"

namespace Proto {
    class Engine {
    public:
        void Initialize();
        void Run();
        void CleanUp();
    private:
		VulkanRenderer* renderer;
    };
}