
ADD_INTEGRATION_TEST(resource_context_test
    "${CMAKE_CURRENT_SOURCE_DIR}/ComplexScene.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComplexScene.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
)

TARGET_INCLUDE_DIRECTORIES(resource_context_test PRIVATE 
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


set_target_properties(resource_context_test PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

TARGET_LINK_LIBRARIES(resource_context_test PRIVATE
    vpr_core
    vpr_resource
    vpr_sync
    vpr_command
    ${Vulkan_LIBRARY}
    rendering_context
    resource_context
    gli)

ADD_CUSTOM_COMMAND(TARGET resource_context_test POST_BUILD COMMAND ${CMAKE_COMMAND}
    -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../../../assets/ResourceContextTestAssets/" "${CMAKE_CURRENT_BINARY_DIR}/")

# Copy Slang DLLs since resource_context depends on rhi_system which uses Slang
COPY_SLANG_DLLS_TO_INTEGRATION_TEST(resource_context_test)
