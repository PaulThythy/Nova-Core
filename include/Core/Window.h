#ifndef WINDOW_H
#define WINDOW_H

#include <functional>
#include <SDL3/SDL.h>

#include "Api.h"
#include "Core/GraphicsAPI.h"

namespace Nova::Core::Events {
    class Event;
    class WindowClosedEvent;
    class WindowResizeEvent;
}

namespace Nova::Core {
    
    class NV_API Window {
    public:
        struct NV_API WindowDesc {
            const char* m_Title = "Nova Engine";
            int m_Width         = 1280;
            int m_Height        = 720;
            bool m_Resizable    = true;
            bool m_Maximized    = false;
            GraphicsAPI m_GraphicsAPI = GraphicsAPI::Vulkan;
            bool m_VSync        = true;

            using EventCallbackFn = std::function<void(Events::Event&)>;
            EventCallbackFn m_EventCallback;
        };

        Window();
        ~Window() { Destroy(); }

        void SetVSync(bool enabled);
        void SetTitle(const char* title);

        void GetWindowSize(int& width, int& height);
        SDL_Window* GetSDLWindow() const { return m_Window; }
        SDL_Renderer* GetSDLRenderer() const { return m_Renderer; }
        GraphicsAPI GetGraphicsAPI() const { return m_Desc.m_GraphicsAPI; }

        void SetSDLRenderer(SDL_Renderer* renderer) { m_Renderer = renderer; }

        bool IsMinimized() const;

        bool Create(const WindowDesc& desc);
        void Destroy();

        void PresentRenderer();

        void RaiseEvent(Events::Event& event);
    
    private:
        WindowDesc m_Desc;

        SDL_Window* m_Window = nullptr;
        SDL_Renderer* m_Renderer = nullptr;
    };
} // namespace Nova::Core

#endif // WINDOW_H