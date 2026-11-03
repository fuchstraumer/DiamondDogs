cmake_minimum_required(VERSION 3.10)

set(OCEAN_CAMERA_TEST_SOURCES
    test_main.cpp
    test_camera.cpp
    test_controller.cpp
)

add_unit_test_target(ocean_camera_tests
    "${OCEAN_CAMERA_TEST_SOURCES}"
    "ocean_camera")
