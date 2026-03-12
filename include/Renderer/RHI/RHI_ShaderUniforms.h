#ifndef RHI_SHADER_UNIFORMS_H
#define RHI_SHADER_UNIFORMS_H

/**
 * Centralized definition of all variables passed to shaders (UBO, push constants).
 * Only one file to keep track of them. Bindings are fixed here.
 *
 * Bindings:
 *   0 = UBO_MVP (model, view, proj)          — OpenGL & Vulkan
 *   1 = UBO_Material (u_Color, etc.)         — OpenGL & Vulkan
 *   2 = UBO_Globals (iResolution, iTime…)    — OpenGL = UBO ; Vulkan = push constants
 */

#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace Nova::Core::Renderer::RHI {

    // =========================================================================
    // Binding 0 — MVP (OpenGL & Vulkan, UBO)
    // =========================================================================
    struct UBO_MVP {
        alignas(16) glm::mat4 model{ 1.0f };
        alignas(16) glm::mat4 view{ 1.0f };
        alignas(16) glm::mat4 proj{ 1.0f };
    };

    // =========================================================================
    // Binding 1 — Material (u_Color, etc.) — OpenGL & Vulkan, UBO
    // =========================================================================
    struct UBO_Material {
        alignas(16) glm::vec4 u_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // =========================================================================
    // Globals (resolution, time, frame, mouse, date) — one block per frame
    // Vulkan: push constants. OpenGL: UBO binding 2.
    // =========================================================================
    struct Globals {
        alignas(16) glm::vec3 iResolution{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     iTime{ 0.0f };
        alignas(4)  float     iTimeDelta{ 0.0f };
        alignas(4)  float     iFrameRate{ 0.0f };
        alignas(4)  int       iFrame{ 0 };
        alignas(16) glm::vec4 iMouse{ 0.0f, 0.0f, 0.0f, 0.0f };  // xy: current, zw: click
        alignas(16) glm::vec4 iDate{ 0.0f, 0.0f, 0.0f, 0.0f };   // year, month, day, time in seconds
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
            { "iMouse",       offsetof(Globals, iMouse)       },
            { "iDate",        offsetof(Globals, iDate)        },
        };
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_UNIFORMS_H
