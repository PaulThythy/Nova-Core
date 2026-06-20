#ifndef RHI_SWAPCHAIN_DESC_H
#define RHI_SWAPCHAIN_DESC_H

#include <cstdint>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    // Cross-backend present mode preference (mapped per API in each backend).
    enum class RHI_PresentMode {
        Default,     // VSync / FIFO-style behaviour when available
        LowLatency,  // Prefer mailbox / low-latency modes (triple buffering when possible)
        Immediate    // Prefer immediate presentation (may tear)
    };

    /**
     * Swapchain and surface creation parameters shared across graphics backends.
     * Backends map these fields to API-specific swapchain/surface setup.
     */
    struct NV_API RHI_SwapchainDesc {
        // Number of CPU-side frames in flight (1 = single, 2 = double, 3 = triple buffering).
        uint32_t m_FramesInFlight = 3;

        // When true, create a platform surface from the application window (SDL on desktop).
        // When false, skip surface creation (e.g. headless or custom surface supplied later).
        bool m_CreateSurface = true;

        // When true and a valid surface exists, create a swapchain for presentation.
        bool m_EnableSwapchain = true;

        // Optional swapchain extent override. When either dimension is 0, use the window size.
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;

        RHI_PresentMode m_PreferredPresentMode = RHI_PresentMode::LowLatency;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SWAPCHAIN_DESC_H
