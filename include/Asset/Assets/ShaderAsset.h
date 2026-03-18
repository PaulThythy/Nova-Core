#ifndef SHADERASSET_H
#define SHADERASSET_H

#include "Api.h"
#include "Asset/Asset.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"

namespace Nova::Core::Asset::Assets {

    class NV_API ShaderAsset final : public Asset{
    public:
        ShaderAsset(std::filesystem::path shaderPath, 
            Nova::Core::Renderer::RHI::RHI_ShaderDesc desc = {},
            Nova::Core::Renderer::RHI::RHI_ShaderCompileOptions options = {});

        bool Compile();

        // Force recompilation, useful for hot reload workflows.
        bool Recompile();

        // Accessors populated after compilation.
        const std::vector<uint32_t>& GetSpirv() const;
        const std::string& GetGlsl() const;
        const std::string& GetLastLog() const { return m_LastLog; }

        const std::vector<uint32_t>& GetSpirv(Nova::Core::GraphicsAPI api) const;
        const std::string& GetGlsl(Nova::Core::GraphicsAPI api) const;

        Nova::Core::Renderer::RHI::RHI_ShaderStage GetStage() const { return m_Stage; }

    private:
        bool CompileInternal(Nova::Core::GraphicsAPI api, bool force);

        Nova::Core::Renderer::RHI::RHI_ShaderDesc m_Desc;
        Nova::Core::Renderer::RHI::RHI_ShaderCompileOptions m_Options;

        Nova::Core::Renderer::RHI::RHI_ShaderStage m_Stage = Nova::Core::Renderer::RHI::RHI_ShaderStage::Unknown;

        bool m_CompiledVulkan = false;
        bool m_CompiledOpenGL = false;

        std::vector<uint32_t> m_SpirvVulkan;
        std::vector<uint32_t> m_SpirvOpenGL;

        std::string m_GlslVulkan;
        std::string m_GlslOpenGL;

        Nova::Core::GraphicsAPI m_LastCompiledApi = Nova::Core::GraphicsAPI::Vulkan;
        std::string m_LastLog;
    };

} // Nova::Core::Asset::Assets

#endif // SHADERASSET_H