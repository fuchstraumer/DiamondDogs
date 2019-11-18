
set(VPR_INCLUDE_DIRS_MODULE_RELATIVE
    "../../third_party/vulpesrender/alloc/include"
    "../../third_party/vulpesrender/command/include"
    "../../third_party/vulpesrender/common"
    "../../third_party/vulpesrender/core/include"
    "../../third_party/vulpesrender/render/include"
    "../../third_party/vulpesrender/resource/include"
    "../../third_party/vulpesrender/sync/include"
)

FUNCTION(ADD_MODULE NAME)
    add_library(${NAME} STATIC ${ARGN})
    target_include_directories(${NAME} PRIVATE "../common/" "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "../../third_party/easyloggingpp/src" "../../core/include")
    target_link_libraries(${NAME} PUBLIC easyloggingpp diamond_dogs_core)
    IF(MSVC)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/MP> $<$<CONFIG:RELWITHDEBINFO>:/MP>)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/Oi> $<$<CONFIG:RELWITHDEBINFO>:/Oi>)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE "/arch:AVX2" "/std:c++latest" "/await")
    else()
        SET_TARGET_PROPERTIES(${NAME} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
    ENDIF()
    TARGET_COMPILE_DEFINITIONS(${NAME} PRIVATE "NOMINMAX" "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
    "_SCL_SECURE_NO_WARNINGS")
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES FOLDER "Modules")
ENDFUNCTION()

#add_subdirectory(ecs_module)
#add_subdirectory(rendergraph_module)
add_subdirectory(rendering_context)
add_subdirectory(resource_context)
# This module provides imgui as a basic debugging UI
# that sits just BARELY over the resource and rendering contexts
add_subdirectory(imgui_module)
#add_subdirectory(material_module)
add_subdirectory(content_compiler)
#add_subdirectory(terrain_module)
#add_subdirectory(vtf_module)
