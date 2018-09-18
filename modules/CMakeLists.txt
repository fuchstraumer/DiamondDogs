
SET(VPR_INCLUDE_DIRS_MODULE_RELATIVE
    "../../third_party/vulpesrender/alloc/include"
    "../../third_party/vulpesrender/command/include"
    "../../third_party/vulpesrender/common"
    "../../third_party/vulpesrender/core/include"
    "../../third_party/vulpesrender/render/include"
    "../../third_party/vulpesrender/resource/include"
    "../../third_party/vulpesrender/sync/include"
)

FUNCTION(ADD_MODULE NAME)
    ADD_LIBRARY(${NAME} STATIC ${ARGN})
    TARGET_INCLUDE_DIRECTORIES(${NAME} PRIVATE "../common/" "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "../../ext/include" "../../third_party/easyloggingpp/src")
    TARGET_LINK_LIBRARIES(${NAME} PUBLIC easyloggingpp)
    IF(MSVC)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/MP> $<$<CONFIG:RELWITHDEBINFO>:/MP>)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/Oi>)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE "/arch:AVX2")
    ENDIF()
    TARGET_COMPILE_DEFINITIONS(${NAME} PRIVATE "NOMINMAX" "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS"
    "_SCL_SECURE_NO_WARNINGS")
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES FOLDER "Modules")
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
ENDFUNCTION()

ADD_SUBDIRECTORY(rendering_context)
ADD_SUBDIRECTORY(resource_context)
#ADD_SUBDIRECTORY(content_compiler)
#ADD_SUBDIRECTORY(rendergraph_module)
