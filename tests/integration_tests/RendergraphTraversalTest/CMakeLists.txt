
ADD_INTEGRATION_TEST(RendergraphTraversalTest
    "main.cpp"
)

TARGET_INCLUDE_DIRECTORIES(RendergraphTraversalTest PRIVATE
    "../../../third_party/shadertools/include"
    "../../../modules/rendering_context/include"
    "../../../modules/resource_context/include"
    "../../../modules/rendergraph_module/include"
    "../../../third_party/easyloggingpp/src"
)

TARGET_LINK_LIBRARIES(RendergraphTraversalTest PRIVATE ShaderTools rendering_context resource_context rendergraph_module glfw ${Vulkan_LIBRARY})
