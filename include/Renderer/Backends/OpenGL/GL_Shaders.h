#ifndef GL_SHADERS_HPP
#define GL_SHADERS_HPP

#include <glad/gl.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "Api.h"
#include "Core/Log.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    /** OpenGL shader program wrapper; derives from RHI_Shaders for uniform API. */
    class NV_API GL_Shaders final : public RHI::RHI_Shaders {
    public:
        GL_Shaders() = default;
        ~GL_Shaders() override;

        /** Set the program. Creates engine buffers for slots in EngineResourceSlot. Call after linking. */
        void SetProgram(GLuint program);

        /** Provide merged reflection (VS+FS) to drive resource binding. */
        void SetReflection(const RHI::RHI_ProgramReflection& reflection);

        // User resources (set/binding -> bindingPoint = set*64+binding).
        // These are backend-specific and will be wrapped by a higher-level RHI API.
        void SetUserUniformBuffer(uint32_t set, uint32_t binding, GLuint buffer);
        void SetUserStorageBuffer(uint32_t set, uint32_t binding, GLuint buffer);
        void SetUserTexture(uint32_t set, uint32_t binding, GLuint texture);
        void SetUserSampler(uint32_t set, uint32_t binding, GLuint sampler);

        static constexpr uint32_t kMaxBindingsPerSet = 64;
        static constexpr GLuint ToBindingPoint(uint32_t set, uint32_t binding) {
            return static_cast<GLuint>(set * kMaxBindingsPerSet + binding);
        }

        void Bind(void* apiContext = nullptr) override;
        void ApplyParameters(void* apiContext = nullptr) override;
        void* GetNativeHandle() const override;

        GLuint GetProgram() const { return m_Program; }
        GLuint GetMvpUniformBuffer() const { return m_BufMvp; }
        bool IsValid() const { return m_Program != 0; }

    private:
        bool ApplyResourceBinding(const RHI::RHI_BindingInfo& info, const RHI::RHI_ResourceBinding& value) override;

        GLint GetLocation(const std::string& name);

        /** Bind user UBO/SSBO/textures/samplers from the resource set (set/binding → unit). */
        void BindUserResources();
        /** Upload engine MVP block from m_Parameters. */
        void UploadMvpUniforms();
        /** Upload engine material block from m_Parameters. */
        void UploadMaterialUniforms();
        /** Upload engine per-frame block from m_Parameters. */
        void UploadFrameUniforms();
        /** Upload remaining parameters as standalone program uniforms. */
        void UploadStandaloneUniforms();
        /** Upload instance SSBO from the given array. */
        void UploadInstanceBuffer(const std::vector<RHI::Instance>& instances);

        GLuint m_Program{ 0 };
        GLuint m_BufMvp{ 0 };
        GLuint m_BufMaterial{ 0 };
        GLuint m_BufFrameUniforms{ 0 };
        GLuint m_BufInstances{ 0 };
        size_t m_InstanceBufferSize{ 0 };
        std::unordered_map<std::string, GLint> m_LocationCache;

        std::unordered_map<GLuint, GLuint> m_UserUniformBuffers; // bindingPoint -> buffer
        std::unordered_map<GLuint, GLuint> m_UserStorageBuffers; // bindingPoint -> buffer
        std::unordered_map<GLuint, GLuint> m_UserTextures;       // unit -> texture
        std::unordered_map<GLuint, GLuint> m_UserSamplers;       // unit -> sampler
    };

    // --- Free functions (compile/link helpers) ---
    NV_API GLuint CompileShader(GLenum shaderType, const std::string& shaderCode);
    NV_API GLuint LinkProgram(const std::initializer_list<GLuint>& shaderIDs);

    NV_API bool CheckGLProgram(GLuint prog, std::string& outLog);
    NV_API GLuint LoadSpirvShader(GLenum stage,
        const std::vector<uint32_t>& spirv,
        const char* entryPoint,
        std::string& outLog);

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_SHADERS_HPP