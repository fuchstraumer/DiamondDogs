if(EMSCRIPTEN)
    set(RHI_CONTEXT_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/RhiContext.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/RhiContextWebGpu.hpp")
    set(RHI_CONTEXT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/webgpu/RhiContextWebGpu.cpp")
else()
    set(RHI_CONTEXT_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/RhiContext.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/RhiContextVulkan.hpp")
    set(RHI_CONTEXT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan/RhiContextVulkan.cpp")
endif()

add_module(rhi_context ${RHI_CONTEXT_HEADERS} ${RHI_CONTEXT_SOURCES})

target_include_directories(rhi_context PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(rhi_context PUBLIC foundation lodestone::client_library lodestone::client_interface)

if(EMSCRIPTEN)
    target_link_libraries(rhi_context PUBLIC emdawnwebgpu_cpp)
else()
    target_link_libraries(rhi_context PUBLIC rhi_system)
endif()
