
# the plain types (ResourceTypes.hpp and what it includes), with no RHI: code that must not reach the device links this
# target alone. foundation does not export its folder, so this target adds it
add_library(rhi_resource_types INTERFACE)
target_include_directories(rhi_resource_types INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
target_link_libraries(rhi_resource_types INTERFACE foundation)
bootstrap_assert_no_rhi(rhi_resource_types)

# the translation from usage bits to the RHI's barrier types (ResourceBarriers.hpp). It reads the plain types only, so
# code that must not reach the device, Loom's core among it, can call it. rhi_resources links it
add_module(rhi_resource_barriers
    "include/ResourceBarriers.hpp"
    "src/ResourceBarriers.cpp"
)
target_link_libraries(rhi_resource_barriers PUBLIC rhi_types rhi_resource_types)
bootstrap_assert_no_rhi(rhi_resource_barriers)

if(EMSCRIPTEN)
    add_module(rhi_resources
        "include/ResourceContext.hpp"
        "include/RhiResourceFlags.hpp"
        "include/ResourceFuture.hpp"
        "include/ResourceTypes.hpp"
        "include/ResourceContextWebGpu.hpp"
        "src/FutureTable.cpp"
        "src/FutureTable.hpp"
        "src/ResourceContext.cpp"
        "src/ResourceMessageTypesInternal.hpp"
        "src/ResourceMessageTypesInternal.cpp"
        "src/ResourceTypes.cpp"
        "src/webgpu/FutureTableWaitWebGpu.cpp"
        "src/webgpu/ResourceContextImpl.hpp"
        "src/webgpu/ResourceContextImpl.cpp"
    )

    target_include_directories(rhi_resources PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
    target_include_directories(rhi_resources PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/webgpu"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_link_libraries(rhi_resources PUBLIC rhi_resource_types rhi_resource_barriers rhi_context emdawnwebgpu_cpp)
    target_link_libraries(rhi_resources PRIVATE EnTT)
else()
    ADD_MODULE(rhi_resources
        "include/ManifestDescriptors.hpp"
        "include/ResourceContext.hpp"
        "include/RhiResourceFlags.hpp"
        "include/ResourceFuture.hpp"
        "include/ResourcePlacement.hpp"
        "include/ResourceTypes.hpp"
        "src/FutureTable.cpp"
        "src/FutureTable.hpp"
        "src/vulkan/FutureTableWaitVulkan.cpp"
        "src/ManifestDescriptors.cpp"
        "src/ResourceContext.cpp"
        "src/vulkan/ResourceContextImpl.cpp"
        "src/vulkan/ResourceContextImpl.hpp"
        "src/ResourceMessageTypesInternal.hpp"
        "src/ResourceMessageTypesInternal.cpp"
        "src/TransferSystemMessagesInternal.hpp"
        "src/ResourcePlacement.cpp"
        "src/ResourceTypes.cpp"
        "src/TransferSystem.cpp"
        "src/TransferSystem.hpp"
        "src/UploadBuffer.cpp"
        "src/UploadBuffer.hpp"
        "src/VulkanConversions.cpp"
        "src/VulkanConversions.hpp"
    )

    # the public headers include foundation headers (EnumFlagUtils, ImageDataFormats), and foundation does not export its folder
    TARGET_INCLUDE_DIRECTORIES(rhi_resources PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include"
        ${Vulkan_INCLUDE_DIR})
    TARGET_INCLUDE_DIRECTORIES(rhi_resources PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    TARGET_LINK_LIBRARIES(rhi_resources PUBLIC rhi_resource_types rhi_resource_barriers rhi_context rhi_system ${Vulkan_LIBRARY})
    TARGET_LINK_LIBRARIES(rhi_resources PRIVATE EnTT VulkanMemoryAllocator)
endif()
