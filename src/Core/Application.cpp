#include "Core/Application.h"
#include "Core/Log.h"

#include <cstdlib>
#include <filesystem>
#include <ranges>

namespace Nova::Core {

    Application* Application::s_Instance = nullptr;

    Application::Application(const Window::WindowDesc& windowDesc) : m_IsRunning(false) {
        if (s_Instance) {
            NV_LOG_WARN("Application instance already exists");
        }
        s_Instance = this;

        InitEngine(&windowDesc);
    }

    Application::~Application() {
        DestroyEngine();
    }

    void Application::InitEngine(const Window::WindowDesc* windowDesc) {
        Window::WindowDesc desc = windowDesc ? *windowDesc : Window::WindowDesc{};

        desc.m_EventCallback = [this](Nova::Core::Events::Event& e) {
            OnEvent(e);
        };

        InitWindow(desc);
        m_ImGuiLayer = &m_LayerStack.PushOverlay<ImGuiLayer>(*m_Window, desc.m_GraphicsAPI);
    }

    void Application::DestroyEngine() {
        DestroyWindow();
    }

    void Application::Run() {
        m_IsRunning = true;

        uint64_t prev = SDL_GetPerformanceCounter();
        const double freq = (double)SDL_GetPerformanceFrequency();

        while(m_IsRunning) {
            SDL_WindowID mainWindowID = SDL_GetWindowID(m_Window->GetSDLWindow());

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (m_ImGuiLayer)
                    m_ImGuiLayer->ProcessSDLEvent(event);

                switch (event.type) {
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                        if (event.window.windowID == mainWindowID) {
                            WindowClosedEvent e;
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_WINDOW_RESIZED: {
                        if (event.window.windowID == mainWindowID) {
                            int w = event.window.data1;
                            int h = event.window.data2;
                            WindowResizeEvent e((uint32_t)w, (uint32_t)h);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_QUIT: {
                        if (event.window.windowID == mainWindowID) {
                            WindowClosedEvent e;
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_MOTION: {
                        if (event.window.windowID == mainWindowID) {
                            MouseMovedEvent e((double)event.motion.x, (double)event.motion.y);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_WHEEL: {
                        if(event.window.windowID == mainWindowID) {
                            MouseScrolledEvent e((double)event.wheel.x, (double)event.wheel.y);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                        if(event.window.windowID == mainWindowID) {
                            MouseButtonPressedEvent e((int)event.button.button);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_UP: {
                        if(event.window.windowID == mainWindowID) {
                            MouseButtonReleasedEvent e((int)event.button.button);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_KEY_DOWN: {
                        if(event.window.windowID == mainWindowID) {
                            bool repeat = event.key.repeat != 0;
                            KeyPressedEvent e((int)event.key.key, repeat);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    case SDL_EVENT_KEY_UP: {
                        if(event.window.windowID == mainWindowID) {
                            KeyReleasedEvent e((int)event.key.key);
                            m_Window->RaiseEvent(e);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            if (SDL_GetWindowFlags(m_Window->GetSDLWindow()) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(10);
                continue;
            }

            // deltaTime
            uint64_t now = SDL_GetPerformanceCounter();
            float dt = (float)((now - prev) / freq);
            prev = now;

            if (m_Window->GetSDLRenderer()) {
                SDL_Renderer* r = m_Window->GetSDLRenderer();
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderClear(r);
            }

            //TODO only update layers when the window is not minimized

            for (Layer* layer : m_LayerStack)
                layer->OnUpdate(dt);

            for (Layer* layer : m_LayerStack)
                layer->OnBegin();

            for (Layer* layer : m_LayerStack)
                layer->OnRender();
            
            if (m_ImGuiLayer) {
                m_ImGuiLayer->Begin();

                for (Layer* layer : m_LayerStack)
                    layer->OnImGuiRender();

                m_ImGuiLayer->End();
            }

            for (Layer* layer : m_LayerStack)
                layer->OnEnd();

            m_LayerStack.ProcessPendingTransitions();

            if (m_Window->GetSDLRenderer()) {
                m_Window->PresentRenderer();
            }
        }
    }

    void Application::InitWindow(const Window::WindowDesc& windowDesc) {
        m_Window = new Window;
        if (!m_Window->Create(windowDesc)) {
            NV_LOG_FATAL("Failed to create window");
            exit(EXIT_FAILURE);
        }
    }

    void Application::DestroyWindow() {
        if (m_Window) {
            m_Window->Destroy();
            delete m_Window;
            m_Window = nullptr;
        }
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowClosedEvent>(
            [this](WindowClosedEvent& ev) { return OnWindowClose(ev); });
        dispatcher.Dispatch<WindowResizeEvent>(
            [this](WindowResizeEvent& ev) { return OnWindowResize(ev); });

        // From top to bottom
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
            if (e.m_Handle)
                break;
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowClose(WindowClosedEvent&) {
        m_IsRunning = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        //TODO do something
        (void)e;
        return false;
    }

} // namespace Nova::Core