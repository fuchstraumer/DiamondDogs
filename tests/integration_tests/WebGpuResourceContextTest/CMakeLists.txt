add_executable(WebGpuResourceContextTest
    "main.cpp"
)

set_target_properties(WebGpuResourceContextTest PROPERTIES
    SUFFIX ".html"
    FOLDER "Tests/Integration Tests"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)

target_link_libraries(WebGpuResourceContextTest PRIVATE
    bootstrap_webgpu
    rhi_resources
    rhi_context
    emdawnwebgpu_cpp
    foundation
)

target_link_options(WebGpuResourceContextTest PRIVATE
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFILESYSTEM=0"
    "-sEXIT_RUNTIME=0"
    "-sDYNAMIC_EXECUTION=0"
    "-sINITIAL_MEMORY=32MB"
    "$<IF:$<BOOL:${DIAMOND_DOGS_WEB_THREADS}>,-sENVIRONMENT=web$<COMMA>worker,-sENVIRONMENT=web>"
)

set_property(TARGET WebGpuResourceContextTest APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
)
