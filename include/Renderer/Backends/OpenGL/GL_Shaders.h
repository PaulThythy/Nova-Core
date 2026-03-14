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

        /** Set the program. Creates UBOs: binding 0 = MVP, 1 = Material, 2 = Globals. Call after linking. */
        void SetProgram(GLuint program);

        void Bind(void* apiContext = nullptr) override;
        void ApplyParameters(void* apiContext = nullptr) override;
        void* GetNativeHandle() const override;

        GLuint GetProgram() const { return m_Program; }
        GLuint GetUBO_MVP() const { return m_UBO_MVP; }
        bool IsValid() const { return m_Program != 0; }

    private:
        GLint GetLocation(const std::string& name);
        void UploadMaterialUBO();
        void UploadGlobalsUBO();

        GLuint m_Program{ 0 };
        GLuint m_UBO_MVP{ 0 };
        GLuint m_UBO_Material{ 0 };
        GLuint m_Globals{ 0 };
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