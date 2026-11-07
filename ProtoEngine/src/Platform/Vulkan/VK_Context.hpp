#pragma once
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan_raii.hpp>
#include "Proto/Renderer/GraphicsContext.hpp"

struct GLFWwindow;

namespace Proto
{

    class VulkanDevice;

    class VulkanContext : public GraphicsContext {
    public:
        VulkanContext(GLFWwindow* windowHandle, const std::vector<const char*>& requiredExtensions);
        ~VulkanContext() = default;

        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        virtual void Init() override;
        void SwapBuffers() override;

        const vk::raii::Instance& GetInstance() const { return m_Instance; }
        const vk::raii::SurfaceKHR& GetSurface() const { return m_Surface; }
        VulkanDevice& GetDevice() const { return *m_VulkanDevice; }
        GLFWwindow* GetWindow() const { return m_Window; }

        void SetSurface(vk::raii::SurfaceKHR _surface) { m_Surface = std::move(_surface); }

        void CreateDevice();
        bool framebufferResized = false;

    private:
        void CreateInstance();
        void SetupDebugMessenger();

        GLFWwindow* m_Window;
        vk::raii::Context m_Context;
        vk::raii::Instance m_Instance = nullptr;
        vk::raii::DebugUtilsMessengerEXT m_DebugMessenger = nullptr;
        vk::raii::SurfaceKHR m_Surface = nullptr;
        std::vector<const char*> m_RequiredExtensions;

        std::unique_ptr<VulkanDevice> m_VulkanDevice;
    };

}