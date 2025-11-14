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
        InitWindow(*windowDesc);
        InitImGui();
    }

    void Application::DestroyEngine() {
        DestroyImGui();
        DestroyWindow();
    }

    void Application::Run() {
        ImVec4 clearColor(0.0f, 0.0f, 0.0f, 1.00f);
        m_IsRunning = true;

        uint64_t prev = SDL_GetPerformanceCounter();
        const double freq = (double)SDL_GetPerformanceFrequency();

        while(m_IsRunning) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);

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

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Main layer update here
			for (Layer* layer : m_LayerStack)
				layer->OnUpdate(dt);

            // NOTE: rendering can be done elsewhere (eg. render thread)
			for (Layer* layer : m_LayerStack)
				layer->OnRender();

            for (Layer* layer : m_LayerStack)
                layer->OnImGuiRender();

            // Rendering
            ImGui::Render();
            glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
            glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            // Update and Render additional Platform Windows
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
                SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
            }

            SDL_GL_SwapWindow(m_Window->GetSDLWindow()); // Swap buffers
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

    void Application::InitImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        io.IniFilename = "imgui.ini";
        bool iniExisted = std::filesystem::exists(io.IniFilename);
        io.UserData = (void*)(intptr_t)iniExisted;

        // imgui flags
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // Enable Multi-Viewport / Platform Windows

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();
        //ImGui::StyleColorsClassic();

        // Initialize backend SDL + OpenGL
        ImGui_ImplSDL3_InitForOpenGL(m_Window->GetSDLWindow(), m_Window->GetGLContext());
        ImGui_ImplOpenGL3_Init(m_Window->GetGLSLVersion()); // Use the same GLSL
    }

    void Application::DestroyImGui() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

} // namespace Nova::Core