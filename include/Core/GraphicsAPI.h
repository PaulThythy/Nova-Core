#ifndef GRAPHICSAPI_H
#define GRAPHICSAPI_H

namespace Nova::Core {
    enum class GraphicsAPI {
        None = 0, 
        OpenGL, 
        Vulkan,
        SDLRenderer
    };
} // namespace Nova::Core

#endif // GRAPHICSAPI_H