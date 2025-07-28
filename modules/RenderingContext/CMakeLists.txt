
OPTION(RENDERING_CONTEXT_USE_DEBUG_INFO "Enable debug info for rendering context" ON)
OPTION(RENDERING_CONTEXT_VALIDATION_ENABLED "Enable validation for rendering context" ON)
OPTION(RENDERING_CONTEXT_DEBUG_INFO_THREAD_ID "Enable thread ID in debug info for rendering context" ON)
OPTION(RENDERING_CONTEXT_DEBUG_INFO_TIMESTAMPS "Enable timestamps in debug info for rendering context" ON)
OPTION(RENDERING_CONTEXT_DEBUG_INFO_CALLING_FN "Enable calling function name in debug info for rendering context" OFF)

ADD_MODULE(rendering_context
    "include/ExtensionWrangler.hpp"
    "include/PipelineExecutableInfo.hpp"
    "include/PlatformWindow.hpp"
    "include/RenderingContext.hpp"
    "src/ExtensionWrangler.cpp"
    "src/PipelineExecutableInfo.cpp"
    "src/PlatformWindow.cpp"
    "src/RenderingContext.cpp"
    "src/RenderingContextImpl.hpp"
    "src/RenderingContextImpl.cpp"
)

TARGET_LINK_LIBRARIES(rendering_context PUBLIC vpr_core glfw ${Vulkan_LIBRARY})
TARGET_INCLUDE_DIRECTORIES(rendering_context PUBLIC ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    "../../third_party/json/include" ${Vulkan_INCLUDE_DIR})

IF(RENDERING_CONTEXT_USE_DEBUG_INFO)
    TARGET_COMPILE_DEFINITIONS(rendering_context PUBLIC RENDERING_CONTEXT_USE_DEBUG_INFO_CONF)
ENDIF()

IF(RENDERING_CONTEXT_VALIDATION_ENABLED)
    TARGET_COMPILE_DEFINITIONS(rendering_context PUBLIC RENDERING_CONTEXT_VALIDATION_ENABLED_CONF)
ENDIF()

IF(RENDERING_CONTEXT_DEBUG_INFO_THREAD_ID)
    TARGET_COMPILE_DEFINITIONS(rendering_context PUBLIC RENDERING_CONTEXT_DEBUG_INFO_THREAD_ID_CONF)
ENDIF()

IF(RENDERING_CONTEXT_DEBUG_INFO_TIMESTAMPS)
    TARGET_COMPILE_DEFINITIONS(rendering_context PUBLIC RENDERING_CONTEXT_DEBUG_INFO_TIMESTAMPS_CONF)
ENDIF()

IF(RENDERING_CONTEXT_DEBUG_INFO_CALLING_FN)
    TARGET_COMPILE_DEFINITIONS(rendering_context PUBLIC RENDERING_CONTEXT_DEBUG_INFO_CALLING_FN_CONF)
ENDIF()

find_package(Python COMPONENTS Interpreter)

# Define paths for dependency tracking
set(RENDERING_CONTEXT_VK_XML_PATH "$ENV{VULKAN_SDK}/share/vulkan/registry/vk.xml")
set(RENDERING_CONTEXT_GENERATOR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/GenerateExtensionAndFeatureHeaders.py")
set(RENDERING_CONTEXT_OUTPUT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/GeneratedExtensionHeader.hpp")

# Custom command that only runs when dependencies change
add_custom_command(
    OUTPUT "${RENDERING_CONTEXT_OUTPUT_HEADER}"
    COMMAND ${Python_EXECUTABLE} "${RENDERING_CONTEXT_GENERATOR_SCRIPT}"
                                 "-specDir=$ENV{VULKAN_SDK}" "-outputDir=${CMAKE_CURRENT_SOURCE_DIR}/src"
    DEPENDS "${RENDERING_CONTEXT_VK_XML_PATH}" "${RENDERING_CONTEXT_GENERATOR_SCRIPT}"
    COMMENT "Generating extension lookup header (dependencies changed)"
    VERBATIM
)

# Custom target that depends on the output file
add_custom_target(generate_extension_headers
    DEPENDS "${OUTPUT_HEADER}"
)

add_dependencies(rendering_context generate_extension_headers)
