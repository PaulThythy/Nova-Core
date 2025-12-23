## NOVA-CORE ##

file(GLOB_RECURSE SOURCES src/**.cpp)
file(GLOB_RECURSE HEADERS include/**.h include/**.hpp)

add_library(Nova-Core STATIC)

target_sources(Nova-Core PRIVATE ${SOURCES} ${HEADERS})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES} ${HEADERS})

# OpenGL
find_package(OpenGL REQUIRED)

# TODO Vulkan
# find_package(Vulkan REQUIRED)
# Vulkan::Vulkan

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
    #${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp

)
add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKENDS_SOURCES})
target_include_directories(imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

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

# linking
target_link_libraries(Nova-Core PUBLIC
    OpenGL::GL
    SDL3::SDL3
    glm::glm
    imgui
    imguizmo
    glad
    EnTT::EnTT
)

target_include_directories(Nova-Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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