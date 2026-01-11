#include "Core/ImGuiLayer.h"

#include "Core/Log.h"

#include <iostream>

namespace Nova::Core {
    
    ImGuiLayer::ImGuiLayer(Window& window, GraphicsAPI api) : Layer("ImGuiLayer"), m_Window(window), m_GraphicsAPI(api) {}

    void ImGuiLayer::OnAttach() {
        // Contexte ImGui
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

        switch (m_GraphicsAPI) {
            case GraphicsAPI::OpenGL: {
                ImGui_ImplSDL3_InitForOpenGL(sdlWindow, m_Window.GetGLContext());
                ImGui_ImplOpenGL3_Init(m_Window.GetGLSLVersion());
                break;
            }
            case GraphicsAPI::SDLRenderer: {
                SDL_Renderer* renderer = m_Window.GetSDLRenderer();
                ImGui_ImplSDL3_InitForSDLRenderer(sdlWindow, renderer);
                ImGui_ImplSDLRenderer3_Init(renderer);
                break;
            } 
            case GraphicsAPI::Vulkan: {
                ImGui_ImplSDL3_InitForVulkan(sdlWindow);

                ImGui_ImplVulkan_InitInfo init_info = {};
                init_info.Instance = m_VulkanInitInfo.m_Instance;
                init_info.PhysicalDevice = m_VulkanInitInfo.m_PhysicalDevice;
                init_info.Device = m_VulkanInitInfo.m_Device;
                init_info.QueueFamily = 0; // Assume that graphics and presentation are on the same queue for now
                init_info.Queue = m_VulkanInitInfo.m_Queue;
                init_info.DescriptorPool = m_VulkanInitInfo.m_DescriptorPool;
                init_info.MinImageCount = m_VulkanInitInfo.m_MinImageCount;
                init_info.ImageCount = m_VulkanInitInfo.m_ImageCount;

                //init_info.UseDynamicRendering = true;

                ImGui_ImplVulkan_Init(&init_info);
                break;
            }

            default:
                break;
        }
    }

    void ImGuiLayer::OnDetach() {
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

        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::ProcessSDLEvent(const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
    }

    void ImGuiLayer::Begin() {
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