
set(VPR_INCLUDE_DIRS_MODULE_RELATIVE
    "../../third_party/vulpesrender/alloc/include"
    "../../third_party/vulpesrender/command/include"
    "../../third_party/vulpesrender/common"
    "../../third_party/vulpesrender/core/include"
    "../../third_party/vulpesrender/render/include"
    "../../third_party/vulpesrender/resource/include"
    "../../third_party/vulpesrender/sync/include"
)

function(add_module NAME)
    add_library(${NAME} STATIC ${ARGN})
    target_include_directories(${NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "../../foundation/include")
    target_link_libraries(${NAME} PUBLIC foundation)
    if(MSVC)
        target_compile_options(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/MP> $<$<CONFIG:RELWITHDEBINFO>:/MP>)
        target_compile_options(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/Oi> $<$<CONFIG:RELWITHDEBINFO>:/Oi>)
        target_compile_definitions(${NAME} PRIVATE 
            "NOMINMAX")
        target_compile_options(${NAME} PRIVATE "/arch:AVX2")
    endif()
    set_target_properties(${NAME} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    set_target_properties(${NAME} PROPERTIES FOLDER "Modules")
endfunction()


#add_subdirectory(ecs_module)
#add_subdirectory(rendergraph_module)
#add_subdirectory(TaskGraph)  # Shelved - coroutine-based task system for later
add_subdirectory(RhiSystem)
add_subdirectory(PlatformSystem)
add_subdirectory(RhiResources)
# This module provides imgui as a basic debugging UI
# that sits just BARELY over the resource and rendering contexts
#add_subdirectory(ImGuiModule)
#add_subdirectory(material_module)
#add_subdirectory(content_compiler)
#add_subdirectory(terrain_module)
#add_subdirectory(vtf_module)
