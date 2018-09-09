
ADD_PLUGIN(rendergraph_plugin
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderGraphAPI.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/FeatureRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineSubmission.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineResource.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderResourcePack.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderGraphAPI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/FeatureRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineSubmission.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderResourcePack.cpp"
)

TARGET_LINK_LIBRARIES(rendergraph_plugin PRIVATE shadertools)
TARGET_INCLUDE_DIRECTORIES(rendergraph_plugin PRIVATE
    "../../ext/include/" "${Vulkan_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
