#ifndef RHI_SHADER_UNIFORMS_H
#define RHI_SHADER_UNIFORMS_H

/**
 * Centralized definition of all variables passed to shaders (UBO, push constants).
 * Only one file to keep track of them. Bindings are fixed here.
 *
 * Bindings:
 *   0 = UBO_Globals                          — OpenGL & Vulkan
 *   1 = UBO_MVP (model, view, proj)          — OpenGL & Vulkan
 *   2 = SSBO_InstanceData                    — OpenGL & Vulkan
 *   3 = UBO_Material (u_Color, etc.)         — OpenGL & Vulkan
 */

#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {
    // =========================================================================
    // Binding 0 — Globals (resolution, time, frame, mouse, date) — one block per frame
    // UBO in opengl, PushConstants in Vulkan
    // =========================================================================
    struct NV_API Globals {
        alignas(16) glm::vec3 iResolution{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     iTime{ 0.0f };
        alignas(4)  float     iTimeDelta{ 0.0f };
        alignas(4)  float     iFrameRate{ 0.0f };
        alignas(4)  int       iFrame{ 0 };
        alignas(4)  int       u_UseInstancing{ 0 };
        alignas(8)  glm::ivec2 _Offset0{ 0, 0 };
        alignas(16) glm::vec4 iMouse{ 0.0f, 0.0f, 0.0f, 0.0f };  // xy: current, zw: click
        alignas(16) glm::vec4 iDate{ 0.0f, 0.0f, 0.0f, 0.0f };   // year, month, day, time in seconds
    };

    // =========================================================================
    // Binding 1 — MVP (OpenGL & Vulkan, UBO)
    // =========================================================================
    struct NV_API UBO_MVP {
        alignas(16) glm::mat4 model{ 1.0f };
        alignas(16) glm::mat4 view{ 1.0f };
        alignas(16) glm::mat4 proj{ 1.0f };

        alignas(16) glm::mat4 viewProj{ 1.0f };
        alignas(16) glm::mat4 invViewProj{ 1.0f };
    };

    // =========================================================================
    // Binding 2 — Per-instance data (OpenGL & Vulkan, SSBO)
    // =========================================================================
    struct NV_API SSBO_InstanceData {
        alignas(16) glm::mat4 model{ 1.0f };
        alignas(16) glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // =========================================================================
    // Binding 3 — Material (u_Color, etc.) — (OpenGL & Vulkan, UBO)
    // =========================================================================
    struct NV_API UBO_Material {
        alignas(16) glm::vec4 u_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // =========================================================================
    // Layout Material : name -> offset (to populate UBO_Material from m_Parameters)
    // =========================================================================
    inline std::unordered_map<std::string, size_t> GetMaterialUBOLayout() {
        return {
            { "u_Color", offsetof(UBO_Material, u_Color) }
        };
    }

    // =========================================================================
    // Layout Globals : name -> offset (to populate Globals from m_Parameters)
    // =========================================================================
    inline std::unordered_map<std::string, size_t> GetGlobalsLayout() {
        return {
            { "iResolution",  offsetof(Globals, iResolution)  },
            { "iTime",        offsetof(Globals, iTime)        },
            { "iTimeDelta",   offsetof(Globals, iTimeDelta)   },
            { "iFrameRate",   offsetof(Globals, iFrameRate)   },
            { "iFrame",       offsetof(Globals, iFrame)       },
            { "u_UseInstancing", offsetof(Globals, u_UseInstancing) },
            { "iMouse",       offsetof(Globals, iMouse)       },
            { "iDate",        offsetof(Globals, iDate)        },
        };
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_UNIFORMS_H
