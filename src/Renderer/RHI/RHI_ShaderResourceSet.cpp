#include "Renderer/RHI/RHI_ShaderResourceSet.h"

#include "Core/GraphicsAPI.h"

// Backend implementations (used via dynamic_cast in Apply()).
#include "Renderer/Backends/OpenGL/GL_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"

#include <vulkan/vulkan.h>

namespace Nova::Core::Renderer::RHI {

    bool RHI_ShaderResourceSet::SetBuffer(const std::string& name, uint64_t handle, uint64_t offset, uint64_t range) {
        if (!m_Reflection) return false;
        if (!m_Reflection->nameToBinding.contains(name)) return false;
        m_Bindings[name] = RHI_BufferBinding{ handle, offset, range };
        return true;
    }

    bool RHI_ShaderResourceSet::SetTexture(const std::string& name, uint64_t textureHandle, uint32_t imageLayout) {
        if (!m_Reflection) return false;
        if (!m_Reflection->nameToBinding.contains(name)) return false;
        m_Bindings[name] = RHI_TextureBinding{ textureHandle, imageLayout };
        return true;
    }

    bool RHI_ShaderResourceSet::SetSampler(const std::string& name, uint64_t samplerHandle) {
        if (!m_Reflection) return false;
        if (!m_Reflection->nameToBinding.contains(name)) return false;
        m_Bindings[name] = RHI_SamplerBinding{ samplerHandle };
        return true;
    }

    static VkDescriptorType ResourceKindToVkDescriptorType(RHI_ResourceKind kind) {
        switch (kind) {
            case RHI_ResourceKind::ConstantBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case RHI_ResourceKind::StorageBuffer:  return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case RHI_ResourceKind::Texture:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case RHI_ResourceKind::Sampler:        return VK_DESCRIPTOR_TYPE_SAMPLER;
            case RHI_ResourceKind::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case RHI_ResourceKind::RWTexture:      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case RHI_ResourceKind::RWBuffer:       return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            default:                               return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }

    bool RHI_ShaderResourceSet::Apply(void* shader) const {
        if (!m_Reflection || !shader) return false;

        // --- OpenGL backend ---
        if (auto* gl = dynamic_cast<Nova::Core::Renderer::Backends::OpenGL::GL_Shaders*>(static_cast<RHI::RHI_Shaders*>(shader))) {
            for (const auto& [name, value] : m_Bindings) {
                auto itKey = m_Reflection->nameToBinding.find(name);
                if (itKey == m_Reflection->nameToBinding.end()) continue;

                const auto* info = m_Reflection->FindBinding(itKey->second.set, itKey->second.binding);
                if (!info) continue;

                const uint32_t set = itKey->second.set;
                const uint32_t binding = itKey->second.binding;

                if (std::holds_alternative<RHI_BufferBinding>(value)) {
                    const auto b = std::get<RHI_BufferBinding>(value);
                    const GLuint buf = static_cast<GLuint>(b.handle);
                    if (info->kind == RHI_ResourceKind::ConstantBuffer)
                        gl->SetUserUniformBuffer(set, binding, buf);
                    else if (info->kind == RHI_ResourceKind::StorageBuffer)
                        gl->SetUserStorageBuffer(set, binding, buf);
                } else if (std::holds_alternative<RHI_TextureBinding>(value)) {
                    const auto t = std::get<RHI_TextureBinding>(value);
                    gl->SetUserTexture(set, binding, static_cast<GLuint>(t.textureHandle));
                } else if (std::holds_alternative<RHI_SamplerBinding>(value)) {
                    const auto s = std::get<RHI_SamplerBinding>(value);
                    gl->SetUserSampler(set, binding, static_cast<GLuint>(s.samplerHandle));
                }
            }
            return true;
        }

        // --- Vulkan backend ---
        if (auto* vk = dynamic_cast<Nova::Core::Renderer::Backends::Vulkan::VK_Shaders*>(static_cast<RHI::RHI_Shaders*>(shader))) {
            // VK_Shaders owns the VkDevice and descriptor sets; we update the user descriptor set (set 1).
            // Note: this assumes `vk->SetSceneBuffers(..., userDescriptorSet)` has been called.
            for (const auto& [name, value] : m_Bindings) {
                auto itKey = m_Reflection->nameToBinding.find(name);
                if (itKey == m_Reflection->nameToBinding.end()) continue;

                const auto* info = m_Reflection->FindBinding(itKey->second.set, itKey->second.binding);
                if (!info) continue;

                const VkDescriptorType dtype = ResourceKindToVkDescriptorType(info->kind);
                if (dtype == VK_DESCRIPTOR_TYPE_MAX_ENUM) continue;

                const uint32_t binding = itKey->second.binding;

                if (std::holds_alternative<RHI_BufferBinding>(value)) {
                    const auto b = std::get<RHI_BufferBinding>(value);
                    VkDescriptorBufferInfo bi{};
                    bi.buffer = reinterpret_cast<VkBuffer>(b.handle);
                    bi.offset = static_cast<VkDeviceSize>(b.offset);
                    bi.range = (b.range == 0) ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(b.range);
                    vk->WriteUserDescriptor(binding, dtype, &bi, nullptr);
                } else if (std::holds_alternative<RHI_TextureBinding>(value)) {
                    const auto t = std::get<RHI_TextureBinding>(value);
                    VkDescriptorImageInfo ii{};
                    ii.imageView = reinterpret_cast<VkImageView>(t.textureHandle);
                    ii.imageLayout = (t.imageLayout == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : static_cast<VkImageLayout>(t.imageLayout);
                    vk->WriteUserDescriptor(binding, dtype, nullptr, &ii);
                } else if (std::holds_alternative<RHI_SamplerBinding>(value)) {
                    const auto s = std::get<RHI_SamplerBinding>(value);
                    VkDescriptorImageInfo ii{};
                    ii.sampler = reinterpret_cast<VkSampler>(s.samplerHandle);
                    vk->WriteUserDescriptor(binding, dtype, nullptr, &ii);
                }
            }
            return true;
        }

        return false;
    }

} // namespace Nova::Core::Renderer::RHI