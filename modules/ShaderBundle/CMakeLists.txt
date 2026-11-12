
# rhi::ShaderBundle reads a cooked Lodestone bundle with no graphics device. It lives outside rhi_system so that code
# which runs before a device exists (the boot stages, a cooker) can open bundles without linking the RHI.
ADD_MODULE(shader_bundle
    "include/ShaderBundle.hpp"
    "src/ShaderBundle.cpp")

target_include_directories(shader_bundle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(shader_bundle PUBLIC lodestone::client_library lodestone::client_interface)
