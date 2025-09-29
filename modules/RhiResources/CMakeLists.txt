
ADD_MODULE(rhi_resources
    "include/ResourceBarriers.hpp"
    "include/ResourceContext.hpp"
    "include/ResourceFlags.hpp"
    "include/ResourceLoader.hpp"
    "include/ResourceMessageReply.hpp"
    "include/ResourceTypes.hpp"
    "include/TransferSystem.hpp"
    "src/CreateInfoWrappers.hpp"
    "src/CreateInfoWrappers.cpp"
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
    "src/UploadBuffer.cpp"
    "src/UploadBuffer.hpp"
)

TARGET_INCLUDE_DIRECTORIES(rhi_resources PRIVATE
    ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    ${Vulkan_INCLUDE_DIR}
    "../../third_party/svs"
    "../../third_party/json/include"
    "../../third_party/entt/include"
    "../RhiSystem/include"
)

TARGET_LINK_LIBRARIES(rhi_resources PUBLIC EnTT vpr_core vpr_command vpr_sync ${Vulkan_LIBRARY} VulkanMemoryAllocator rhi_system)
