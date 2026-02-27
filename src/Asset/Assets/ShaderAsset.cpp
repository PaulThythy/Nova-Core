#include "Asset/Assets/ShaderAsset.h"
#include "Core/Application.h"
#include "Core/Log.h"

namespace Nova::Core::Asset::Assets {

    using namespace Nova::Core;
    using namespace Nova::Core::Renderer;

    ShaderAsset::ShaderAsset(std::filesystem::path shaderPath,
        RHI::RHI_ShaderDesc desc,
        RHI::RHI_ShaderCompileOptions options)
        : Asset(AssetType::Shader, std::move(shaderPath)),
        m_Desc(std::move(desc)),
        m_Options(std::move(options))
    {
        // Le filepath vient de Asset::m_Path => on l’impose ici
        m_Desc.m_FilePath = m_Path;

        // Déduire le stage si besoin
        if (m_Desc.m_Stage == RHI::RHI_ShaderStage::Unknown) {
            m_Desc.m_Stage = RHI::ShaderStageFromFileExtension(m_Path);
        }
    }

    const std::vector<uint32_t>& ShaderAsset::GetSpirv() const {
        return GetSpirv(m_LastCompiledApi);
    }

    const std::string& ShaderAsset::GetGlsl() const {
        return GetGlsl(m_LastCompiledApi);
    }

    const std::vector<uint32_t>& ShaderAsset::GetSpirv(GraphicsAPI api) const {
        return (api == GraphicsAPI::OpenGL) ? m_SpirvOpenGL : m_SpirvVulkan;
    }

    const std::string& ShaderAsset::GetGlsl(GraphicsAPI api) const {
        return (api == GraphicsAPI::OpenGL) ? m_GlslOpenGL : m_GlslVulkan;
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

        RHI::RHI_ShaderCompileOptions opts = m_Options;
        opts.m_TargetApi = api;

        const std::filesystem::path engineShaderRoot =
            std::filesystem::current_path() / "Nova-Core" / "Resources" / "Engine" / "Shaders";

        const std::filesystem::path editorShaderRoot =
            std::filesystem::current_path() / "Nova-Editor" / "Resources" / "Editor" / "Shaders";

        opts.m_IncludeDirs.push_back(engineShaderRoot);
        opts.m_IncludeDirs.push_back(editorShaderRoot);

        RHI::RHI_ShaderDesc shaderDesc = m_Desc;
        shaderDesc.m_FilePath = m_Path;

        // --- Defines "plateforme" minimalistes ---
        if (api == GraphicsAPI::Vulkan) {
            opts.m_Definitions.emplace_back("gl_VertexID", "gl_VertexIndex");
            opts.m_Definitions.emplace_back("NOVA_VULKAN", "1");
        }
        else {
            opts.m_Definitions.emplace_back("gl_VertexIndex", "gl_VertexID");
            opts.m_Definitions.emplace_back("NOVA_OPENGL", "1");
        }

        RHI::RHI_ShaderCompilationOutput out{};
        const bool ok = RHI::CompileShader(shaderDesc, opts, out);

        m_LastLog = out.m_Log;
        if (!ok) {
            if (api == GraphicsAPI::Vulkan) m_CompiledVulkan = false;
            if (api == GraphicsAPI::OpenGL) m_CompiledOpenGL = false;
            return false;
        }

        // On stocke TOUJOURS le SPIR-V (désormais commun aux 2 backends)
        if (api == GraphicsAPI::Vulkan) {
            m_SpirvVulkan = std::move(out.m_Spirv);
            m_GlslVulkan = std::move(out.m_Glsl); // debug éventuel
            m_CompiledVulkan = true;
        }
        else {
            m_SpirvOpenGL = std::move(out.m_Spirv);
            m_GlslOpenGL = std::move(out.m_Glsl); // debug éventuel
            m_CompiledOpenGL = true;
        }

        m_Desc.m_Stage = out.m_Stage;
        m_LastCompiledApi = api;
        return true;
    }


} // namespace Nova::Core::Asset::Assets
