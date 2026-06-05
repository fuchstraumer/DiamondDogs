cmake_minimum_required(VERSION 3.10)

# Include Google Test
include(GoogleTest)

# Common test settings
set(UNIT_TEST_COMPILE_OPTIONS)
set(UNIT_TEST_LINK_LIBRARIES gtest_main gmock_main)

if(MSVC)
    list(APPEND UNIT_TEST_COMPILE_OPTIONS 
        "/arch:AVX2"
        $<$<CONFIG:RELEASE>:/MP> 
        $<$<CONFIG:RELWITHDEBINFO>:/MP>
        $<$<CONFIG:RELEASE>:/Oi> 
        $<$<CONFIG:RELWITHDEBINFO>:/Oi>)
    list(APPEND UNIT_TEST_LINK_LIBRARIES)
else()
    list(APPEND UNIT_TEST_COMPILE_OPTIONS "-std=c++23")
    list(APPEND UNIT_TEST_LINK_LIBRARIES "c++fs" "dl" "pthread")
endif()

# Helper function to create unit test targets
function(add_unit_test_target TARGET_NAME TEST_SOURCES LINK_LIBRARIES)
    add_executable(${TARGET_NAME} ${TEST_SOURCES})
    
    target_compile_options(${TARGET_NAME} PRIVATE ${UNIT_TEST_COMPILE_OPTIONS})
    
    # Include the generated header directory for SourcePaths.hpp
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_BINARY_DIR}/include")
    
    target_link_libraries(${TARGET_NAME} 
        ${UNIT_TEST_LINK_LIBRARIES}
        ${LINK_LIBRARIES})
        
    if(MSVC)
        target_compile_definitions(${TARGET_NAME} PRIVATE "NOMINMAX" "_XM_SVML_INTRINSICS_" "XM_AVX2_INTRINSICS_")
        set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    else()
        set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    endif()

    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Tests/UnitTests")
    
    # Discover and register tests with CTest
    gtest_discover_tests(${TARGET_NAME})

    # Check if this test links against rhi_system (which depends on Slang)
    # If so, automatically copy Slang DLLs to the test output directory
    if("${LINK_LIBRARIES}" MATCHES "rhi_system")
        copy_slang_dlls_to_target(${TARGET_NAME})
    endif()
endfunction()

# Add subdirectories for each module's tests
add_subdirectory(foundation)
add_subdirectory(rhi_system)
add_subdirectory(platform_system) 
add_subdirectory(resource_context)

# Add other module test directories as they become available
# add_subdirectory(content_compiler)
# add_subdirectory(material_module)
# add_subdirectory(render_graph)
# add_subdirectory(imgui_module)
# add_subdirectory(terrain_module)
# add_subdirectory(vtf_module)
