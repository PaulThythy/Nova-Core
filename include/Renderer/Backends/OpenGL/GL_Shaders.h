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

namespace Nova::Core::Renderer::Backends::OpenGL {

    /** OpenGL shader program wrapper; derives from RHI_Shaders for uniform API. */
    class NV_API GL_Shaders final : public RHI::RHI_Shaders {
    public:
        GL_Shaders() = default;
        ~GL_Shaders() override;

        /** Set the program. Creates engine buffers for slots in EngineResourceSlot. Call after linking. */
        void SetProgram(GLuint program);

        void Bind(void* apiContext = nullptr) override;
        void ApplyParameters(void* apiContext = nullptr) override;
        void SetInstanceData(const std::vector<RHI::Instance>& instances) override;
        void* GetNativeHandle() const override;

        GLuint GetProgram() const { return m_Program; }
        GLuint GetMvpUniformBuffer() const { return m_BufMvp; }
        bool IsValid() const { return m_Program != 0; }

    private:
        GLint GetLocation(const std::string& name);
        void UploadMaterialUBO();
        void UploadGlobalsUBO();

        GLuint m_Program{ 0 };
        GLuint m_BufMvp{ 0 };
        GLuint m_BufMaterial{ 0 };
        GLuint m_BufGlobals{ 0 };
        GLuint m_BufInstances{ 0 };
        size_t m_InstanceBufferSize{ 0 };
        std::unordered_map<std::string, GLint> m_LocationCache;
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