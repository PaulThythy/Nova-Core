#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

#include <memory>
#include <vector>
#include <cstdint>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/Graphics/Mesh.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_Shaders.h"

namespace Nova::Core::Renderer::RHI {

    enum class RHI_PrimitiveTopology {
        Triangles,
        Lines,
        Points
    };

    enum class RHI_IndexType {
        UInt16,
        UInt32
    };

    struct NV_API RHI_DrawCommand {
        std::shared_ptr<Renderer::Graphics::Mesh> m_Mesh;

        RHI_PrimitiveTopology m_Topology = RHI_PrimitiveTopology::Triangles;

        uint32_t m_VertexCount = 0;
        uint32_t m_FirstVertex = 0;

        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
    };

    struct NV_API RHI_DrawIndexedCommand {
        std::shared_ptr<Renderer::Graphics::Mesh> m_Mesh;

        RHI_PrimitiveTopology m_Topology = RHI_PrimitiveTopology::Triangles;
        RHI_IndexType m_IndexType = RHI_IndexType::UInt32;

        uint32_t m_IndexCount = 0;
        uint32_t m_FirstIndex = 0;
        int32_t  m_VertexOffset = 0;   // base vertex

        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
    };

    class NV_API IRenderer {
    public:
        virtual ~IRenderer() = default;
        static std::unique_ptr<IRenderer> Create(Core::GraphicsAPI api);

        virtual bool Create() = 0;
        virtual void Destroy() = 0;

        virtual bool Resize(int w, int h) = 0;

        virtual void Update(float dt) = 0;

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        /**
         * Call after all draws targeting the editor viewport / offscreen target, before ImGui.
         * OpenGL: binds the default framebuffer and sets glViewport to the window size so UI draws correctly.
         */
        virtual void PrepareForImGui() = 0;

        // Scene state is configured separately from raw draw commands.
        virtual void BeginScene(const glm::mat4& view, const glm::mat4& proj) = 0;
        virtual void SetModelMatrix(const glm::mat4& model) = 0;

        virtual void Draw(const RHI_DrawCommand& cmd) = 0;
        virtual void DrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;

        // Returns an API-specific ImGui texture identifier for the current viewport
        // render target, or nullptr if the renderer does not expose one.
        // OpenGL: typically a GLuint cast to ImTextureID.
        // Vulkan: typically a VkDescriptorSet cast to ImTextureID.
        virtual void* GetViewportTextureID() const = 0;

        /** Returns the current/default shader (e.g. model shader). Ownership stays with the renderer. */
        virtual RHI_Shaders* GetShader() = 0;

        /**
         * Create a fullscreen pass shader from shader sources (Slang inputs).
         * Each backend compiles for its graphics API; callers do not pass SPIR-V or other IR.
         * Vertex stage expects position (location 0) and UV (location 1), stride 16 bytes (2× float2).
         * The pipeline uses alpha blending and depth test/write like the editor grid pass.
         * Uses the same engine descriptor set (NovaUniforms) as the model shader.
         * Caller owns the returned pointer and must call DestroyFullscreenShader() to free it.
         */
        virtual RHI_Shaders* CreateFullscreenShader(
            const RHI_ShaderCompileInput& vertIn,
            const RHI_ShaderCompileInput& fragIn) = 0;

        /** Destroy a shader created by CreateFullscreenShader(). */
        virtual void DestroyFullscreenShader(RHI_Shaders* shader) = 0;

        /**
         * Draw a fullscreen quad (two triangles, 6 vertices) with baked NDC positions and UVs.
         * Flushes the current scene parameters (view, proj, globals) before drawing.
         */
        virtual void DrawFullscreen(RHI_Shaders* shader) = 0;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H