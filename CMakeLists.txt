CMAKE_MINIMUM_REQUIRED(VERSION 3.4)
PROJECT(DiamondDogs)
# and turquoise days...

SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Gy")
SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Oi /MP /Gy")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Gy")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /JMC")


FIND_PACKAGE(Vulkan REQUIRED)
ADD_SUBDIRECTORY(third_party/vulpesrender)
ADD_SUBDIRECTORY(third_party/shadertools)
add_subdirectory(third_party/vma)
add_subdirectory(third_party/entt)
add_subdirectory(third_party/mimalloc)
# Foundation is core functionality that wouldn't make sense to put into modules:
# Threading primitives, utilities, exception helpers, containers, etc
# Also our building block reactor objects, used to build higher-level systems
ADD_SUBDIRECTORY(foundation)
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(tests/integration_tests)
