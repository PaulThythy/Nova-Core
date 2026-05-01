#ifndef RHI_SHADER_RESOURCE_SET_H
#define RHI_SHADER_RESOURCE_SET_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

#include "Api.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"

namespace Nova::Core::Renderer::RHI {

    struct NV_API RHI_BufferBinding {
        uint64_t m_Handle = 0; // VkBuffer or GLuint (cast)
        uint64_t m_Offset = 0;
        uint64_t m_Range = 0;  // 0 = VK_WHOLE_SIZE / full buffer
    };

    struct NV_API RHI_TextureBinding {
        uint64_t m_TextureHandle = 0; // VkImageView or GLuint texture
        uint32_t m_ImageLayout = 0;   // Vulkan only (VkImageLayout). 0 = default/ignore.
    };

    struct NV_API RHI_SamplerBinding {
        uint64_t m_SamplerHandle = 0; // VkSampler or GLuint sampler
    };

    using RHI_ResourceBinding = std::variant<RHI_BufferBinding, RHI_TextureBinding, RHI_SamplerBinding>;

    /**
     * Backend-agnostic shader resource binder: set resources by reflection name.
     *
     * - Names come from `RHI_ProgramReflection::m_NameToBinding`, e.g. "nova.frame" or "user.albedo".
     * - Engine resources (set 0) are typically owned/bound by the renderer; this class is intended
     *   primarily for user resources in set 1.
     */
    class NV_API RHI_ShaderResourceSet {
    public:
        explicit RHI_ShaderResourceSet(const RHI_ProgramReflection* reflection = nullptr)
            : m_Reflection(reflection) {}

        void SetReflection(const RHI_ProgramReflection* reflection) { m_Reflection = reflection; }

        bool SetBuffer(const std::string& name, uint64_t handle, uint64_t offset = 0, uint64_t range = 0);
        bool SetTexture(const std::string& name, uint64_t textureHandle, uint32_t imageLayout = 0);
        bool SetSampler(const std::string& name, uint64_t samplerHandle);

        /** Apply stored bindings to a backend shader object. */
        bool Apply(void* shader) const;

    private:
        const RHI_ProgramReflection* m_Reflection = nullptr;
        std::unordered_map<std::string, RHI_ResourceBinding> m_Bindings;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_RESOURCE_SET_H
