cmake_minimum_required(VERSION 3.10)

# no GPU: the conversions of a capture (decode, sRGB encode, downsample) against the double reference in color_math.
# src is for CaptureConversion.hpp, which is private to the module
set(FRAME_CAPTURE_TEST_SOURCES
    test_capture_conversion.cpp
)

add_unit_test_target(frame_capture_tests
    "${FRAME_CAPTURE_TEST_SOURCES}"
    "frame_capture;color_math")

target_include_directories(frame_capture_tests PRIVATE "${CMAKE_SOURCE_DIR}/modules/FrameCapture/src")
