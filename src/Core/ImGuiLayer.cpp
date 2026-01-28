#include "Core/ImGuiLayer.h"

#include "Core/Log.h"

#include <iostream>

namespace Nova::Core {
    
    ImGuiLayer::ImGuiLayer(Window& window, GraphicsAPI api) : Layer("ImGuiLayer"), m_Window(window), m_GraphicsAPI(api) {}

    void ImGuiLayer::OnAttach() {
        // ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();
        //ImGui::StyleColorsClassic();

        // Adjust style for viewports
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        SDL_Window* sdlWindow = m_Window.GetSDLWindow();
        ImGui_ImplSDL3_InitForOther(sdlWindow);
    }

    void ImGuiLayer::SetImGuiBackend(GraphicsAPI api) {
        if(m_IsRendererInitialized){
            NV_LOG_WARN("ImGui backend already initialized");
            return;
        }

        m_GraphicsAPI = api;
        switch(m_GraphicsAPI) {
            case GraphicsAPI::OpenGL:
                ImGui_ImplOpenGL3_Init(m_Window.GetGLSLVersion());
                m_IsRendererInitialized = true;
                NV_LOG_INFO("ImGui OpenGL3 backend initialized");
                break;
            case GraphicsAPI::SDLRenderer:
                ImGui_ImplSDLRenderer3_Init(m_Window.GetSDLRenderer());
                m_IsRendererInitialized = true;
                NV_LOG_INFO("ImGui SDLRenderer3 backend initialized");
                break;
            case GraphicsAPI::Vulkan: {
                NV_LOG_WARN("For Vulkan, please use SetVulkanInitInfo() instead of SetImGuiBackend()");
                break;
            }
            default:
                NV_LOG_ERROR("Unsupported Graphics API");
                break;
        }
    }

    void ImGuiLayer::SetVulkanInitInfo(const ImGui_ImplVulkan_InitInfo& info) {
        if(m_IsRendererInitialized){
            NV_LOG_WARN("ImGui Vulkan backend already initialized");
            return;
        }
        m_VulkanInitInfo = info;

        if(m_GraphicsAPI == GraphicsAPI::Vulkan) {
            ImGui_ImplVulkan_Init(&m_VulkanInitInfo);

            // init_info.Allocator = m_VulkanInitInfo.m_Allocator;
            // init_info.CheckVkResultFn = nullptr;
            // init_info.UseDynamicRendering = false;

            m_IsRendererInitialized = true;
            NV_LOG_INFO("ImGui Vulkan backend initialized");
        }
    }

    void ImGuiLayer::OnDetach() {
        if (m_IsRendererInitialized && m_GraphicsAPI == GraphicsAPI::Vulkan) {
            if (m_VulkanInitInfo.Device != VK_NULL_HANDLE) {
                vkDeviceWaitIdle(m_VulkanInitInfo.Device);
            }
        }

        if(m_IsRendererInitialized) {
            switch (m_GraphicsAPI) {
                case GraphicsAPI::OpenGL:
                    ImGui_ImplOpenGL3_Shutdown();
                    break;
                case GraphicsAPI::SDLRenderer:
                    ImGui_ImplSDLRenderer3_Shutdown();
                    break;
                case GraphicsAPI::Vulkan:
                    ImGui_ImplVulkan_Shutdown();
                    break;
                default:
                    break;
            }

            m_IsRendererInitialized = false;
        }

        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::ProcessSDLEvent(const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
    }

    void ImGuiLayer::Begin() {
        if(!m_IsRendererInitialized) {
            NV_LOG_ERROR("ImGui backend not initialized!");
            return;
        }

        switch (m_GraphicsAPI) {
            case GraphicsAPI::OpenGL:
                ImGui_ImplOpenGL3_NewFrame();
                break;
            case GraphicsAPI::SDLRenderer:
                ImGui_ImplSDLRenderer3_NewFrame();
                break;
            case GraphicsAPI::Vulkan:
                ImGui_ImplVulkan_NewFrame();
                break;
            default:
                break;
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::End() {
        if(!m_IsRendererInitialized) {
            NV_LOG_ERROR("ImGui backend not initialized!");
            return;
        }

        ImGuiIO& io = ImGui::GetIO();

        int w, h;
        m_Window.GetWindowSize(w, h);
        io.DisplaySize = ImVec2((float)w, (float)h);

        ImGui::Render();

        switch (m_GraphicsAPI) {
            case GraphicsAPI::OpenGL: {
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    SDL_Window*   backup_window   = SDL_GL_GetCurrentWindow();
                    SDL_GLContext backup_context  = SDL_GL_GetCurrentContext();
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                    SDL_GL_MakeCurrent(backup_window, backup_context);
                }
                break;
            }

            case GraphicsAPI::SDLRenderer: {
                SDL_Renderer* renderer = m_Window.GetSDLRenderer();
                SDL_SetRenderScale(renderer,
                                   io.DisplayFramebufferScale.x,
                                   io.DisplayFramebufferScale.y);

                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
                break;
            }

            case GraphicsAPI::Vulkan: {
                if (m_CurrentCommandBuffer != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CurrentCommandBuffer);
                }
                else {
                    NV_LOG_ERROR("Vulkan Command Buffer not set for ImGuiLayer!");
                }

                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
                break;
            }

            default:
                break;
        }
    }
}