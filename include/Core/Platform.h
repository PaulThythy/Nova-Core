#ifndef PLATFORM_H
#define PLATFORM_H

// Platform macros are injected by CMake (see cmake/NovaCompileOptions.cmake):
//   NOVA_WINDOWS | NOVA_LINUX | NOVA_MACOS | NOVA_UNKNOWN_PLATFORM
//
// Build-type macros (same source):
//   NOVA_DEBUG | NOVA_RELEASE | NOVA_RELWITHDEBINFO | NOVA_MINSIZEREL

#if defined(NOVA_WINDOWS)
    #define NOVA_PLATFORM_NAME "Windows"
#elif defined(NOVA_LINUX)
    #define NOVA_PLATFORM_NAME "Linux"
#elif defined(NOVA_MACOS)
    #define NOVA_PLATFORM_NAME "macOS"
#else
    #define NOVA_PLATFORM_NAME "Unknown"
#endif

#if defined(NOVA_DEBUG)
    #define NOVA_BUILD_TYPE_NAME "Debug"
#elif defined(NOVA_RELEASE)
    #define NOVA_BUILD_TYPE_NAME "Release"
#elif defined(NOVA_RELWITHDEBINFO)
    #define NOVA_BUILD_TYPE_NAME "RelWithDebInfo"
#elif defined(NOVA_MINSIZEREL)
    #define NOVA_BUILD_TYPE_NAME "MinSizeRel"
#else
    #define NOVA_BUILD_TYPE_NAME "Unknown"
#endif

#endif // PLATFORM_H