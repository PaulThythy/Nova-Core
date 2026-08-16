#include "Asset/Assets/TextureAsset.h"

#include <string>
#include <vector>

#include "Core/Log.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Nova::Core::Asset::Assets {

    TextureAsset::TextureAsset(std::filesystem::path path, TextureAssetDesc desc)
        : Asset(AssetType::Texture, std::move(path)),
          m_Desc(desc)
    {
    }

    bool TextureAsset::Load() {
        if (m_Loaded && m_CPUTexture)
            return true;

        m_CPUTexture.reset();

        const std::string pathStr = m_Path.string();
        const int desiredChannels = m_Desc.m_ForceRGBA ? 4 : 0;

        int w = 0, h = 0, n = 0;
        stbi_uc* data = stbi_load(pathStr.c_str(), &w, &h, &n, desiredChannels);
        if (!data) {
            const char* reason = stbi_failure_reason();
            NV_LOG_WARN(("TextureAsset::Load failed for '" + pathStr + "': " +
                         (reason ? reason : "unknown")).c_str());
            return false;
        }

        const int channels = desiredChannels > 0 ? desiredChannels : n;
        if (channels != 4) {
            stbi_image_free(data);
            NV_LOG_WARN("TextureAsset::Load currently requires RGBA8 pixel data.");
            return false;
        }

        const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
        std::vector<uint8_t> pixels(data, data + byteCount);
        stbi_image_free(data);

        m_CPUTexture = std::make_shared<Renderer::RHI::RHI_Texture>(
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            std::move(pixels),
            m_Desc.m_CreateImGuiID);

        m_Loaded = true;
        return true;
    }

    bool TextureAsset::Reload() {
        m_Loaded = false;
        m_CPUTexture.reset();
        return Load();
    }

} // namespace Nova::Core::Asset::Assets
