CMAKE_MINIMUM_REQUIRED(VERSION 3.4)
PROJECT(DiamondDogs)
# and turquoise days...

FIND_PACKAGE(Vulkan REQUIRED)
ADD_SUBDIRECTORY(third_party/vulpesrender)
ADD_SUBDIRECTORY(third_party/shadertools)
SET(build_static_lib ON CACHE BOOL "Build easyloggingpp as a static library")
ADD_SUBDIRECTORY(third_party/easyloggingpp)
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(core)
ADD_SUBDIRECTORY(tests/integration_tests)
