# The offline cooker console: boots the launch and core stages, and nothing that needs a device. It is a stub today,
# so that the build proves that a cooker links without the RHI.
add_executable(ContentCooker "main.cpp")
target_link_libraries(ContentCooker PRIVATE bootstrap content_compiler)
set_target_properties(ContentCooker PROPERTIES FOLDER "Applications" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

if(MSVC)
    target_compile_definitions(ContentCooker PRIVATE "NOMINMAX")
endif()

bootstrap_assert_no_rhi(ContentCooker)
