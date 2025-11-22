#include "Core/Application.h"

#include <iostream>
#include <filesystem>
#include <ranges>

namespace Nova::Core {

    Application* Application::s_Instance = nullptr;

    Application::Application(const Window::WindowDesc& windowDesc) : m_IsRunning(false) {
        if (s_Instance) {
            std::cerr << "Warning: Application instance already exists\n";
        }
        s_Instance = this;

        InitEngine(&windowDesc);
    }

    Application::~Application() {
        DestroyEngine();
    }

    void Application::InitEngine(const Window::WindowDesc* windowDesc) {
        Window::WindowDesc desc = windowDesc ? *windowDesc : Window::WindowDesc{};

        desc.m_EventCallback = [this](Nova::Events::Event& e) {
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
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (m_ImGuiLayer)
                    m_ImGuiLayer->ProcessSDLEvent(event);

                switch (event.type) {
                    case SDL_EVENT_QUIT: {
                        Nova::Events::WindowClosedEvent e;
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_WINDOW_RESIZED: {
                        int w = event.window.data1;
                        int h = event.window.data2;
                        Nova::Events::WindowResizeEvent e((uint32_t)w, (uint32_t)h);
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_MOUSE_MOTION: {
                        Nova::Events::MouseMovedEvent e((double)event.motion.x, (double)event.motion.y);
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_MOUSE_WHEEL: {
                        Nova::Events::MouseScrolledEvent e((double)event.wheel.x, (double)event.wheel.y);
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                        Nova::Events::MouseButtonPressedEvent e((int)event.button.button);
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_UP: {
                        Nova::Events::MouseButtonReleasedEvent e((int)event.button.button);
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_KEY_DOWN: {
                        bool repeat = event.key.repeat != 0;
                        Nova::Events::KeyPressedEvent e((int)event.key.key, repeat);
                        m_Window->RaiseEvent(e);
                        break;
                    }
                    case SDL_EVENT_KEY_UP: {
                        Nova::Events::KeyReleasedEvent e((int)event.key.key);
                        m_Window->RaiseEvent(e);
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

            if (m_Window->GetGLContext()) {
                m_Window->MakeCurrent();
                int w, h;
                m_Window->GetWindowSize(w, h);
                glViewport(0, 0, w, h);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            } else if (m_Window->GetSDLRenderer()) {
                SDL_Renderer* r = m_Window->GetSDLRenderer();
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderClear(r);
            }

            //TODO only update layers when the window is not minimized

            for (Layer* layer : m_LayerStack)
                layer->OnUpdate(dt);

            for (Layer* layer : m_LayerStack)
                layer->OnRender();
            
            if (m_ImGuiLayer) {
                m_ImGuiLayer->Begin();

                for (Layer* layer : m_LayerStack)
                    layer->OnImGuiRender();

                m_ImGuiLayer->End();
            }

            m_LayerStack.ProcessPendingTransitions();

            if (m_Window->GetGLContext()) {
                m_Window->SwapBuffers();
            } else if (m_Window->GetSDLRenderer()) {
                m_Window->PresentRenderer();
            }
        }
    }

    void Application::InitWindow(const Window::WindowDesc& windowDesc) {
        m_Window = new Window;
        if (!m_Window->Create(windowDesc)) {
            std::cerr << "Failed to create window\n";
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

    void Application::OnEvent(Nova::Events::Event& e) {
        Nova::Events::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<Nova::Events::WindowClosedEvent>(
            [this](Nova::Events::WindowClosedEvent& ev) { return OnWindowClose(ev); });
        dispatcher.Dispatch<Nova::Events::WindowResizeEvent>(
            [this](Nova::Events::WindowResizeEvent& ev) { return OnWindowResize(ev); });

        // From top to bottom
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
            if (e.m_Handle)
                break;
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowClose(Nova::Events::WindowClosedEvent&) {
        m_IsRunning = false;
        return true;
    }

    bool Application::OnWindowResize(Nova::Events::WindowResizeEvent& e) {
        //TODO do something
        (void)e;
        return false;
    }

} // namespace Nova::Core