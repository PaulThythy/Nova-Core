#include <string>

#include "Core/Log.h"
#include "Core/Window.h"

namespace Nova::Core {

    Window::Window() : m_Desc{}, m_Window(nullptr) {}

    bool Window::Create(const WindowDesc& desc) {
        m_Desc = desc;
        m_GraphicsAPI = GraphicsAPI::None;
        
        // Initialize SDL video/events (idempotent).
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            NV_LOG_FATAL(std::string("SDL_Init failed: ") + SDL_GetError());
            return false;
        }

        // Create an API-agnostic window. SDL_WINDOW_VULKAN is requested up front
        // so a later SetGraphicsAPI(Vulkan) does not require recreating the window.
        Uint32 flags = SDL_WINDOW_VULKAN;
        if (m_Desc.m_Resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (m_Desc.m_Maximized) flags |= SDL_WINDOW_MAXIMIZED;

        m_Window = SDL_CreateWindow(m_Desc.m_Title, m_Desc.m_Width, m_Desc.m_Height, flags);
        if (!m_Window) {
            NV_LOG_FATAL(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
            Destroy();
            return false;
        }
        SDL_ShowWindow(m_Window);

        return true;
    }

    void Window::DestroyGraphicsAPIResources() {
        if (m_Renderer) {
            SDL_DestroyRenderer(m_Renderer);
            m_Renderer = nullptr;
        }
    }

    bool Window::SetGraphicsAPI(GraphicsAPI api) {
        if (!m_Window) {
            NV_LOG_ERROR("SetGraphicsAPI called before Create()");
            return false;
        }

        if (m_GraphicsAPI == api)
            return true;

        DestroyGraphicsAPIResources();
        m_GraphicsAPI = api;

        if (api == GraphicsAPI::SDLRenderer) {
            m_Renderer = SDL_CreateRenderer(m_Window, nullptr);
            if (!m_Renderer) {
                NV_LOG_FATAL(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
                m_GraphicsAPI = GraphicsAPI::None;
                return false;
            }
            SDL_SetRenderVSync(m_Renderer, m_Desc.m_VSync ? 1 : 0);
        }

        return true;
    }

    void Window::Destroy() {
        DestroyGraphicsAPIResources();
        m_GraphicsAPI = GraphicsAPI::None;

        if (m_Window) {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }

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