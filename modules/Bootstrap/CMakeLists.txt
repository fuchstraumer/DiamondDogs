set(BOOTSTRAP_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/BootResult.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/BootTimings.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/CoreStage.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LaunchStage.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ModuleDescriptor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ModuleSelection.hpp"
)

set(BOOTSTRAP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BootResult.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BootTimings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CoreStage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LaunchStage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ModuleDescriptor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ModuleSelection.cpp"
)

add_module(bootstrap
    ${BOOTSTRAP_HEADERS}
    ${BOOTSTRAP_SOURCES}
)

# no RHI: a cooker links this target alone. bootstrap_vulkan adds the device stages
target_include_directories(bootstrap PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
target_link_libraries(bootstrap PUBLIC shader_bundle)

# nlohmann stays private to LaunchStage.cpp, so that it never reaches a public header
target_include_directories(bootstrap PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party/json/include")

# boot timings compile out of Release and MinSizeRel, the same split as RHI_GPU_TIMERS_ENABLED
target_compile_definitions(bootstrap
    PUBLIC "BOOTSTRAP_TIMINGS_ENABLED=$<IF:$<CONFIG:Release,MinSizeRel>,0,1>"
    PRIVATE "BOOTSTRAP_BUILD_CONFIG=\"$<CONFIG>\"")

# Fails the configure when `target` links rhi_system or the Vulkan loader, directly or through any target it links.
# Executables that must stay free of the RHI (the cooker) call this, so that a stray link fails at once.
function(bootstrap_assert_no_rhi target)
    set(pending ${target})
    set(visited "")

    while(pending)
        list(POP_FRONT pending current)

        if(current IN_LIST visited)
            continue()
        endif()

        list(APPEND visited ${current})

        # a regex over the whole item, so that a link inside a generator expression is caught too
        if(current MATCHES "(^|[^a-z_])rhi_system($|[^a-z_])" OR current MATCHES "Vulkan::" OR current MATCHES "vulkan-1")
            message(FATAL_ERROR "${target} must not link the RHI, and it reaches ${current}. Visited: ${visited}")
        endif()

        if(NOT TARGET ${current})
            continue()
        endif()

        get_target_property(aliased ${current} ALIASED_TARGET)

        if(aliased)
            list(APPEND pending ${aliased})
            continue()
        endif()

        get_target_property(type ${current} TYPE)
        set(links "")

        if(NOT type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(direct ${current} LINK_LIBRARIES)

            if(direct)
                list(APPEND links ${direct})
            endif()
        endif()

        get_target_property(interface ${current} INTERFACE_LINK_LIBRARIES)

        if(interface)
            list(APPEND links ${interface})
        endif()

        foreach(link IN LISTS links)
            # $<LINK_ONLY:x> is how CMake records a PRIVATE link of a static library
            string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" link "${link}")
            list(APPEND pending ${link})
        endforeach()
    endwhile()
endfunction()
