# Copies rendered images out to 8-bit sRGB PNG files, for screenshots and for agents watching a scene change.
ADD_MODULE(frame_capture
    "include/FrameCapture.hpp"
    "src/FrameCapture.cpp")

target_include_directories(frame_capture PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
target_include_directories(frame_capture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb")
target_link_libraries(frame_capture PUBLIC rhi_resources rhi_system)
