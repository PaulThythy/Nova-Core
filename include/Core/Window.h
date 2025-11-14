#ifndef WINDOW_H
#define WINDOW_H

#include <SDL3/SDL.h>

#include "Core/GraphicsAPI.h"

namespace Nova::Core {
    
    class Window {
    public:
        struct WindowDesc {
            const char* m_Title = "Nova Engine";
            int m_Width         = 1280;
            int m_Height        = 720;
            bool m_Resizable    = true;
            GraphicsAPI m_GraphicsAPI = GraphicsAPI::OpenGL;
            int m_GL_Major      = 3;
            int m_GL_Minor      = 3;
            int m_GL_Profile    = SDL_GL_CONTEXT_PROFILE_CORE;
            bool m_VSync        = true;
        };

        Window();
        ~Window() { Destroy(); }


        void MakeCurrent();
        void SetVSync(bool enabled);
        void SwapBuffers();
        void SetTitle(const char* title);

        void GetWindowSize(int& width, int& height);
        const char* GetGLSLVersion() const { return m_GLSLVersion; }
        SDL_Window* GetSDLWindow() const { return m_Window; }
        SDL_GLContext GetGLContext() const { return m_GLContext; }
        SDL_Renderer* GetSDLRenderer() const { return m_Renderer; }

        void SetSDLRenderer(SDL_Renderer* renderer) { m_Renderer = renderer; }

        bool IsMinimized() const;

        bool Create(const WindowDesc& desc);
        void Destroy();

        void PresentRenderer();
    
    private:
        WindowDesc m_Desc;

        SDL_Window* m_Window = nullptr;
        SDL_Renderer* m_Renderer = nullptr;
        SDL_GLContext m_GLContext;

        const char* m_GLSLVersion = nullptr;
    };
} // namespace Nova::Core

#endif // WINDOW_H