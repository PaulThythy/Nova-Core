#ifndef RHI_TEXTURE_H
#define RHI_TEXTURE_H

#include <cstdint>
#include <memory>
#include <vector>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    /**
     * CPU-side texture (pixels in system memory), analogous to `RHI_Mesh`.
     * Upload to GPU via `IRenderer::GetOrUploadTexture` (backend returns a cached GPU subclass).
     */
    struct NV_API RHI_Texture {
        RHI_Texture() = default;
        RHI_Texture(uint32_t width, uint32_t height, std::vector<uint8_t> rgbaPixels, bool createImGuiID = true)
            : m_Width(width),
              m_Height(height),
              m_Pixels(std::move(rgbaPixels)),
              m_CreateImGuiID(createImGuiID)
        {
        }

        virtual ~RHI_Texture() = default;

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }
        std::vector<uint8_t>& GetPixels() { return m_Pixels; }
        bool WantsImGuiID() const { return m_CreateImGuiID; }

        virtual void Upload(const RHI_Texture& src);
        virtual void Release();

        /** ImGui descriptor after GPU upload; nullptr on CPU-only instances. */
        virtual void* GetImGuiID() const { return nullptr; }

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        std::vector<uint8_t> m_Pixels; // RGBA8
        bool m_CreateImGuiID = true;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_TEXTURE_H