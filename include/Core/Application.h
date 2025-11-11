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
            auto layer = std::make_unique<T>(std::forward<Args>(args)...);
            T& layerRef = *layer;
            m_LayerStack.emplace_back(std::move(layer));
            layerRef.OnAttach();
            return layerRef;
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

        std::vector<std::unique_ptr<Layer>> m_LayerStack;
    };

} // namespace Nova::Core

#endif // APPLICATION_H