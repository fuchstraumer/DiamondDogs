set(CONTENT_COMPILER_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ArtifactTable.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ContentDiagnostic.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ContentTypes.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Executor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ExecutorTypes.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/FieldKinds.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/GraphJson.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Ingest.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/KindRegistry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ParamsBox.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RecipeParams.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RecipeTable.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/IngestValidation.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/NodeRow.hpp"
)

set(CONTENT_COMPILER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ArtifactTable.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ContentTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Executor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ExecutorTypes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/FieldKinds.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GraphJson.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Ingest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/IngestValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/KindRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ParamsBox.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RecipeTable.cpp"
)

add_module(content_compiler
    ${CONTENT_COMPILER_HEADERS}
    ${CONTENT_COMPILER_SOURCES}
)

# foundation only: no rhi, no Vulkan, no Loom
target_include_directories(content_compiler PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")

target_include_directories(content_compiler PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# the graph file reader parses JSON with nlohmann; no public header includes it
target_include_directories(content_compiler PRIVATE "${PROJECT_SOURCE_DIR}/third_party/json/include")
