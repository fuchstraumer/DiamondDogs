ADD_EXECUTABLE(DisplayProbe main.cpp)

TARGET_INCLUDE_DIRECTORIES(DisplayProbe PRIVATE
    ${Vulkan_INCLUDE_DIR}
    "${CMAKE_SOURCE_DIR}/modules/PlatformSystem/include"
    "${CMAKE_SOURCE_DIR}/modules/PlatformSystem/src/Win32"
    "${CMAKE_SOURCE_DIR}/foundation/include")
TARGET_COMPILE_DEFINITIONS(DisplayProbe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
TARGET_LINK_LIBRARIES(DisplayProbe PRIVATE ${Vulkan_LIBRARY} glfw dxgi platform_system)
SET_TARGET_PROPERTIES(DisplayProbe PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
SET_TARGET_PROPERTIES(DisplayProbe PROPERTIES FOLDER "Tests/Integration Tests")
diamond_dogs_add_pch(DisplayProbe LIBRARY)
