## NOVA-CORE VENDORS ##
# Builds third-party libraries only. Consumers link the exported targets.

# SDL3
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)

set(SDL_X11 ON CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND ON CACHE BOOL "" FORCE)

add_subdirectory(SDL EXCLUDE_FROM_ALL)

# glm
add_subdirectory(glm)

# volk (avant imgui : le backend Vulkan ImGui utilise volk)
# Note: avoid VK_USE_PLATFORM_XLIB_KHR — Xlib.h #define None 0 conflicts with our enums.
if(WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR CACHE STRING "" FORCE)
elseif(APPLE)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_METAL_EXT CACHE STRING "" FORCE)
elseif(UNIX)
    set(VOLK_STATIC_DEFINES "VK_USE_PLATFORM_XCB_KHR;VK_USE_PLATFORM_WAYLAND_KHR" CACHE STRING "" FORCE)
endif()
add_subdirectory(volk EXCLUDE_FROM_ALL)

# Dear ImGui
set(IMGUI_SOURCES
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
set(IMGUI_BACKENDS_SOURCES
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/backends/imgui_impl_sdlrenderer3.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKENDS_SOURCES})
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC volk SDL3::SDL3)

# ImGuizmo
add_library(imguizmo STATIC ImGuizmo/ImGuizmo.cpp ImGuizmo/ImGuizmo.h)
target_include_directories(imguizmo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo
)
target_link_libraries(imguizmo PUBLIC imgui)

# EnTT
add_subdirectory(entt EXCLUDE_FROM_ALL)

# Slang (statique pour un lien simple ; options réduites pour éviter les outils annexes)
set(SLANG_LIB_TYPE STATIC CACHE STRING "How to build the slang-compiler lib" FORCE)
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_GFX OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_SLANGRT OFF CACHE BOOL "" FORCE)
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "" FORCE)
if(MINGW)
    # DXC/COM sous MinGW : symboles __mingw_uuidof(IDxc*) non fournis par la chaîne de lien ;
    # Nova compile les shaders en SPIR-V, pas besoin de DXIL dans cette build Slang.
    set(SLANG_ENABLE_DXIL OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(slang EXCLUDE_FROM_ALL)

# MinGW : slang-dispatcher et slang-bootstrap (slangc/main) utilisent wmain ; sans -municode
# le CRT attend WinMain → « undefined reference to `WinMain' ».
if(MINGW)
    if(TARGET slang-dispatcher)
        target_link_options(slang-dispatcher PRIVATE "-municode")
    endif()
    if(TARGET slang-bootstrap)
        target_link_options(slang-bootstrap PRIVATE "-municode")
    endif()
endif()

# MSVC Debug: slang/source/slangc/main.cpp calls _CrtDumpMemoryLeaks() then
# assert(!memleakDetected) at the end of execution. The current version of Slang has
# minor internal leaks that trigger this assert → abort() → the custom build of
# generate_core_module_headers fails with "Code 3" even though the .h files were
# successfully generated. We neutralize only the assert on slang-bootstrap
# (NDEBUG only disables assert(); _CrtDumpMemoryLeaks still logs).
if(MSVC AND TARGET slang-bootstrap)
    target_compile_definitions(slang-bootstrap PRIVATE NDEBUG)
endif()

if(BUILD_SHARED_LIBS AND TARGET slang)
    set_property(TARGET slang PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# VulkanMemoryAllocator
add_subdirectory(VulkanMemoryAllocator EXCLUDE_FROM_ALL)

# stb (header-only)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# Assimp (static; reduced options for use as a submodule)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(_nova_assimp_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(assimp EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_nova_assimp_build_shared_libs})
unset(_nova_assimp_build_shared_libs)

if(BUILD_SHARED_LIBS)
    set_target_properties(imgui imguizmo PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(TARGET assimp)
        set_property(TARGET assimp PROPERTY POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

# Tracy (client only — the profiler GUI is a separate Tracy build)
# TRACY_ENABLE is left OFF in Tracy's own options; Nova injects it per-config below
# so Dist builds stay free of profiling overhead under multi-config generators.
option(NOVA_ENABLE_TRACY "Enable Tracy frame profiler (all configs except Dist)" ON)
set(TRACY_STATIC ON CACHE BOOL "" FORCE)
set(TRACY_ENABLE OFF CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_CLIENT_PYTHON OFF CACHE BOOL "" FORCE)
# MinGW/UCRT: Tracy's DXT1 CompressWorker thread aborts with
# "*** stack smashing detected ***" (winpthread + GCC stack protector). Disable
# frame-image support there; zones/plots remain available.
if(MINGW)
    set(TRACY_NO_FRAME_IMAGE ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(tracy EXCLUDE_FROM_ALL)

if(NOVA_ENABLE_TRACY)
    # Per-config enablement: Tracy's own TRACY_ENABLE option is configure-time only.
    # Generator expression keeps Dist free of profiling while Debug/Release stay instrumented.
    target_compile_definitions(TracyClient PUBLIC
        $<$<NOT:$<CONFIG:Dist>>:TRACY_ENABLE>
    )
endif()

if(MINGW AND TARGET TracyClient)
    # winpthread + -fstack-protector has produced canary failures in Tracy workers.
    target_compile_options(TracyClient PRIVATE -fno-stack-protector)
endif()

if(BUILD_SHARED_LIBS AND TARGET TracyClient)
    set_property(TARGET TracyClient PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()