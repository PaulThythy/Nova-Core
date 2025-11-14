#include "Core/Application.h"

#include <iostream>
#include <filesystem>

namespace Nova::Core {

    Application::Application(const Window::WindowDesc& windowDesc) : m_IsRunning(false) {
        InitEngine(&windowDesc);
    }

    Application::~Application() {
        DestroyEngine();
    }

    void Application::InitEngine(const Window::WindowDesc* windowDesc) {
        Window::WindowDesc desc = windowDesc ? *windowDesc : Window::WindowDesc{};
        InitWindow(desc);
        m_ImGuiLayer = &PushOverlay<ImGuiLayer>(*m_Window, desc.m_GraphicsAPI);
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

                if (event.type == SDL_EVENT_QUIT) {
                    m_IsRunning = false;
                }

                if(event.type == SDL_EVENT_WINDOW_RESIZED) {
                    //int width = event.window.data1;
                    //int height = event.window.data2;
                    // resize event
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

} // namespace Nova::Core