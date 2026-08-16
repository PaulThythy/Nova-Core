#ifndef TEXTUREASSET_H
#define TEXTUREASSET_H

#include <filesystem>
#include <memory>

#include "Api.h"
#include "Asset/Asset.h"
#include "Renderer/RHI/RHI_Texture.h"

namespace Nova::Core::Asset::Assets {

    struct NV_API TextureAssetDesc {
        /** Force decode to RGBA8 (recommended). When false, keep source channel count. */
        bool m_ForceRGBA = true;
        /** Request an ImGui texture ID when the texture is uploaded via GetOrUploadTexture. */
        bool m_CreateImGuiID = true;
    };

    /**
     * Texture loaded from an external image file (PNG, JPG, TGA, BMP, … via stb_image).
     * `Load()` produces a CPU `RHI_Texture`; GPU upload is done by
     * `IRenderer::GetOrUploadTexture` (same pattern as MeshAsset / RHI_Mesh).
     */
    class NV_API TextureAsset final : public Asset {
    public:
        TextureAsset(std::filesystem::path path, TextureAssetDesc desc = {});

        bool Load();
        bool Reload();

        bool IsLoaded() const { return m_Loaded && m_CPUTexture != nullptr; }

        std::shared_ptr<Renderer::RHI::RHI_Texture> GetCPUTexture() const { return m_CPUTexture; }

        const TextureAssetDesc& GetDesc() const { return m_Desc; }

    private:
        TextureAssetDesc m_Desc;
        std::shared_ptr<Renderer::RHI::RHI_Texture> m_CPUTexture;
        bool m_Loaded = false;
    };

} // namespace Nova::Core::Asset::Assets

#endif // TEXTUREASSET_H