cmake_minimum_required(VERSION 3.10)

# the window, device and GPU stages on a real device with full validation. One test opens a small window
add_unit_test_target(bootstrap_vulkan_tests
    "test_bootstrap_vulkan.cpp"
    "bootstrap_vulkan")

target_include_directories(bootstrap_vulkan_tests PRIVATE "${PROJECT_SOURCE_DIR}/modules/PlatformSystem/include")
