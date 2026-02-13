#include "Renderer/Backends/OpenGL/GL_Renderer.h"
#include "Core/Application.h"
#include "Core/Log.h"

#include <glad/gl.h>

namespace Nova::Core::Renderer::Backends::OpenGL {

    bool GL_Renderer::Create() {
        // Configure ImGui backend for OpenGL
        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetImGuiBackend(GraphicsAPI::OpenGL);

        // Verify OpenGL context is available
        if (!glIsEnabled(GL_CONTEXT_CORE_PROFILE_BIT)) {
            // OpenGL context is initialized
        }

        // Initialize shader program (can be customized or loaded from files later)
        // For now, create a minimal program or leave at 0 if not needed during Create
        // This allows AppLayer to manage specific shaders independently
        m_Program = 0;

        // Set default OpenGL state
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);

        NV_LOG_INFO("OpenGL Renderer created and initialized.");
        return true;
    }

    void GL_Renderer::Destroy() {
        // Clean up shader program if it exists
        if (m_Program != 0) {
            glDeleteProgram(m_Program);
            m_Program = 0;
        }

        // Reset OpenGL state
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        NV_LOG_INFO("OpenGL Renderer destroyed.");
    }

    bool GL_Renderer::Resize(int w, int h) {
        // Handle viewport resizing if needed
        return true;
    }

    void GL_Renderer::Update(float dt) {
        (void)dt;
        // Update any renderer-specific data if needed
    }

    void GL_Renderer::BeginFrame() {
        // Clear the screen or set up frame-specific state if needed
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void GL_Renderer::Render() {
        // This will be handled by AppLayer for now, so we can leave this empty
    }

    void GL_Renderer::EndFrame() {
        // Swap buffers or perform any end-of-frame operations if needed
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL