cmake_minimum_required(VERSION 3.10)

# no GPU: this tests the sea state descriptor and the sea command that reads it
set(SEA_STATE_TEST_SOURCES
    test_sea_state.cpp
    "${PROJECT_SOURCE_DIR}/applications/OceanFft/SceneCommands.cpp"
)

add_unit_test_target(sea_state_tests
    "${SEA_STATE_TEST_SOURCES}"
    "ocean_simulation")

target_include_directories(sea_state_tests PRIVATE "${PROJECT_SOURCE_DIR}/applications/OceanFft")
