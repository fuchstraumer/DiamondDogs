
ADD_MODULE(rhi_resources
    "include/ResourceBarriers.hpp"
    "include/ResourceContext.hpp"
    "include/ResourceFlags.hpp"
    "include/ResourceLoader.hpp"
    "include/ResourceMessageReply.hpp"
    "include/ResourceTypes.hpp"
    "src/ResourceBarriers.cpp"
    "src/ResourceContext.cpp"
    "src/ResourceContextImpl.cpp"
    "src/ResourceContextImpl.hpp"
    "src/ResourceDataArena.hpp"
    "src/ResourceDataArena.cpp"
    "src/ResourceLoader.cpp"
    "src/ResourceMessageReply.cpp"
    "src/ResourceMessageTypesInternal.hpp"
    "src/ResourceMessageTypesInternal.cpp"
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

TARGET_LINK_LIBRARIES(rhi_resources PUBLIC rhi_system ${Vulkan_LIBRARY})
TARGET_LINK_LIBRARIES(rhi_resources PRIVATE EnTT VulkanMemoryAllocator)
