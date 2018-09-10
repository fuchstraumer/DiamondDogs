CMAKE_MINIMUM_REQUIRED(VERSION 3.4)
PROJECT(DiamondDogs)
# and turquoise days...

FIND_PACKAGE(Vulkan REQUIRED)
ADD_SUBDIRECTORY(third_party/vulpesrender)
ADD_SUBDIRECTORY(third_party/shadertools)

ADD_SUBDIRECTORY(third_party/easyloggingpp)
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(core)
