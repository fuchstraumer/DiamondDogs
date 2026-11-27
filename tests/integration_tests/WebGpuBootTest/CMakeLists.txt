# The WebGPU device stage in the browser: Required and Wanted needs, Declined, the neutral capability, and the
# adapter's limits
add_executable(WebGpuBootTest
    "main.cpp"
)

set_target_properties(WebGpuBootTest PROPERTIES
    SUFFIX ".html"
    FOLDER "Tests/Integration Tests"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)

target_link_libraries(WebGpuBootTest PRIVATE
    bootstrap_webgpu
    rhi_context
    emdawnwebgpu_cpp
    foundation
)

target_link_options(WebGpuBootTest PRIVATE
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
    "-sEXIT_RUNTIME=0"
    "-sDYNAMIC_EXECUTION=0"
    "-sINITIAL_MEMORY=32MB"
    "$<IF:$<BOOL:${DIAMOND_DOGS_WEB_THREADS}>,-sENVIRONMENT=web$<COMMA>worker,-sENVIRONMENT=web>"
)

set_property(TARGET WebGpuBootTest APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
)
