
ADD_MODULE(resource_context
    "include/ResourceContext.hpp"
    "include/ResourceLoader.hpp"
    "include/ResourceTypes.hpp"
    "include/TransferSystem.hpp"
    "src/ResourceContext.cpp"
    "src/ResourceLoader.cpp"
    "src/TransferSystem.cpp"
    "src/UploadBuffer.hpp"
)

TARGET_INCLUDE_DIRECTORIES(resource_context PRIVATE ${VPR_INCLUDE_DIRS_MODULE_RELATIVE} ${Vulkan_INCLUDE_DIR})
TARGET_LINK_LIBRARIES(resource_context PUBLIC vpr_core vpr_alloc vpr_command vpr_sync ${Vulkan_LIBRARY} easyloggingpp)
