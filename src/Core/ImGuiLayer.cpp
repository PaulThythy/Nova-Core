#include "Core/ImGuiLayer.h"

#include "Core/Log.h"

#include "ImGuizmo.h"

#include <iostream>

namespace Nova::Core {
    
    ImGuiLayer::ImGuiLayer(Window& window) : Layer("ImGuiLayer"), m_Window(window), m_GraphicsAPI(GraphicsAPI::None) {}

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
        m_IsRendererInitializedWithoutBackend = true;
    }

    void ImGuiLayer::SetImGuiBackend(GraphicsAPI api) {
        if (m_IsRendererInitialized) {
            if (m_GraphicsAPI == api) {
                NV_LOG_WARN("ImGui backend already initialized");
                return;
            }
            DestroyImGuiBackend(m_GraphicsAPI);
        }

        if (m_IsRendererInitializedWithoutBackend) {
            ImGui_ImplSDL3_Shutdown();
            m_IsRendererInitializedWithoutBackend = false;
        }

        m_GraphicsAPI = api;
        switch (m_GraphicsAPI) {
            case GraphicsAPI::SDLRenderer:
                ImGui_ImplSDL3_InitForSDLRenderer(m_Window.GetSDLWindow(), m_Window.GetSDLRenderer());
                ImGui_ImplSDLRenderer3_Init(m_Window.GetSDLRenderer());
                m_IsRendererInitialized = true;
                NV_LOG_INFO("ImGui SDLRenderer3 backend initialized");
                break;
            case GraphicsAPI::Vulkan:
                // Platform/renderer init is completed by SetVulkanInitInfo() once
                // the Vulkan device + descriptor pool exist.
                NV_LOG_INFO("ImGui Vulkan backend selected; waiting for SetVulkanInitInfo()");
                break;
            default:
                NV_LOG_ERROR("Unsupported Graphics API");
                break;
        }
    }

    void ImGuiLayer::DestroyImGuiBackend(GraphicsAPI api) {
        if (!m_IsRendererInitialized)
            return;

        switch (api) {
            case GraphicsAPI::SDLRenderer:
                ImGui_ImplSDLRenderer3_Shutdown();
                break;
            case GraphicsAPI::Vulkan:
                if (m_VulkanInitInfo.Device != VK_NULL_HANDLE)
                    vkDeviceWaitIdle(m_VulkanInitInfo.Device);
                m_VulkanInitInfo.Device = VK_NULL_HANDLE;
                ImGui_ImplVulkan_Shutdown();
                break;
            default:
                break;
        }

        m_IsRendererInitialized = false;
    }

    void ImGuiLayer::SetVulkanInitInfo(const ImGui_ImplVulkan_InitInfo& info) {
        if (m_IsRendererInitialized) {
            if (m_GraphicsAPI == GraphicsAPI::Vulkan) {
                NV_LOG_WARN("ImGui Vulkan backend already initialized");
                return;
            }
            DestroyImGuiBackend(m_GraphicsAPI);
        }

        m_VulkanInitInfo = info;
        m_GraphicsAPI = GraphicsAPI::Vulkan;

        ImGui_ImplSDL3_Shutdown();
        m_IsRendererInitializedWithoutBackend = false;

        ImGui_ImplSDL3_InitForVulkan(m_Window.GetSDLWindow());
        ImGui_ImplVulkan_Init(&m_VulkanInitInfo);

        m_IsRendererInitialized = true;
        NV_LOG_INFO("ImGui Vulkan backend initialized");
    }

    void ImGuiLayer::OnDetach() {
        DestroyImGuiBackend(m_GraphicsAPI);
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::ProcessSDLEvent(const SDL_Event& e) {
        ImGui_ImplSDL3_ProcessEvent(&e);
    }

    void ImGuiLayer::Begin() {
        // Renderer backend NewFrame is optional; the platform + ImGui frame
        // must always run so OnImGuiRender() stays inside a valid frame scope.
        if (m_IsRendererInitialized) {
            switch (m_GraphicsAPI) {
                case GraphicsAPI::SDLRenderer:
                    ImGui_ImplSDLRenderer3_NewFrame();
                    break;
                case GraphicsAPI::Vulkan:
                    ImGui_ImplVulkan_NewFrame();
                    break;
                default:
                    break;
            }
        } else {
            // ImGui 1.92 builds the font atlas via RendererHasTextures during
            // NewFrame. Without a GPU backend yet, opt into that path so the
            // frame can proceed (draw data simply won't be submitted).
            ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
    }

    void ImGuiLayer::End() {
        ImGuiIO& io = ImGui::GetIO();

        int w, h;
        m_Window.GetWindowSize(w, h);
        io.DisplaySize = ImVec2((float)w, (float)h);

        ImGui::Render();

        if (!m_IsRendererInitialized)
            return;

        switch (m_GraphicsAPI) {
            case GraphicsAPI::SDLRenderer: {
                SDL_Renderer* renderer = m_Window.GetSDLRenderer();
                SDL_SetRenderScale(renderer,
                                   io.DisplayFramebufferScale.x,
                                   io.DisplayFramebufferScale.y);

                ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
                break;
            }

            case GraphicsAPI::Vulkan: {
                if (m_VulkanBeforeRenderCallback) {
                    m_VulkanBeforeRenderCallback();
                }

                if (m_CurrentCommandBuffer != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_CurrentCommandBuffer);
                }
                else {
                    NV_LOG_ERROR("Vulkan Command Buffer not set for ImGuiLayer!");
                }
                // Secondary viewports are rendered after the main CB submit
                // (see RenderPlatformWindows) so they can sample this frame's textures.
                break;
            }

            default:
                break;
        }
    }

    void ImGuiLayer::RenderPlatformWindows() {
        if (!m_IsRendererInitialized)
            return;

        ImGuiIO& io = ImGui::GetIO();
        if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
            return;

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}