
set(material_module_descriptor_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Descriptor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DescriptorBinder.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DescriptorTemplate.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DescriptorPack.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UpdateTemplateData.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Descriptor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DescriptorBinder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DescriptorTemplate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DescriptorPack.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UpdateTemplateData.cpp"
)

set(material_module_mtl_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/include/EmbeddedTextures.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/MaterialCache.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/MaterialStructures.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Material.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Material.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MaterialCache.cpp"
)


ADD_MODULE(material_module
    ${material_module_descriptor_srcs}
    ${material_module_mtl_srcs}
)

source_group("descriptors" FILES ${material_module_descriptor_srcs})
source_group("material" FILES ${material_module_mtl_srcs})

target_link_libraries(material_module PRIVATE vpr_resource)
target_include_directories(material_module PRIVATE 
    "${Vulkan_INCLUDE_DIR}" ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    "../resource_context/include" "../rendering_context/include"
    "../../third_party/mango/include"
    "../../third_party/shadertools/include")
