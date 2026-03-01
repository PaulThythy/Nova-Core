#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/Backends/OpenGL/GL_Shaders.h"

#include <glad/gl.h>
#include <memory>

namespace Nova::Core::Renderer::Backends::OpenGL {

    class GL_Renderer final : public RHI::IRenderer {
    public:
        GL_Renderer() = default;
        ~GL_Renderer() override = default;

        bool Create() override;
        void Destroy() override;

        bool Resize(int w, int h) override;

        void Update(float dt) override;

        void BeginFrame() override;
        void Render() override;
        void EndFrame() override;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        //TODO add framebuffer field

        //TODO GetViewportTextureID()
    private:
        std::unique_ptr<GL_ShaderProgram> m_ShaderProgram;
    };

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_RENDERER_H