
set(LOOM_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Declarations.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/GraphBuilder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomDiagnostic.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomError.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/LoomKeys.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ResourceFuture.hpp"
)

set(LOOM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Declarations.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GraphBuilder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LoomDiagnostic.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/LoomError.cpp"
)

add_module(loom
    ${LOOM_HEADERS}
    ${LOOM_SOURCES}
)

target_include_directories(loom PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../RhiResources/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../RhiSystem/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")

# link only the backend modules, do *not* link a graphics API or 
# use any of it's headers; that'll make emscripten transition way worse.
target_link_libraries(loom PUBLIC rhi_system rhi_resources)
