
ADD_INTEGRATION_TEST(TriangleTest
    "main.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
    "TriangleTest.cpp"
    "TriangleTest.hpp"
)

# Copy Slang DLLs since rhi_system depends on Slang
copy_slang_dlls_to_target(TriangleTest)
