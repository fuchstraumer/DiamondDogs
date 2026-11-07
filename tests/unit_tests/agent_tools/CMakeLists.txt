cmake_minimum_required(VERSION 3.10)

# no GPU: the registry, its schemas and its text form, against described test types
set(AGENT_TOOLS_TEST_SOURCES
    test_tool_registry.cpp
)

add_unit_test_target(agent_tools_tests
    "${AGENT_TOOLS_TEST_SOURCES}"
    "agent_tools")

# nlohmann parses what the writers produce, so that a test proves the output is valid JSON
target_include_directories(agent_tools_tests PRIVATE "${CMAKE_SOURCE_DIR}/third_party/json/include")
