#ifndef SHADERASSET_H
#define SHADERASSET_H

#include "Asset/Asset.h"
#include "Renderer/RHI/RHI_Shaders.h"

namespace Nova::Core::Asset::Assets {

    class ShaderAsset final : public Asset{
    public:
        ShaderAsset(std::filesystem::path shaderPath, 
            Nova::Core::Renderer::RHI::RHI_ShaderDesc desc = {},
            Nova::Core::Renderer::RHI::RHI_ShaderCompileOptions options = {});

        bool Compile();

        // Force recompilation (utile reload)
        bool Recompile();

        // Accessors (après compile)
        const std::vector<uint32_t>& GetSpirv() const { return m_Spirv; }
        const std::string& GetGlsl() const { return m_Glsl; }
        const std::string& GetLastLog() const { return m_LastLog; }

        Nova::Core::Renderer::RHI::RHI_ShaderStage GetStage() const { return m_Stage; }

    private:
        bool CompileInternal(Nova::Core::GraphicsAPI api, bool force);

        Nova::Core::Renderer::RHI::RHI_ShaderDesc m_Desc;
        Nova::Core::Renderer::RHI::RHI_ShaderCompileOptions m_Options;

        Nova::Core::Renderer::RHI::RHI_ShaderStage m_Stage = Nova::Core::Renderer::RHI::RHI_ShaderStage::Unknown;

        bool m_CompiledVulkan = false;
        bool m_CompiledOpenGL = false;

        std::vector<uint32_t> m_Spirv;
        std::string m_Glsl;
        std::string m_LastLog;
    };

} // Nova::Core::Asset::Assets

#endif // SHADERASSET_H