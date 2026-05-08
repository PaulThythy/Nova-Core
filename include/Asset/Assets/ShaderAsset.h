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
        const std::vector<uint8_t>& GetBinary() const;
        Nova::Core::Renderer::RHI::RHI_ShaderBinaryFormat GetBinaryFormat() const;
        // Source .slang conservé après compilation (débogage).
        const std::string& GetSource() const;
        const std::string& GetLastLog() const { return m_LastLog; }

        const std::vector<uint8_t>& GetBinary(Nova::Core::GraphicsAPI api) const;
        Nova::Core::Renderer::RHI::RHI_ShaderBinaryFormat GetBinaryFormat(Nova::Core::GraphicsAPI api) const;
        const std::string& GetSource(Nova::Core::GraphicsAPI api) const;

        const Nova::Core::Renderer::RHI::RHI_ProgramReflection& GetReflection() const;
        const Nova::Core::Renderer::RHI::RHI_ProgramReflection& GetReflection(Nova::Core::GraphicsAPI api) const;

        Nova::Core::Renderer::RHI::RHI_ShaderStage GetStage() const { return m_Input.m_Stage; }

    private:
        bool CompileInternal(Nova::Core::GraphicsAPI api, bool force);

        Nova::Core::Renderer::RHI::RHI_ShaderCompileInput m_Input;

        bool m_CompiledVulkan = false;

        Nova::Core::Renderer::RHI::RHI_ShaderBinaryFormat m_FormatVulkan =
            Nova::Core::Renderer::RHI::RHI_ShaderBinaryFormat::Unknown;
        std::vector<uint8_t> m_BinaryVulkan;

        std::string m_SourceVulkan;

        Nova::Core::Renderer::RHI::RHI_ProgramReflection m_ReflectionVulkan{};

        Nova::Core::GraphicsAPI m_LastCompiledApi = Nova::Core::GraphicsAPI::Vulkan;
        std::string m_LastLog;
    };

} // Nova::Core::Asset::Assets

#endif // SHADERASSET_H