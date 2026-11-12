cmake_minimum_required(VERSION 3.10)

# no GPU: the launch, core and content stages, module selection and profiles, the device requirements, the results
# and the boot timings
set(BOOTSTRAP_TEST_SOURCES
    test_modules.hpp
    test_modules.cpp
    test_boot_result.cpp
    test_boot_timings.cpp
    test_content_stage.cpp
    test_core_stage.cpp
    test_device_requirements.cpp
    test_launch_stage.cpp
)

add_unit_test_target(bootstrap_tests
    "${BOOTSTRAP_TEST_SOURCES}"
    "bootstrap")

# the test executable links no RHI, as a cooker does
bootstrap_assert_no_rhi(bootstrap_tests)

# The content and requirements tests read these bundles with no device. BootNeedsWave has a capability axis, so its
# needs split into Required and Wanted. The HashFill bundle (ComputeManifestTest cooks it) is the second bundle.
lodestone_add_shader_cook(bootstrap_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    MODULES BootNeedsWave.slang BootNeedsPlain.slang)
set_target_properties(bootstrap_test_shaders PROPERTIES FOLDER "Tests/Unit Tests")
add_dependencies(bootstrap_tests bootstrap_test_shaders compute_manifest_test_shaders)
target_compile_definitions(bootstrap_tests PRIVATE
    "BOOT_NEEDS_BUNDLE=\"$<TARGET_PROPERTY:bootstrap_test_shaders,LODESTONE_MANIFEST>\""
    "HASH_FILL_BUNDLE=\"$<TARGET_PROPERTY:compute_manifest_test_shaders,LODESTONE_MANIFEST>\"")
