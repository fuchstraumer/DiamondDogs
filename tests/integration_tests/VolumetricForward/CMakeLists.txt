
SET(SCENE_GUI_SOURCES 
    "../fixtures/ImGuiWrapper.hpp"
    "../fixtures/ImGuiWrapper.cpp"
    "../../../third_party/imgui/imgui.h"
    "../../../third_party/imgui/imgui.cpp"
    "../../../third_party/imgui/imgui_draw.cpp"
    "../../../third_party/imgui/imconfig.h"
    "../../../third_party/imgui/imgui_demo.cpp"
    "../../../third_party/imgui/imgui_internal.h"
    "../../../third_party/imgui/imgui_widgets.cpp"
)

ADD_INTEGRATION_TEST(VTF_Scene
    "main.cpp"
    "VTF_Scene.cpp"
    "VTF_Scene.hpp"
    "vtfFrameData.cpp"
    "vtfFrameData.hpp"
    "vtfStructs.hpp"
    "vtfTasksAndSteps.cpp"
    "vtfTasksAndSteps.hpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
    "../fixtures/PerspectiveCamera.hpp"
    "../fixtures/PerspectiveCamera.cpp"
    "../fixtures/ObjModel.hpp"
    "../fixtures/ObjModel.cpp"
    "../fixtures/MaterialCache.hpp"
    "../fixtures/MaterialCache.cpp"
    "../fixtures/MaterialStructures.hpp"
    "../fixtures/MurmurHash.hpp"
    "../fixtures/MurmurHash.cpp"
    ${SCENE_GUI_SOURCES}
)

SOURCE_GROUP("imgui" FILES ${SCENE_GUI_SOURCES})

TARGET_INCLUDE_DIRECTORIES(VTF_Scene PRIVATE
    "../../../third_party/shadertools/include"
    "../../../modules/rendering_context/include"
    "../../../modules/resource_context/include"
    "../../../modules/rendergraph_module/include"
    "../../../third_party/easyloggingpp/src"
    "../../../third_party/glm/"
    ${Vulkan_INCLUDE_DIR}
    "../../../third_party/vulpesrender/core/include"
    "../../../third_party/vulpesrender/resource/include"
    "../../../third_party/vulpesrender/alloc/include"
    "../../../third_party/vulpesrender/command/include"
    "../../../third_party/vulpesrender/sync/include"
    "../../../third_party/vulpesrender/render/include"
    "../../../third_party/svs"
    "../../../third_party/tinyobjloader/experimental"
)

TARGET_LINK_LIBRARIES(VTF_Scene PRIVATE ShaderTools rendering_context resource_context rendergraph_module glfw ${Vulkan_LIBRARY}
    vpr_core vpr_resource vpr_sync vpr_command vpr_render easyloggingpp mango)
