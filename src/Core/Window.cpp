#include <string>

#include "Core/Log.h"
#include "Core/Window.h"

namespace Nova::Core {

    Window::Window() : m_Desc{}, m_Window(nullptr) {}

    bool Window::Create(const WindowDesc& desc) {
        m_Desc = desc;
        
        // Initialize SDL video/events (idempotent).
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            NV_LOG_FATAL(std::string("SDL_Init failed: ") + SDL_GetError());
            return false;
        }

        // Build window flags.
        Uint32 flags = 0;
        if (m_Desc.m_GraphicsAPI == GraphicsAPI::Vulkan)  flags |= SDL_WINDOW_VULKAN;
        if (m_Desc.m_Resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (m_Desc.m_Maximized) flags |= SDL_WINDOW_MAXIMIZED;

        m_Window = SDL_CreateWindow(m_Desc.m_Title, m_Desc.m_Width, m_Desc.m_Height, flags);
        if (!m_Window) {
            NV_LOG_FATAL(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
            Destroy();
            return false;
        }
        SDL_ShowWindow(m_Window);



        if (m_Desc.m_GraphicsAPI == GraphicsAPI::SDLRenderer) {
            m_Renderer = SDL_CreateRenderer(m_Window, nullptr);
            if (!m_Renderer) {
                NV_LOG_FATAL(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
                Destroy();
                return false;
            }
            SDL_SetRenderVSync(m_Renderer, m_Desc.m_VSync ? 1 : 0);
        }

        return true;
    }

    void Window::Destroy() {
        if (m_Window)   { SDL_DestroyWindow(m_Window); m_Window = nullptr; }
        if (m_Renderer) { SDL_DestroyRenderer(m_Renderer); m_Renderer = nullptr; }

        SDL_Quit();
    }

    void Window::SetVSync(bool enabled) {
        if (m_Renderer) {
            SDL_SetRenderVSync(m_Renderer, enabled ? 1 : 0);
        }
        m_Desc.m_VSync = enabled;
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

    void Window::RaiseEvent(Events::Event& event) {
        if (m_Desc.m_EventCallback) {
            m_Desc.m_EventCallback(event);
        }
    }

} // namespace Nova::Core