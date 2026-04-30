#ifndef SHADERASSET_H
#define SHADERASSET_H

#include "Api.h"
#include "Asset/Asset.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"

namespace Nova::Core::Asset::Assets {

    class NV_API ShaderAsset final : public Asset{
    public:
        ShaderAsset(std::filesystem::path shaderPath,
            Nova::Core::Renderer::RHI::RHI_ShaderCompileInput compileInput = {});

        bool Compile();

        // Force recompilation, useful for hot reload workflows.
        bool Recompile();

        // Accessors populated after compilation.
        const std::vector<uint32_t>& GetSpirv() const;
        // Source .slang conservé après compilation (débogage).
        const std::string& GetSource() const;
        const std::string& GetLastLog() const { return m_LastLog; }

        const std::vector<uint32_t>& GetSpirv(Nova::Core::GraphicsAPI api) const;
        const std::string& GetSource(Nova::Core::GraphicsAPI api) const;

        const Nova::Core::Renderer::RHI::RHI_ProgramReflection& GetReflection() const;
        const Nova::Core::Renderer::RHI::RHI_ProgramReflection& GetReflection(Nova::Core::GraphicsAPI api) const;

        Nova::Core::Renderer::RHI::RHI_ShaderStage GetStage() const { return m_Input.m_Stage; }

    private:
        bool CompileInternal(Nova::Core::GraphicsAPI api, bool force);

        Nova::Core::Renderer::RHI::RHI_ShaderCompileInput m_Input;

        bool m_CompiledVulkan = false;
        bool m_CompiledOpenGL = false;

        std::vector<uint32_t> m_SpirvVulkan;
        std::vector<uint32_t> m_SpirvOpenGL;

        std::string m_SourceVulkan;
        std::string m_SourceOpenGL;

        Nova::Core::Renderer::RHI::RHI_ProgramReflection m_ReflectionVulkan{};
        Nova::Core::Renderer::RHI::RHI_ProgramReflection m_ReflectionOpenGL{};

        Nova::Core::GraphicsAPI m_LastCompiledApi = Nova::Core::GraphicsAPI::Vulkan;
        std::string m_LastLog;
    };

} // Nova::Core::Asset::Assets

#endif // SHADERASSET_H