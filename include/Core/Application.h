#ifndef APPLICATION_H
#define APPLICATION_H

#include <SDL3/SDL.h>
#include <vector>
#include <memory>

#if defined(IMGUI_IMPL_OPENGL_ES2)
//#include <SDL3/SDL_opengles2.h>
#else
//#include <SDL3/SDL_opengl.h>
#endif

#include <glad/gl.h>

#include "Core/Window.h"
#include "Core/Layer.h"
#include "Events/Event.h"
#include "Events/ApplicationEvents.h"
#include "Events/InputEvents.h"
#include "Core/ImGuiLayer.h"
#include "Core/LayerStack.h"

namespace Nova::Core::Events {
    class Event;
    class WindowClosedEvent;
    class WindowResizeEvent;
}

namespace Nova::Core {

    class Application {
    public:
        explicit Application(const Window::WindowDesc& windowDesc);
        ~Application();

        void Run();

        Window& GetWindow() { return *m_Window; }
        LayerStack& GetLayerStack() { return m_LayerStack; }

        static Application& Get() { return *s_Instance; }

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