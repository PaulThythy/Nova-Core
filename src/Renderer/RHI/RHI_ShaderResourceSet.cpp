#include "Renderer/RHI/RHI_ShaderResourceSet.h"

#include "Renderer/RHI/RHI_Shaders.h"

namespace Nova::Core::Renderer::RHI {

    bool RHI_ShaderResourceSet::SetBuffer(const std::string& name, uint64_t handle, uint64_t offset, uint64_t range) {
        if (!m_Reflection) return false;
        auto it = m_Reflection->m_NameToBinding.find(name);
        if (it == m_Reflection->m_NameToBinding.end()) return false;
        const auto* info = m_Reflection->FindBinding(it->second.m_Set, it->second.m_Binding);
        if (!info) return false;
        if (info->m_Kind != RHI_ResourceKind::ConstantBuffer && info->m_Kind != RHI_ResourceKind::StorageBuffer &&
            info->m_Kind != RHI_ResourceKind::RWBuffer)
            return false;
        m_Bindings[name] = RHI_BufferBinding{ handle, offset, range };
        return true;
    }

    bool RHI_ShaderResourceSet::SetTexture(const std::string& name, uint64_t textureHandle, uint32_t imageLayout) {
        if (!m_Reflection) return false;
        auto it = m_Reflection->m_NameToBinding.find(name);
        if (it == m_Reflection->m_NameToBinding.end()) return false;
        const auto* info = m_Reflection->FindBinding(it->second.m_Set, it->second.m_Binding);
        if (!info) return false;
        if (info->m_Kind != RHI_ResourceKind::Texture && info->m_Kind != RHI_ResourceKind::CombinedTextureSampler &&
            info->m_Kind != RHI_ResourceKind::RWTexture)
            return false;
        m_Bindings[name] = RHI_TextureBinding{ textureHandle, imageLayout };
        return true;
    }

    bool RHI_ShaderResourceSet::SetSampler(const std::string& name, uint64_t samplerHandle) {
        if (!m_Reflection) return false;
        auto it = m_Reflection->m_NameToBinding.find(name);
        if (it == m_Reflection->m_NameToBinding.end()) return false;
        const auto* info = m_Reflection->FindBinding(it->second.m_Set, it->second.m_Binding);
        if (!info) return false;
        if (info->m_Kind != RHI_ResourceKind::Sampler && info->m_Kind != RHI_ResourceKind::CombinedTextureSampler)
            return false;
        m_Bindings[name] = RHI_SamplerBinding{ samplerHandle };
        return true;
    }

    bool RHI_ShaderResourceSet::Apply(void* shader) const {
        if (!m_Reflection || !shader) return false;
        auto* rhiShader = static_cast<RHI_Shaders*>(shader);

        for (const auto& [name, value] : m_Bindings) {
            auto itKey = m_Reflection->m_NameToBinding.find(name);
            if (itKey == m_Reflection->m_NameToBinding.end()) continue;

            const auto* info = m_Reflection->FindBinding(itKey->second.m_Set, itKey->second.m_Binding);
            if (!info) continue;

            (void)rhiShader->ApplyResourceBinding(*info, value);
        }

        return true;
    }

} // namespace Nova::Core::Renderer::RHI