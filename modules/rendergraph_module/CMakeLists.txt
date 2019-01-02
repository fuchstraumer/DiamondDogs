
SET(rendergraph_core_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Descriptor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DescriptorPool.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DescriptorTemplate.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineResource.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineState.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineSubmission.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderTarget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderResourcePack.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UpdateTemplateData.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Descriptor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DescriptorPool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DescriptorTemplate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineSubmission.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderTarget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderResourcePack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UpdateTemplateData.cpp"
)

SET(rendergraph_material_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/material/Material.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/material/MaterialParameters.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/material/Material.cpp"
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
