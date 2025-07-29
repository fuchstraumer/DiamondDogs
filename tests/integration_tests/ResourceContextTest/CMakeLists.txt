
ADD_INTEGRATION_TEST(ResourceContextTest
    "${CMAKE_CURRENT_SOURCE_DIR}/ComplexScene.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComplexScene.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
)

TARGET_INCLUDE_DIRECTORIES(ResourceContextTest PRIVATE 
    "${Vulkan_INCLUDE_DIR}"
    "../fixtures/"
    "../../../third_party"
    "../../../ext/include"
    "../../../third_party/tinyobjloader/experimental"
    "../../../modules/RenderingContext/include"
    "../../../modules/ResourceContext/include"
    "../../../third_party/vma/src"
    "../../../third_party/gli"
)


set_target_properties(ResourceContextTest PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

TARGET_LINK_LIBRARIES(ResourceContextTest PRIVATE
    vpr_core
    vpr_resource
    vpr_sync
    vpr_command
    ${Vulkan_LIBRARY}
    rendering_context
    resource_context
    gli)

ADD_CUSTOM_COMMAND(TARGET ResourceContextTest POST_BUILD COMMAND ${CMAKE_COMMAND}
    -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../../assets/ResourceContextTestAssets/" "${CMAKE_CURRENT_BINARY_DIR}/")
