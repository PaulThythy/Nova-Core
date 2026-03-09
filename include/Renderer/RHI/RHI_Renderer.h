#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

#include <memory>

#include "Core/GraphicsAPI.h"
#include "Renderer/Graphics/Mesh.h"
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

    struct RHI_DrawCommand {
        std::shared_ptr<Renderer::Graphics::Mesh> m_Mesh;

        glm::mat4 m_Model{1.0f};
        glm::mat4 m_View{1.0f};
        glm::mat4 m_Proj{1.0f};

        RHI_PrimitiveTopology m_Topology = RHI_PrimitiveTopology::Triangles;

        uint32_t m_VertexCount = 0;
        uint32_t m_FirstVertex = 0;

        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
    };

    struct RHI_DrawIndexedCommand {
        std::shared_ptr<Renderer::Graphics::Mesh> m_Mesh;

        glm::mat4 m_Model{1.0f};
        glm::mat4 m_View{1.0f};
        glm::mat4 m_Proj{1.0f};

        RHI_PrimitiveTopology m_Topology = RHI_PrimitiveTopology::Triangles;
        RHI_IndexType m_IndexType = RHI_IndexType::UInt32;

        uint32_t m_IndexCount = 0;
        uint32_t m_FirstIndex = 0;
        int32_t  m_VertexOffset = 0;   // base vertex

        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
    };

    class IRenderer {
    public:
        virtual ~IRenderer() = default;
        static std::unique_ptr<IRenderer> Create(Core::GraphicsAPI api);

        virtual bool Create() = 0;
        virtual void Destroy() = 0;

        virtual bool Resize(int w, int h) = 0;

        virtual void Update(float dt) = 0;

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        virtual void Draw(const RHI_DrawCommand& cmd) = 0;
        virtual void DrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;

        // Returns an API-specific ImGui texture identifier for the current viewport
        // render target, or nullptr if the renderer does not expose one.
        // OpenGL: typically a GLuint cast to ImTextureID.
        // Vulkan: typically a VkDescriptorSet cast to ImTextureID.
        virtual void* GetViewportTextureID() const = 0;

        // Called after scene render, before ImGui. Vulkan: ends viewport pass, transitions
        // viewport image to shader read, begins swapchain pass. OpenGL: no-op.
        virtual void PrepareForImGui() {}

        /** Returns the current/default shader (e.g. model shader). Ownership stays with the renderer. */
        virtual RHI_Shaders* GetShader() = 0;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H