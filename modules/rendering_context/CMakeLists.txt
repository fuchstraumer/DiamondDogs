

ADD_MODULE(rendering_context
    "include/PipelineExecutableInfo.hpp"
    "include/PlatformWindow.hpp"
    "include/RenderingContext.hpp"
    "src/PipelineExecutableInfo.cpp"
    "src/PlatformWindow.cpp"
    "src/RenderingContext.cpp"
    "src/GeneratedExtensionHeader.hpp"
)

TARGET_LINK_LIBRARIES(rendering_context PUBLIC vpr_core glfw ${Vulkan_LIBRARY})
TARGET_INCLUDE_DIRECTORIES(rendering_context PUBLIC ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    "../../third_party/json/include" ${Vulkan_INCLUDE_DIR})

find_package(Python COMPONENTS Interpreter)

add_custom_target(generate_extension_headers ALL
    COMMAND ${Python_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/GenerateExtensionAndFeatureHeaders.py"
                                 "-specDir=$ENV{VULKAN_SDK}" "-outputDir=${CMAKE_CURRENT_SOURCE_DIR}/src"
    BYPRODUCTS "${CMAKE_CURRENT_SOURCE_DIR}/src/GeneratedExtensionHeader.hpp"
    COMMENT "Generating extension lookup header")

add_dependencies(rendering_context generate_extension_headers)
