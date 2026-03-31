## NOVA-CORE ##

file(GLOB_RECURSE SOURCES src/**.cpp)
file(GLOB_RECURSE HEADERS include/**.h include/**.hpp)

if(BUILD_SHARED_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

add_library(Nova-Core)

target_sources(Nova-Core PRIVATE ${SOURCES} ${HEADERS})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES} ${HEADERS})

# OpenGL
find_package(OpenGL REQUIRED)

# Vulkan
find_package(Vulkan REQUIRED)
target_include_directories(Nova-Core PUBLIC ${Vulkan_INCLUDE_DIRS})

# SDL3
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)

set(SDL_X11 ON CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
set(SDL_WAYLAND ON CACHE BOOL "" FORCE)

set(SDL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendors/SDL)
add_subdirectory(${SDL_DIR} EXCLUDE_FROM_ALL)
include_directories(${SDL_DIR}/include)

# glm
set(GLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendors/glm)
add_subdirectory(${GLM_DIR})
include_directories(${GLM_DIR})

# Dear Imgui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendors/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
set(IMGUI_BACKENDS_SOURCES
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp

)
add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKENDS_SOURCES})
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan)

# ImGuizmo
set(IMGUIZMO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendors/ImGuizmo)
set(IMGUIZMO_SOURCES ${IMGUIZMO_DIR}/ImGuizmo.cpp)
set(IMGUIZMO_HEADERS ${IMGUIZMO_DIR}/ImGuizmo.h)

add_library(imguizmo STATIC ${IMGUIZMO_SOURCES} ${IMGUIZMO_HEADERS})
target_include_directories(imguizmo PUBLIC
  ${IMGUIZMO_DIR}
)
target_link_libraries(imguizmo PUBLIC imgui)

# glad 
add_library(glad STATIC vendors/glad/src/gl.c)
target_include_directories(glad PUBLIC vendors/glad/include)

# entt
set(ENTT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendors/entt)
add_subdirectory(${ENTT_DIR} EXCLUDE_FROM_ALL)

# Slang (éviter doublons avec d’autres deps ; statique pour lien simple)
set(SLANG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendors/slang)
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
add_subdirectory(${SLANG_DIR} EXCLUDE_FROM_ALL)

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

if(BUILD_SHARED_LIBS AND TARGET slang)
    set_property(TARGET slang PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# linking
target_link_libraries(Nova-Core PUBLIC
    OpenGL::GL
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
    imgui
    imguizmo
    glad
    EnTT::EnTT
    slang
)

target_include_directories(Nova-Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(BUILD_SHARED_LIBS)
    set_target_properties(Nova-Core imgui imguizmo glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(Nova-Core
    PUBLIC
        $<$<BOOL:${BUILD_SHARED_LIBS}>:NV_BUILD_SHARED>
    PRIVATE
        $<$<BOOL:${BUILD_SHARED_LIBS}>:NV_CORE_EXPORTS>
)

target_compile_definitions(Nova-Core PRIVATE
    NOVA_CORE_ROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

target_compile_definitions(Nova-Core
    PRIVATE
        NOVA_CORE_ROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        $<$<CONFIG:Debug>:NOVA_DEBUG>
        $<$<CONFIG:Release>:NOVA_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:NOVA_RELWITHDEBINFO>
        $<$<CONFIG:MinSizeRel>:NOVA_MINSIZEREL>
)

target_compile_definitions(Nova-Core
    PUBLIC
        $<$<CONFIG:Debug>:NV_ENABLE_ASSERTS>
)