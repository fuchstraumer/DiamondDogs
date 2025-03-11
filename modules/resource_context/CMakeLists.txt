
ADD_MODULE(resource_context
    "include/ResourceContext.hpp"
    "include/ResourceMessageReply.hpp"
    "include/ResourceLoader.hpp"
    "include/ResourceTypes.hpp"
    "include/TransferSystem.hpp"
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

TARGET_INCLUDE_DIRECTORIES(resource_context PRIVATE
    ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    ${Vulkan_INCLUDE_DIR}
    "../../third_party/svs"
    "../../third_party/json/include"
    "../../third_party/entt/include"
)

TARGET_LINK_LIBRARIES(resource_context PUBLIC EnTT vpr_core vpr_command vpr_sync ${Vulkan_LIBRARY} VulkanMemoryAllocator)
