#ifndef APPLICATION_H
#define APPLICATION_H

#include <SDL3/SDL.h>
#include <vector>
#include <memory>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "Core/Window.h"
#include "Core/Layer.h"
#include "Core/LayerStack.h"

namespace Nova::Core {

    class Application {
    public:
        explicit Application(const Window::WindowDesc& windowDesc);
        ~Application();

        void Run();

        Nova::Core::Window& GetWindow() { return *m_Window; }

        template<typename T, typename... Args>
        T& PushLayer(Args&&... args) {
            static_assert(std::is_base_of<Layer, T>::value, "T must be derived from Layer");
            T* layer = new T(std::forward<Args>(args)...);
            m_LayerStack.PushLayer(layer);
            layer->OnAttach();
            return *layer;
        }

        template<typename T, typename... Args>
        T& PushOverlay(Args&&... args) {
            static_assert(std::is_base_of<Layer, T>::value, "T must be derived from Layer");
            T* overlay = new T(std::forward<Args>(args)...);
            m_LayerStack.PushOverlay(overlay);
            overlay->OnAttach();
            return *overlay;
        }

    private:
        bool m_IsRunning;

        void InitEngine(const Window::WindowDesc* windowDesc = nullptr);
        void DestroyEngine();

        void InitWindow(const Window::WindowDesc& windowDesc);
        void DestroyWindow();

        void InitImGui();
        void DestroyImGui();

        Window* m_Window = nullptr;

        LayerStack m_LayerStack;
    };

} // namespace Nova::Core

#endif // APPLICATION_H