
SET(rendergraph_core_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineResource.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineState.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineSubmission.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderTarget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineSubmission.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderTarget.cpp"
)
ADD_MODULE(rendergraph_module
    ${rendergraph_core_srcs}
    ${rendergraph_material_srcs}
)

SOURCE_GROUP("graph" FILES ${rendergraph_core_srcs})
SOURCE_GROUP("material" FILES ${rendergraph_material_srcs})

TARGET_LINK_LIBRARIES(rendergraph_module PUBLIC ShaderTools vpr_resource)
TARGET_INCLUDE_DIRECTORIES(rendergraph_module PRIVATE
    "../../ext/include/" "${Vulkan_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "../rendering_context/include" "../resource_context/include" ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    "../../third_party/glm/" "../../third_party/shadertools/include"
)
