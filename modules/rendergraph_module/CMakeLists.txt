
SET(rendergraph_core_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/FeatureRenderer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineSubmission.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PipelineResource.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderGraph.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ShaderResourcePack.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/FeatureRenderer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineSubmission.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/PipelineResource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderGraph.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderResourcePack.cpp"
)

SET(rendergraph_object_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/objects/DepthTarget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/objects/Material.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/objects/MaterialParameters.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/objects/RenderTarget.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/objects/DepthTarget.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/objects/Material.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/objects/RenderTarget.cpp"
) 

SET(rendergraph_system_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/systems/BufferResourceCache.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/systems/ImageResourceCache.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/systems/BufferResourceCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/systems/ImageResourceCache.cpp"
)

ADD_MODULE(rendergraph_module
    ${rendergraph_core_srcs}
    ${rendergraph_object_srcs}
    ${rendergraph_system_srcs}
)

SOURCE_GROUP("graph" FILES ${rendergraph_core_srcs})
SOURCE_GROUP("object" FILES ${rendergraph_object_srcs})
SOURCE_GROUP("system" FILES ${rendergraph_system_srcs})

TARGET_LINK_LIBRARIES(rendergraph_module PRIVATE shadertools)
TARGET_INCLUDE_DIRECTORIES(rendergraph_module PRIVATE
    "../../ext/include/" "${Vulkan_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "../rendering_context/include" "../resource_context/include" ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
)
