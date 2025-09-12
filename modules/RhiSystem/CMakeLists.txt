
OPTION(RHI_SYSTEM_USE_DEBUG_INFO "Enable debug info for RHI system" ON)
OPTION(RHI_SYSTEM_VALIDATION_ENABLED "Enable validation for RHI system" ON)
OPTION(RHI_SYSTEM_DEBUG_INFO_THREAD_ID "Enable thread ID in debug info for RHI system" ON)
OPTION(RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS "Enable timestamps in debug info for RHI system" ON)
OPTION(RHI_SYSTEM_DEBUG_INFO_CALLING_FN "Enable calling function name in debug info for RHI system" OFF)

ADD_MODULE(rhi_system
    "include/ExtensionWrangler.hpp"
    "include/PipelineExecutableInfo.hpp"
    "include/RhiSystem.hpp"
    "src/ExtensionWrangler.cpp"
    "src/PipelineExecutableInfo.cpp"
    "src/RhiSystem.cpp"
)

TARGET_LINK_LIBRARIES(rhi_system PUBLIC vpr_core ${Vulkan_LIBRARY})
TARGET_INCLUDE_DIRECTORIES(rhi_system PUBLIC ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    "../../third_party/json/include" ${Vulkan_INCLUDE_DIR})

IF(RHI_SYSTEM_USE_DEBUG_INFO)
    TARGET_COMPILE_DEFINITIONS(rhi_system PUBLIC RHI_SYSTEM_USE_DEBUG_INFO_CONF)
ENDIF()

IF(RHI_SYSTEM_VALIDATION_ENABLED)
    TARGET_COMPILE_DEFINITIONS(rhi_system PUBLIC RHI_SYSTEM_VALIDATION_ENABLED_CONF)
ENDIF()

IF(RHI_SYSTEM_DEBUG_INFO_THREAD_ID)
    TARGET_COMPILE_DEFINITIONS(rhi_system PUBLIC RHI_SYSTEM_DEBUG_INFO_THREAD_ID_CONF)
ENDIF()

IF(RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS)
    TARGET_COMPILE_DEFINITIONS(rhi_system PUBLIC RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS_CONF)
ENDIF()

IF(RHI_SYSTEM_DEBUG_INFO_CALLING_FN)
    TARGET_COMPILE_DEFINITIONS(rhi_system PUBLIC RHI_SYSTEM_DEBUG_INFO_CALLING_FN_CONF)
ENDIF()

find_package(Python COMPONENTS Interpreter)

# Define paths for dependency tracking
set(RHI_SYSTEM_VK_XML_PATH "$ENV{VULKAN_SDK}/share/vulkan/registry/vk.xml")
set(RHI_SYSTEM_GENERATOR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/GenerateExtensionAndFeatureHeaders.py")
set(RHI_SYSTEM_OUTPUT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/GeneratedExtensionHeader.hpp")

# Custom command that only runs when dependencies change
add_custom_command(
    OUTPUT "${RHI_SYSTEM_OUTPUT_HEADER}"
    COMMAND ${Python_EXECUTABLE} "${RHI_SYSTEM_GENERATOR_SCRIPT}"
                                 "-specDir=$ENV{VULKAN_SDK}" "-outputDir=${CMAKE_CURRENT_SOURCE_DIR}/src"
    DEPENDS "${RHI_SYSTEM_VK_XML_PATH}" "${RHI_SYSTEM_GENERATOR_SCRIPT}"
    COMMENT "Generating extension lookup header (dependencies changed)"
    VERBATIM
)

# Custom target that depends on the output file
add_custom_target(generate_extension_headers
    DEPENDS "${OUTPUT_HEADER}"
)

add_dependencies(rhi_system generate_extension_headers)

# throw this in the modules subdir in the IDE, so it's at least not cluttering the root
set_target_properties(generate_extension_headers PROPERTIES FOLDER "Modules")
