#include <iostream>

#include "Core/Window.h"

namespace Nova::Core {

    Window::Window() : m_Desc{}, m_Window(nullptr), m_GLContext(nullptr), m_GLSLVersion(nullptr) {}

    bool Window::Create(const WindowDesc& desc) {
        m_Desc = desc;
        
        // Initialize SDL video/events (idempotent).
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
            return false;
        }
            
            // Decide GL+GLSL versions
        #if defined(IMGUI_IMPL_OPENGL_ES2)
            // GL ES 2.0 + GLSL 100 (WebGL 1.0)
            m_GLSLVersion = "#version 100";
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        #elif defined(IMGUI_IMPL_OPENGL_ES3)
            // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
            m_GLSLVersion = "#version 300 es";
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        #elif defined(__APPLE__)
            // GL 3.2 Core + GLSL 150
            m_GLSLVersion = "#version 150";
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        #else
            // GL 3.0 + GLSL 130
            m_GLSLVersion = "#version 130";
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        #endif
            // other flags
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

            // If you want a forward-compatible context on macOS:
            // SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

        // Build window flags.
        Uint32 flags = 0;
        if (m_Desc.m_GraphicsAPI == GraphicsAPI::OpenGL)  flags |= SDL_WINDOW_OPENGL;
        if (m_Desc.m_Resizable) flags |= SDL_WINDOW_RESIZABLE;

        m_Window = SDL_CreateWindow(m_Desc.m_Title, m_Desc.m_Width, m_Desc.m_Height, flags);
        if (!m_Window) {
            std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
            Destroy();
            return false;
        }
        SDL_ShowWindow(m_Window);

        if (m_Desc.m_GraphicsAPI == GraphicsAPI::OpenGL) {
            m_GLContext = SDL_GL_CreateContext(m_Window);
            if (!m_GLContext) {
                std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
                Destroy();
                return false;
            }
            MakeCurrent();

            if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
                std::cerr << "Failed to initialize GLAD" << std::endl;
                Destroy();
                return false;
            }

            SetVSync(m_Desc.m_VSync);

        } else if (m_Desc.m_GraphicsAPI == GraphicsAPI::SDLRenderer) {
            m_Renderer = SDL_CreateRenderer(m_Window, nullptr);
            if (!m_Renderer) {
                std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
                Destroy();
                return false;
            }
            SDL_SetRenderVSync(m_Renderer, m_Desc.m_VSync ? 1 : 0);
        }

        return true;
    }

    void Window::Destroy() {
        if (m_GLContext){ SDL_GL_DestroyContext(m_GLContext);}
        if (m_Window)   { SDL_DestroyWindow(m_Window); m_Window = nullptr; }
        if (m_Renderer) { SDL_DestroyRenderer(m_Renderer); m_Renderer = nullptr; }

        SDL_Quit();
    }

    void Window::MakeCurrent() {
        if (m_Window && m_GLContext) {
            SDL_GL_MakeCurrent(m_Window, m_GLContext);
        }
    }

    void Window::SetVSync(bool enabled) {
        if (!m_GLContext) return;
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
        m_Desc.m_VSync = enabled;
    }

    void Window::SwapBuffers() {
        if (m_Window && m_GLContext) {
            SDL_GL_SwapWindow(m_Window);
        }
    }

    void Window::SetTitle(const char* title) {
        if (m_Window) SDL_SetWindowTitle(m_Window, title);
        m_Desc.m_Title = title;
    }

    void Window::GetWindowSize(int& w, int& h) {
        if (!m_Window) { w = h = 0; return; }
        SDL_GetWindowSize(m_Window, &w, &h);
        m_Desc.m_Width = w;
        m_Desc.m_Height = h;
    }

    bool Window::IsMinimized() const {
        if (!m_Window) return false;
        const Uint32 wf = SDL_GetWindowFlags(m_Window);
        return (wf & SDL_WINDOW_MINIMIZED) != 0;
    }

    void Window::PresentRenderer() {
        if (m_Renderer) {
            SDL_RenderPresent(m_Renderer);
        }
    }

    void Window::RaiseEvent(Nova::Events::Event& event) {
        if (m_Desc.m_EventCallback) {
            m_Desc.m_EventCallback(event);
        }
    }

} // namespace Nova::Core