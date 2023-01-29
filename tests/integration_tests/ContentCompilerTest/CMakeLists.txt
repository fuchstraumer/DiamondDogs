
ADD_INTEGRATION_TEST(ContentCompilerTest
    "${CMAKE_CURRENT_SOURCE_DIR}/ContentCompilerScene.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ContentCompilerScene.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp")

TARGET_INCLUDE_DIRECTORIES(ContentCompilerTest PRIVATE
    "${Vulkan_INCLUDE_DIR}"
    "../fixtures/"
    "../../../third_party"
    "../../../third_party/mango/include"
    "../../../third_party/shadertools/include"
    "../../../modules/rendering_context/include"
    "../../../modules/resource_context/include"
    "../../../modules/content_compiler/include"
    "../../../third_party/vma/src")

TARGET_LINK_LIBRARIES(ContentCompilerTest PRIVATE 
        vpr_core vpr_resource mango vpr_sync vpr_command ${Vulkan_LIBRARY} 
        rendering_context resource_context content_compiler
        ShaderTools)
