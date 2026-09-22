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
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)

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

if(BUILD_SHARED_LIBS)
    set_target_properties(imgui imguizmo PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()