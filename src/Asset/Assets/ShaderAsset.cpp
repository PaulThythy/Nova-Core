#include "Asset/Assets/ShaderAsset.h"
#include "Core/Application.h"
#include "Core/Log.h"

namespace Nova::Core::Asset::Assets {

    using namespace Nova::Core;
    using namespace Nova::Core::Renderer;

    ShaderAsset::ShaderAsset(std::filesystem::path shaderPath, RHI::RHI_ShaderCompileInput compileInput)
        : Asset(AssetType::Shader, std::move(shaderPath)),
        m_Input(std::move(compileInput))
    {
        m_Input.m_File = m_Path;

        if (m_Input.m_Stage == RHI::RHI_ShaderStage::Unknown) {
            m_Input.m_Stage = RHI::ShaderStageFromFileExtension(m_Path);
        }
    }

    const std::vector<uint32_t>& ShaderAsset::GetSpirv() const {
        return GetSpirv(m_LastCompiledApi);
    }

    const std::string& ShaderAsset::GetSource() const {
        return GetSource(m_LastCompiledApi);
    }

    const std::vector<uint32_t>& ShaderAsset::GetSpirv(GraphicsAPI api) const {
        return (api == GraphicsAPI::OpenGL) ? m_SpirvOpenGL : m_SpirvVulkan;
    }

    const std::string& ShaderAsset::GetSource(GraphicsAPI api) const {
        return (api == GraphicsAPI::OpenGL) ? m_SourceOpenGL : m_SourceVulkan;
    }

    bool ShaderAsset::Compile() {
        GraphicsAPI api = Application::Get().GetWindow().GetGraphicsAPI();
        return CompileInternal(api, false);
    }

    bool ShaderAsset::Recompile() {
        GraphicsAPI api = Application::Get().GetWindow().GetGraphicsAPI();
        return CompileInternal(api, true);
    }

    bool ShaderAsset::CompileInternal(GraphicsAPI api, bool force) {
        if (api == GraphicsAPI::Vulkan && m_CompiledVulkan && !force) {
            m_LastCompiledApi = api;
            return true;
        }
        if (api == GraphicsAPI::OpenGL && m_CompiledOpenGL && !force) {
            m_LastCompiledApi = api;
            return true;
        }

        RHI::RHI_ShaderCompileInput opts = m_Input;
        opts.m_TargetApi = api;

        const std::filesystem::path engineShaderRoot =
            std::filesystem::current_path() / "Nova-Core" / "Resources" / "Engine" / "Shaders";

        const std::filesystem::path editorShaderRoot =
            std::filesystem::current_path() / "Nova-Editor" / "Resources" / "Editor" / "Shaders";

        opts.m_IncludeDirs.push_back(engineShaderRoot);
        opts.m_IncludeDirs.push_back(editorShaderRoot);

        opts.m_File = m_Path;
        opts.m_SkipCache = force;

        if (api == GraphicsAPI::Vulkan) {
            opts.m_Defines.emplace_back("NOVA_VULKAN", "1");
        }
        else {
            opts.m_Defines.emplace_back("NOVA_OPENGL", "1");
        }

        RHI::RHI_ShaderCompileResult out = RHI::RHI_ShaderCompiler::Compile(opts);

        m_LastLog = out.m_Log;
        if (!out.m_Success) {
            if (api == GraphicsAPI::Vulkan) m_CompiledVulkan = false;
            if (api == GraphicsAPI::OpenGL) m_CompiledOpenGL = false;
            return false;
        }

        if (api == GraphicsAPI::Vulkan) {
            m_SpirvVulkan = std::move(out.m_Spirv);
            m_SourceVulkan = std::move(out.m_Source);
            m_CompiledVulkan = true;
        }
        else {
            m_SpirvOpenGL = std::move(out.m_Spirv);
            m_SourceOpenGL = std::move(out.m_Source);
            m_CompiledOpenGL = true;
        }

        m_Input.m_Stage = out.m_Stage;
        m_LastCompiledApi = api;
        return true;
    }


} // namespace Nova::Core::Asset::Assets
