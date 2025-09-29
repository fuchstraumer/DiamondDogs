
OPTION(RHI_SYSTEM_USE_DEBUG_INFO "Enable debug info for RHI system" ON)
OPTION(RHI_SYSTEM_VALIDATION_ENABLED "Enable validation for RHI system" ON)
OPTION(RHI_SYSTEM_DEBUG_INFO_THREAD_ID "Enable thread ID in debug info for RHI system" ON)
OPTION(RHI_SYSTEM_DEBUG_INFO_TIMESTAMPS "Enable timestamps in debug info for RHI system" ON)
OPTION(RHI_SYSTEM_DEBUG_INFO_CALLING_FN "Enable calling function name in debug info for RHI system" OFF)

# Graphics API selection - Vulkan is default
set(RHI_SYSTEM_GRAPHICS_API "Vulkan" CACHE STRING "Graphics API to use (Vulkan, DirectX12)")
set_property(CACHE RHI_SYSTEM_GRAPHICS_API PROPERTY STRINGS "Vulkan" "DirectX12")

set(RHI_CORE_HEADERS
    "include/core/DebugUtilFns.hpp"
    "include/core/ExtensionPack.hpp"
    "include/core/ExtensionWrangler.hpp"
    "include/core/PipelineExecutableInfo.hpp"
    "include/core/RhiDefines.hpp"
    "include/core/RhiSystem.hpp"
    "include/core/RhiFlags.hpp"
    "include/core/RhiTypes.hpp")

set(RHI_CORE_SOURCES 
    "src/core/ExtensionWrangler.cpp"
    "src/core/ExtensionPack.cpp"
    "src/core/PipelineExecutableInfo.cpp"
    "src/core/RhiSystem.cpp")

set(RHI_OBJECT_HEADERS
    "include/objects/CommandBuffer.hpp"
    "include/objects/CommandPool.hpp"
    "include/objects/Device.hpp"
    "include/objects/Fence.hpp"
    "include/objects/Instance.hpp"
    "include/objects/RhiFunctions.hpp"
    "include/objects/RhiHandle.hpp"
    "include/objects/RhiResult.hpp"
    "include/objects/Semaphore.hpp"
    "include/objects/ShaderObject.hpp")

set(RHI_OBJECT_SOURCES
    "src/objects/CommandBuffer.cpp"
    "src/objects/CommandPool.cpp"
    "src/objects/Device.cpp"
    "src/objects/Fence.cpp"
    "src/objects/Instance.cpp"
    "src/objects/RhiFunctionsVk.cpp"
    "src/objects/RhiResult.cpp"
    "src/objects/Semaphore.cpp"
    "src/objects/ShaderObject.cpp")

ADD_MODULE(rhi_system "${RHI_CORE_HEADERS}""${RHI_CORE_SOURCES}" "${RHI_OBJECT_HEADERS}" "${RHI_OBJECT_SOURCES}")

if(RHI_SYSTEM_GRAPHICS_API STREQUAL "Vulkan")
    target_link_libraries(rhi_system PUBLIC ${Vulkan_LIBRARY})
    target_include_directories(rhi_system PUBLIC ${Vulkan_INCLUDE_DIR})
    target_compile_definitions(rhi_system PUBLIC RHI_SYSTEM_USE_VULKAN_CONF)
    message(STATUS "RHI System: Using Vulkan as graphics API")
elseif(RHI_SYSTEM_GRAPHICS_API STREQUAL "DX12")
    message(FATAL_ERROR "DirectX 12 support not yet implemented")
else()
    message(FATAL_ERROR "Unknown RHI_SYSTEM_GRAPHICS_API: ${RHI_SYSTEM_GRAPHICS_API}. Supported values are Vulkan and DirectX12.")
endif()

TARGET_LINK_LIBRARIES(rhi_system PUBLIC ${Vulkan_LIBRARY} slang)
TARGET_INCLUDE_DIRECTORIES(rhi_system PUBLIC
    "../../third_party/json/include"
    "../../third_party/slang/include"
    ${Vulkan_INCLUDE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/objects")

source_group("Core\\include" FILES ${RHI_CORE_HEADERS})
source_group("Core\\src" FILES ${RHI_CORE_SOURCES})
source_group("Objects\\include" FILES ${RHI_OBJECT_HEADERS})
source_group("Objects\\src" FILES ${RHI_OBJECT_SOURCES})

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

if (RHI_SYSTEM_GRAPHICS_API STREQUAL "Vulkan")
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
    set_target_properties(generate_extension_headers PROPERTIES FOLDER "Generators")
endif()
