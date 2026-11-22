add_executable(WebGpuTriangleTest
    "main.cpp"
)

set_target_properties(WebGpuTriangleTest PROPERTIES
    SUFFIX ".html"
    FOLDER "Tests/Integration Tests"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)

target_link_libraries(WebGpuTriangleTest PRIVATE
    bootstrap_webgpu
    rhi_resources
    rhi_context
    emdawnwebgpu_cpp
    foundation
)

target_link_options(WebGpuTriangleTest PRIVATE
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "-sALLOW_MEMORY_GROWTH=1"
)

set_property(TARGET WebGpuTriangleTest APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
)
