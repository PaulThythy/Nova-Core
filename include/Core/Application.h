#ifndef APPLICATION_H
#define APPLICATION_H

#include <SDL3/SDL.h>
#include <vector>
#include <memory>

#include "Api.h"
#include "Core/Assert.h"
#include "Core/Window.h"
#include "Core/Layer.h"
#include "Events/Event.h"
#include "Events/ApplicationEvents.h"
#include "Events/InputEvents.h"
#include "Core/ImGuiLayer.h"
#include "Core/LayerStack.h"

namespace Nova::Core {

    class NV_API Application {
    public:
        explicit Application(const Window::WindowDesc& windowDesc);
        ~Application();

        void Run();

        Window& GetWindow() {
            NV_ASSERT_MSG(m_Window, "Application window is not initialized.");
            return *m_Window;
        }
        LayerStack& GetLayerStack() { return m_LayerStack; }
        ImGuiLayer& GetImGuiLayer() {
            NV_ASSERT_MSG(m_ImGuiLayer, "ImGui layer is not initialized.");
            return *m_ImGuiLayer;
        }

        static Application& Get() {
            NV_ASSERT_MSG(s_Instance, "Application instance is not initialized.");
            return *s_Instance;
        }

        void OnEvent(Events::Event& e);

    private:
        static Application* s_Instance;

        bool m_IsRunning;

        void InitEngine(const Window::WindowDesc* windowDesc = nullptr);
        void DestroyEngine();

        void InitWindow(const Window::WindowDesc& windowDesc);
        void DestroyWindow();

        bool OnWindowClose(WindowClosedEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);

        Window* m_Window = nullptr;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        LayerStack m_LayerStack;
    };

} // namespace Nova::Core

#endif // APPLICATION_H