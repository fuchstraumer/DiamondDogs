
# the plain types (ResourceTypes.hpp and what it includes), with no RHI: code that must not reach the device links this
# target alone. foundation does not export its folder, so this target adds it
add_library(rhi_resource_types INTERFACE)
target_include_directories(rhi_resource_types INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
target_link_libraries(rhi_resource_types INTERFACE foundation)
bootstrap_assert_no_rhi(rhi_resource_types)

ADD_MODULE(rhi_resources
    "include/ManifestDescriptors.hpp"
    "include/ResourceBarriers.hpp"
    "include/ResourceContext.hpp"
    "include/RhiResourceFlags.hpp"
    "include/ResourceFuture.hpp"
    "include/ResourcePlacement.hpp"
    "include/ResourceTypes.hpp"
    "src/FutureTable.cpp"
    "src/FutureTable.hpp"
    "src/ManifestDescriptors.cpp"
    "src/ResourceBarriers.cpp"
    "src/ResourceContext.cpp"
    "src/ResourceContextImpl.cpp"
    "src/ResourceContextImpl.hpp"
    "src/ResourceMessageTypesInternal.hpp"
    "src/ResourceMessageTypesInternal.cpp"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

TARGET_LINK_LIBRARIES(rhi_resources PUBLIC rhi_resource_types rhi_system ${Vulkan_LIBRARY})
TARGET_LINK_LIBRARIES(rhi_resources PRIVATE EnTT VulkanMemoryAllocator)
