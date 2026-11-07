set(AGENT_TOOLS_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ToolResult.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ToolResult.reflect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ToolLine.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ToolRegistry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ToolReply.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToolSchema.hpp"
)

set(AGENT_TOOLS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToolResult.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToolLine.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToolRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToolReply.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ToolSchema.cpp"
)

add_module(agent_tools
    ${AGENT_TOOLS_HEADERS}
    ${AGENT_TOOLS_SOURCES}
)

# foundation only: no rhi, no Vulkan, no Loom
target_include_directories(agent_tools PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")

target_include_directories(agent_tools PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
