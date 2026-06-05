
FUNCTION(ADD_INTEGRATION_TEST NAME)
    ADD_EXECUTABLE(${NAME} ${ARGN})
    TARGET_INCLUDE_DIRECTORIES(${NAME} PRIVATE
        "../../../foundation/include"
        "../../../ext/include"
        "../../../Modules/PlatformSystem/include"
        "../../../Modules/RhiSystem/include"
        "../../../third_party"
        ${Vulkan_INCLUDE_DIR}
        "../fixtures"
        "${CMAKE_BINARY_DIR}/include"  # Include generated headers like SourcePaths.hpp
    )
    TARGET_COMPILE_DEFINITIONS(${NAME} PUBLIC
        "NOMINMAX" "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")
    TARGET_LINK_LIBRARIES(${NAME} PRIVATE ${Vulkan_LIBRARY} glfw rhi_system platform_system foundation)
    IF(MSVC)
        # Multithreaded compile, and intrinsic functions
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE "/MP")
        TARGET_COMPILE_OPTIONS(${NAME} PRIVATE $<$<CONFIG:RELEASE>:/Oi>)
    ENDIF()
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES FOLDER "Tests/Integration Tests")
    SET_TARGET_PROPERTIES(${NAME} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    ADD_CUSTOM_COMMAND(TARGET ${NAME} POST_BUILD COMMAND ${CMAKE_COMMAND}
        -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/cfg/" "${CMAKE_CURRENT_BINARY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${NAME}> $<TARGET_FILE_DIR:${NAME}>
        COMMAND_EXPAND_LISTS)

ENDFUNCTION()

ADD_SUBDIRECTORY(TriangleTest)
#ADD_SUBDIRECTORY(ResourceContextTest)  # Requires rewrite for new RhiResources module
#ADD_SUBDIRECTORY(ContentCompilerTest)
#ADD_SUBDIRECTORY(VolumetricForward)
#ADD_SUBDIRECTORY(DescriptorTest)
