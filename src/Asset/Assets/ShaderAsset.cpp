#include "Asset/Assets/ShaderAsset.h"
#include "Core/Application.h"

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

    bool ShaderAsset::Compile() {
        GraphicsAPI api = Application::Get().GetWindow().GetGraphicsAPI();
        return CompileInternal(api, false);
    }

    bool ShaderAsset::Recompile() {
        GraphicsAPI api = Application::Get().GetWindow().GetGraphicsAPI();
        return CompileInternal(api, true);
    }

    bool ShaderAsset::CompileInternal(GraphicsAPI api, bool force) {
        if (api == GraphicsAPI::Vulkan && m_CompiledVulkan && !force) return true;
        if (api == GraphicsAPI::OpenGL && m_CompiledOpenGL && !force) return true;

        // On part d’une copie des options, et on override la target API
        RHI::RHI_ShaderCompileOptions opts = m_Options;
        opts.m_TargetApi = api;

        // On s’assure que le filepath est bien celui de l’asset
        RHI::RHI_ShaderDesc shaderDesc = m_Desc;
        shaderDesc.m_FilePath = m_Path;

        RHI::RHI_ShaderCompilationOutput out{};
        const bool ok = RHI::CompileShader(shaderDesc, opts, out);

        m_LastLog = out.m_Log;

        if (!ok) {
            if (api == GraphicsAPI::Vulkan) m_CompiledVulkan = false;
            if (api == GraphicsAPI::OpenGL) m_CompiledOpenGL = false;
            return false;
        }

        if (api == GraphicsAPI::Vulkan) {
            m_Spirv = std::move(out.m_Spirv);
            m_CompiledVulkan = true;
        }
        else {
            m_Glsl = std::move(out.m_Glsl);
            m_CompiledOpenGL = true;
        }

        // au cas où le stage a été ajusté
        m_Desc.m_Stage = out.m_Stage;
        return true;
    }


} // namespace Nova::Core::Asset::Assets
