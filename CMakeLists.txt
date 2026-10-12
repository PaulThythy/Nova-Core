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

# Vulkan (system dependency required by vendors and Nova-Core)
find_package(Vulkan REQUIRED)
target_include_directories(Nova-Core PUBLIC ${Vulkan_INCLUDE_DIRS})

# Third-party libraries
add_subdirectory(vendors)

# linking (volk charge le loader Vulkan dynamiquement — ne pas lier Vulkan::Vulkan)
target_link_libraries(Nova-Core PUBLIC
    volk
    SDL3::SDL3
    glm::glm
    imgui
    imguizmo
    EnTT::EnTT
    slang
    VulkanMemoryAllocator
)
target_link_libraries(Nova-Core PRIVATE stb)

target_include_directories(Nova-Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(BUILD_SHARED_LIBS)
    set_target_properties(Nova-Core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(Nova-Core
    PUBLIC
        $<$<BOOL:${BUILD_SHARED_LIBS}>:NV_BUILD_SHARED>
    PRIVATE
        $<$<BOOL:${BUILD_SHARED_LIBS}>:NV_CORE_EXPORTS>
)

target_compile_definitions(Nova-Core
    PRIVATE
        NOVA_CORE_ROOT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
    PUBLIC
        $<$<CONFIG:Debug>:NV_ENABLE_ASSERTS>
)