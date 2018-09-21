
ADD_INTEGRATION_TEST(ResourceContextTest
    "${CMAKE_CURRENT_SOURCE_DIR}/ComplexScene.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComplexScene.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
    "../fixtures/ObjModel.hpp"
    "../fixtures/ObjModel.cpp"
)

TARGET_INCLUDE_DIRECTORIES(ResourceContextTest PRIVATE 
    "${Vulkan_INCLUDE_DIR}"
    "../../../third_party/glm"
    "../fixtures/"
    "../../../third_party"
    "../../../ext/include"
    "../../../third_party/gli"
    "../../../third_party/tinyobjloader/experimental"
)

TARGET_LINK_LIBRARIES(ResourceContextTest PRIVATE vpr_core vpr_resource
vpr_sync vpr_alloc vpr_command ${Vulkan_LIBRARY} easyloggingpp)

IF(APPLE)# Needed to support Boost filesystem
SET(boost_min_ver 1.67.0)
SET(boost_libs system filesystem)
FIND_PACKAGE(Boost ${boost_min_ver})
IF(Boost_FOUND)
FIND_PACKAGE(Boost ${boost_min_ver} COMPONENTS ${boost_libs})
ENDIF()
TARGET_LINK_LIBRARIES(ResourceContextTest PRIVATE ${Boost_LIBRARIES})
TARGET_INCLUDE_DIRECTORIES(ResourceContextTest PRIVATE ${Boost_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})
ENDIF()
