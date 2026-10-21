# Runs the OceanFft compute chain headless through ocean_simulation (applications/OceanFft), and compares the IFFT
# kernels, the conjugate pass, and the tick-IFFT-resolve chain with double precision on the CPU.
ADD_INTEGRATION_TEST(OceanFftTest "main.cpp")
target_link_libraries(OceanFftTest PRIVATE ocean_simulation rhi_resources)
add_test(NAME OceanFftTest COMMAND OceanFftTest)
