#ifndef APPLICATION_H
#define APPLICATION_H

#include <SDL3/SDL.h>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include "Core/Window.h"

namespace Nova::Core {

    class Application {
    public:
        explicit Application(const Window::WindowDesc& windowDesc);
        ~Application();

        void Run();

    private:
        bool m_IsRunning;

        void InitEngine(const Window::WindowDesc* windowDesc = nullptr);
        void DestroyEngine();

        void InitWindow(const Window::WindowDesc& windowDesc);
        void DestroyWindow();

        void InitImGui();
        void DestroyImGui();

        Window* m_Window = nullptr;
    };

} // namespace Nova::Core

#endif // APPLICATION_H