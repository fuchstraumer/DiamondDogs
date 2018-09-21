
SET(VPR_INCLUDE_DIRS_TEST_RELATIVE
    "../../../third_party/vulpesrender/alloc/include"
    "../../../third_party/vulpesrender/command/include"
    "../../../third_party/vulpesrender/common"
    "../../../third_party/vulpesrender/core/include"
    "../../../third_party/vulpesrender/render/include"
    "../../../third_party/vulpesrender/resource/include"
    "../../../third_party/vulpesrender/sync/include"
)

FUNCTION(ADD_INTEGRATION_TEST NAME)
    ADD_EXECUTABLE(${NAME} ${ARGN})
    TARGET_INCLUDE_DIRECTORIES(${NAME} PRIVATE
        "../../../core/include"
        "../../../ext/include"
        "../../../modules/common"
        ${VPR_INCLUDE_DIRS_TEST_RELATIVE}
        "../../../third_party"
        "../../../third_party/glm"
        ${Vulkan_INCLUDE_DIR}
        "../fixtures"
    )
    TARGET_COMPILE_DEFINITIONS(${NAME} PUBLIC
        "NOMINMAX" "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")
    TARGET_LINK_LIBRARIES(${NAME} PRIVATE ${Vulkan_LIBRARY} glfw easyloggingpp)
    IF(MSVC)
        # Multithreaded compile, and intrinsic functions
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/MP> $<$<CONFIG:RELWITHDEBINFO>:/MP>)
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/Oi>)
    ENDIF()
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES FOLDER "Integration Tests")
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    ADD_CUSTOM_COMMAND(TARGET ${NAME} POST_BUILD COMMAND ${CMAKE_COMMAND}
        -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/cfg/" "${CMAKE_CURRENT_BINARY_DIR}/")
ENDFUNCTION()

ADD_SUBDIRECTORY(TriangleTest)
ADD_SUBDIRECTORY(ResourceContextTest)
ADD_SUBDIRECTORY(RendergraphTraversalTest)
