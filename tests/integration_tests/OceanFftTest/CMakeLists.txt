# Runs the OceanFft compute chain headless through ocean_simulation (applications/OceanFft), and compares the IFFT
# kernels, the conjugate pass, and the tick-IFFT-resolve chain with double precision on the CPU.
ADD_INTEGRATION_TEST(OceanFftTest
    "main.cpp"
    "${PROJECT_SOURCE_DIR}/applications/OceanFft/mesh/OceanCdlod.cpp"
    "${PROJECT_SOURCE_DIR}/applications/OceanFft/environment/ExrLoader.cpp")
target_include_directories(OceanFftTest PRIVATE
    "${PROJECT_SOURCE_DIR}/applications/OceanFft/mesh"
    "${PROJECT_SOURCE_DIR}/applications/OceanFft/environment"
    "${PROJECT_SOURCE_DIR}/modules/ColorPipe/include"
    "${PROJECT_SOURCE_DIR}/third_party/basis_universal/encoder"
    "${PROJECT_SOURCE_DIR}/third_party/basis_universal/encoder/3rdparty")
target_link_libraries(OceanFftTest PRIVATE ocean_simulation rhi_resources color_pipe)
add_test(NAME OceanFftTest COMMAND OceanFftTest)
