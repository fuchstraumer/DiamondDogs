set(BOOTSTRAP_WEBGPU_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/WebGpuDeviceStage.hpp"
)

set(BOOTSTRAP_WEBGPU_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/WebGpuDeviceStage.cpp"
)

add_module(bootstrap_webgpu
    ${BOOTSTRAP_WEBGPU_HEADERS}
    ${BOOTSTRAP_WEBGPU_SOURCES}
)

target_include_directories(bootstrap_webgpu PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(bootstrap_webgpu PUBLIC
    foundation
    rhi_context
    emdawnwebgpu_cpp
    bootstrap
)
