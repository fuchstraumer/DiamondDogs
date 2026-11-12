cmake_minimum_required(VERSION 3.10)

# no GPU: the launch and core stages, module selection and profiles, the results and the boot timings
set(BOOTSTRAP_TEST_SOURCES
    test_modules.hpp
    test_modules.cpp
    test_boot_result.cpp
    test_boot_timings.cpp
    test_core_stage.cpp
    test_launch_stage.cpp
)

add_unit_test_target(bootstrap_tests
    "${BOOTSTRAP_TEST_SOURCES}"
    "bootstrap")

# the test executable links no RHI, as a cooker does
bootstrap_assert_no_rhi(bootstrap_tests)
