#ifndef IMGUI_LAYER_H
#define IMGUI_LAYER_H

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdlrenderer3.h"

#include "Core/Layer.h"
#include "Events/Event.h"
#include "Core/Window.h"
#include "Core/GraphicsAPI.h"

namespace Nova::Core {

    struct ImGuiVulkanInitInfo {
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        uint32_t m_QueueFamily = (uint32_t)-1;
        VkQueue m_Queue = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        uint32_t m_MinImageCount = 2;
        uint32_t m_ImageCount;
        VkAllocationCallbacks* m_Allocator = nullptr;
    };

    class ImGuiLayer : public Layer {
    public:
        ImGuiLayer(Window& window, GraphicsAPI api);
        ~ImGuiLayer() = default;

        virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(Event& e) override {}

        void Begin();
        void End();

        void BlockEvents(bool block) { m_BlockEvents = block; }

        void ProcessSDLEvent(const SDL_Event& e);

        void SetImGuiBackend(GraphicsAPI api);

        // --- Vulkan-specific methods ---
        // Must be called by VK_Renderer before the layer is attached (or immediately after creation)
        void SetVulkanInitInfo(const ImGuiVulkanInitInfo& info);

        // must be called by the renderer each frame before ImGuiLayer::End()
        void SetVulkanCommandBuffer(VkCommandBuffer cmd) { m_CurrentCommandBuffer = cmd; }

    private:
        bool m_BlockEvents = true;
        Window& m_Window;
        GraphicsAPI m_GraphicsAPI;

        bool m_IsRendererInitialized = false;

        ImGuiVulkanInitInfo m_VulkanInitInfo{};
        VkCommandBuffer m_CurrentCommandBuffer = VK_NULL_HANDLE;
    };

} // namespace Nova::Core

#endif // IMGUI_LAYER_H