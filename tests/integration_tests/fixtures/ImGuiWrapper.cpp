#include "ImGuiWrapper.hpp"
#include "ShaderModule.hpp"
#include "PipelineCache.hpp"
#include "PipelineLayout.hpp"
#include "DescriptorSet.hpp"
#include "DescriptorSetLayout.hpp"
#include "DescriptorPool.hpp"
#include "Renderpass.hpp"
#include "LogicalDevice.hpp"
#include "PhysicalDevice.hpp"
#include "CreateInfoBase.hpp"
#include "Swapchain.hpp"
#include "GraphicsPipeline.hpp"
#include "RenderingContext.hpp"
#include "ResourceContext.hpp"
#include "ResourceTypes.hpp"
#include <vector>
#include "GLFW/glfw3.h"
#include <ratio>

static std::array<bool, 5> mouse_pressed{ false, false, false, false, false };
static double MouseScrollX = 0.0;
static double MouseScrollY = 0.0;
static std::array<bool, 512> keys;
static std::array<GLFWcursor*, size_t(ImGuiMouseCursor_COUNT)> standardCursors;

// From imgui_impl_vulkan.cpp, in imgui/examples
// glsl_shader.vert, compiled with:
// # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
constexpr static uint32_t __glsl_shader_vert_spv[] =
{
    0x07230203,0x00010000,0x00080001,0x0000002e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x000a000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x0000000f,0x00000015,
    0x0000001b,0x0000001c,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00030005,0x00000009,0x00000000,0x00050006,0x00000009,0x00000000,0x6f6c6f43,
    0x00000072,0x00040006,0x00000009,0x00000001,0x00005655,0x00030005,0x0000000b,0x0074754f,
    0x00040005,0x0000000f,0x6c6f4361,0x0000726f,0x00030005,0x00000015,0x00565561,0x00060005,
    0x00000019,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x00000019,0x00000000,
    0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000001b,0x00000000,0x00040005,0x0000001c,
    0x736f5061,0x00000000,0x00060005,0x0000001e,0x73755075,0x6e6f4368,0x6e617473,0x00000074,
    0x00050006,0x0000001e,0x00000000,0x61635375,0x0000656c,0x00060006,0x0000001e,0x00000001,
    0x61725475,0x616c736e,0x00006574,0x00030005,0x00000020,0x00006370,0x00040047,0x0000000b,
    0x0000001e,0x00000000,0x00040047,0x0000000f,0x0000001e,0x00000002,0x00040047,0x00000015,
    0x0000001e,0x00000001,0x00050048,0x00000019,0x00000000,0x0000000b,0x00000000,0x00030047,
    0x00000019,0x00000002,0x00040047,0x0000001c,0x0000001e,0x00000000,0x00050048,0x0000001e,
    0x00000000,0x00000023,0x00000000,0x00050048,0x0000001e,0x00000001,0x00000023,0x00000008,
    0x00030047,0x0000001e,0x00000002,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,
    0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040017,
    0x00000008,0x00000006,0x00000002,0x0004001e,0x00000009,0x00000007,0x00000008,0x00040020,
    0x0000000a,0x00000003,0x00000009,0x0004003b,0x0000000a,0x0000000b,0x00000003,0x00040015,
    0x0000000c,0x00000020,0x00000001,0x0004002b,0x0000000c,0x0000000d,0x00000000,0x00040020,
    0x0000000e,0x00000001,0x00000007,0x0004003b,0x0000000e,0x0000000f,0x00000001,0x00040020,
    0x00000011,0x00000003,0x00000007,0x0004002b,0x0000000c,0x00000013,0x00000001,0x00040020,
    0x00000014,0x00000001,0x00000008,0x0004003b,0x00000014,0x00000015,0x00000001,0x00040020,
    0x00000017,0x00000003,0x00000008,0x0003001e,0x00000019,0x00000007,0x00040020,0x0000001a,
    0x00000003,0x00000019,0x0004003b,0x0000001a,0x0000001b,0x00000003,0x0004003b,0x00000014,
    0x0000001c,0x00000001,0x0004001e,0x0000001e,0x00000008,0x00000008,0x00040020,0x0000001f,
    0x00000009,0x0000001e,0x0004003b,0x0000001f,0x00000020,0x00000009,0x00040020,0x00000021,
    0x00000009,0x00000008,0x0004002b,0x00000006,0x00000028,0x00000000,0x0004002b,0x00000006,
    0x00000029,0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,
    0x00000005,0x0004003d,0x00000007,0x00000010,0x0000000f,0x00050041,0x00000011,0x00000012,
    0x0000000b,0x0000000d,0x0003003e,0x00000012,0x00000010,0x0004003d,0x00000008,0x00000016,
    0x00000015,0x00050041,0x00000017,0x00000018,0x0000000b,0x00000013,0x0003003e,0x00000018,
    0x00000016,0x0004003d,0x00000008,0x0000001d,0x0000001c,0x00050041,0x00000021,0x00000022,
    0x00000020,0x0000000d,0x0004003d,0x00000008,0x00000023,0x00000022,0x00050085,0x00000008,
    0x00000024,0x0000001d,0x00000023,0x00050041,0x00000021,0x00000025,0x00000020,0x00000013,
    0x0004003d,0x00000008,0x00000026,0x00000025,0x00050081,0x00000008,0x00000027,0x00000024,
    0x00000026,0x00050051,0x00000006,0x0000002a,0x00000027,0x00000000,0x00050051,0x00000006,
    0x0000002b,0x00000027,0x00000001,0x00070050,0x00000007,0x0000002c,0x0000002a,0x0000002b,
    0x00000028,0x00000029,0x00050041,0x00000011,0x0000002d,0x0000001b,0x0000000d,0x0003003e,
    0x0000002d,0x0000002c,0x000100fd,0x00010038
};

// glsl_shader.frag, compiled with:
// # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
constexpr static uint32_t __glsl_shader_frag_spv[] =
{
    0x07230203,0x00010000,0x00080001,0x0000001e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000d,0x00030010,
    0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00040005,0x00000009,0x6c6f4366,0x0000726f,0x00030005,0x0000000b,0x00000000,
    0x00050006,0x0000000b,0x00000000,0x6f6c6f43,0x00000072,0x00040006,0x0000000b,0x00000001,
    0x00005655,0x00030005,0x0000000d,0x00006e49,0x00050005,0x00000016,0x78655473,0x65727574,
    0x00000000,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,
    0x00000000,0x00040047,0x00000016,0x00000022,0x00000000,0x00040047,0x00000016,0x00000021,
    0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
    0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,
    0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,0x0000000a,0x00000006,
    0x00000002,0x0004001e,0x0000000b,0x00000007,0x0000000a,0x00040020,0x0000000c,0x00000001,
    0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,0x00040015,0x0000000e,0x00000020,
    0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040020,0x00000010,0x00000001,
    0x00000007,0x00090019,0x00000013,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,
    0x00000001,0x00000000,0x0003001b,0x00000014,0x00000013,0x00040020,0x00000015,0x00000000,
    0x00000014,0x0004003b,0x00000015,0x00000016,0x00000000,0x0004002b,0x0000000e,0x00000018,
    0x00000001,0x00040020,0x00000019,0x00000001,0x0000000a,0x00050036,0x00000002,0x00000004,
    0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000010,0x00000011,0x0000000d,
    0x0000000f,0x0004003d,0x00000007,0x00000012,0x00000011,0x0004003d,0x00000014,0x00000017,
    0x00000016,0x00050041,0x00000019,0x0000001a,0x0000000d,0x00000018,0x0004003d,0x0000000a,
    0x0000001b,0x0000001a,0x00050057,0x00000007,0x0000001c,0x00000017,0x0000001b,0x00050085,
    0x00000007,0x0000001d,0x00000012,0x0000001c,0x0003003e,0x00000009,0x0000001d,0x000100fd,
    0x00010038
};

constexpr static VkDeviceSize bufferSize(VkDeviceSize sz) {
    return ((sz - 1) / 256 + 1) * 256;
}

void MouseButtonCallback(int button, int action, int mods) {
    if (button >= 0 && button < 5) {
        if (action == GLFW_PRESS) {
            mouse_pressed[button] = true;
        }
        else if (action == GLFW_RELEASE) {
            mouse_pressed[button] = false;
        }
    }
}

void KeyCallback(int key, int scancode, int action, int mods) {

    if (action == GLFW_PRESS) {
        keys[key] = true;
    }
    if (action == GLFW_RELEASE) {
        keys[key] = false;
    }
    
}

void ScrollCallback(double x_offset, double y_offset) {
    auto& io = ImGui::GetIO();
    io.MouseWheelH += static_cast<float>(x_offset);
    io.MouseWheel += static_cast<float>(y_offset);
    //auto& arc = ArcballCamera::GetCamera();
    //arc.ScrollAmount += static_cast<float>(y_offset);
}

void CharCallback(unsigned int code_point) {
    auto& io = ImGui::GetIO();
    if (code_point > 0 && code_point < 0x10000) {
        io.AddInputCharacter(static_cast<ImWchar>(code_point));
    }
}

ImGuiWrapper::ImGuiWrapper() {}

ImGuiWrapper::~ImGuiWrapper() {
    Destroy();
}

ImGuiWrapper& ImGuiWrapper::GetImGuiWrapper() {
    static ImGuiWrapper gui;
    return gui;
}

void ImGuiWrapper::Construct(VkRenderPass renderpass) {
    static bool callbacks_registered = false;
    auto& rendering_context = RenderingContext::Get();
    auto& resource_context = ResourceContext::Get();

    rendererContext = &rendering_context;
    resourceContext = &resource_context;
    if (!callbacks_registered) {
        rendererContext->RegisterScrollCallback(scroll_callback_t::create<&ScrollCallback>());
        rendererContext->RegisterCharCallback(char_callback_t::create<&CharCallback>());
        rendererContext->RegisterMouseButtonCallback(mouse_button_callback_t::create<&MouseButtonCallback>());
        rendererContext->RegisterKeyboardKeyCallback(keyboard_key_callback_t::create<&KeyCallback>());
        callbacks_registered = true;
    }
    frameData.resize(rendererContext->Swapchain()->ImageCount());

    ImGui::CreateContext();
    device = rendererContext->Device();    
    createResources();
    createGraphicsPipeline(renderpass);
    timePointA = std::chrono::high_resolution_clock::now();
    timePointB = std::chrono::high_resolution_clock::now();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
    io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
    io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
    io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
    io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
    io.KeyMap[ImGuiKey_Insert] = GLFW_KEY_INSERT;
    io.KeyMap[ImGuiKey_Delete] = GLFW_KEY_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
    io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
    io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
    io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
    io.KeyMap[ImGuiKey_A] = GLFW_KEY_A;
    io.KeyMap[ImGuiKey_C] = GLFW_KEY_C;
    io.KeyMap[ImGuiKey_V] = GLFW_KEY_V;
    io.KeyMap[ImGuiKey_X] = GLFW_KEY_X;
    io.KeyMap[ImGuiKey_Y] = GLFW_KEY_Y;
    io.KeyMap[ImGuiKey_Z] = GLFW_KEY_Z;
    //io.ImeWindowHandle = rendererContext->GetWin32_WindowHandle();

    standardCursors[ImGuiMouseCursor_Arrow] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_ARROW_CURSOR);
    standardCursors[ImGuiMouseCursor_TextInput] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_IBEAM_CURSOR);
    standardCursors[ImGuiMouseCursor_ResizeAll] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_ARROW_CURSOR);
    standardCursors[ImGuiMouseCursor_ResizeNS] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_VRESIZE_CURSOR);
    standardCursors[ImGuiMouseCursor_ResizeEW] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_HRESIZE_CURSOR);
    standardCursors[ImGuiMouseCursor_ResizeNESW] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_ARROW_CURSOR);
    standardCursors[ImGuiMouseCursor_ResizeNWSE] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_ARROW_CURSOR);
    standardCursors[ImGuiMouseCursor_Hand] = (GLFWcursor*)rendererContext->CreateStandardCursor(GLFW_HAND_CURSOR);

}

void ImGuiWrapper::Destroy() {
    setLayout.reset();
    descriptorSet.reset();
    cache.reset();
    vert.reset();
    frag.reset();
    pipeline.reset();
    layout.reset();
    descriptorPool.reset();
    for (auto& frame : frameData) {
        if (frame.vbo != nullptr) {
            resourceContext->DestroyResource(frame.vbo);
        }
        if (frame.ebo != nullptr) {
            resourceContext->DestroyResource(frame.ebo);
        }
    }
    frameData.clear();
    frameData.shrink_to_fit();
    for (auto* cursor : standardCursors) {
        rendererContext->DestroyCursor(cursor);
    }
    ImGui::DestroyContext();
}

void ImGuiWrapper::NewFrame() {
    newFrame();
}

void ImGuiWrapper::EndImGuiFrame() {
    ImGui::Render();
}

void ImGuiWrapper::newFrame() {

    auto& io = ImGui::GetIO();

    timePointA = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> work_time = timePointA - timePointB;
    double work_time_seconds = static_cast<double>(work_time.count());
    io.DeltaTime = static_cast<float>(work_time_seconds);
    vpr::Swapchain* swapchain = rendererContext->Swapchain();
    io.DisplaySize = ImVec2(static_cast<float>(swapchain->Extent().width), static_cast<float>(swapchain->Extent().height));

    for (size_t i = 0; i < mouse_pressed.size(); ++i) {
        io.MouseDown[i] = mouse_pressed[i] || rendererContext->GetMouseButton(int(i)) != 0;
    }
    mouse_pressed.fill(false);
#ifdef _MSC_VER
    memcpy_s(io.KeysDown, sizeof(bool) * 512, keys.data(), sizeof(bool) * 512);
#else
    memcpy(io.KeysDown, keys.data(), sizeof(bool) * keys.size());
#endif
    io.KeyCtrl = keys[GLFW_KEY_LEFT_CONTROL] || keys[GLFW_KEY_RIGHT_CONTROL];
    io.KeyShift = keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT];
    io.KeyAlt = keys[GLFW_KEY_LEFT_ALT] || keys[GLFW_KEY_RIGHT_ALT];
    io.KeySuper = keys[GLFW_KEY_LEFT_SUPER] || keys[GLFW_KEY_RIGHT_SUPER];

    if (rendererContext->GetWindowAttribute(GLFW_FOCUSED)) {
        const ImVec2 mouse_pos_backup = io.MousePos;
        if (io.WantSetMousePos) {
            rendererContext->SetCursorPosition(static_cast<double>(mouse_pos_backup.x), static_cast<double>(mouse_pos_backup.y));
        }
        else {
            double mouse_x, mouse_y;
            rendererContext->GetCursorPosition(mouse_x, mouse_y);
            io.MousePos = ImVec2(static_cast<float>(mouse_x), static_cast<float>(mouse_y));
        }
    }
    else {
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    }

    if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) && !(rendererContext->GetInputMode(GLFW_CURSOR) == GLFW_CURSOR_DISABLED)) {
        ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
        if (cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
            rendererContext->SetInputMode(GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        }
        else {
            rendererContext->SetCursor(standardCursors[cursor] ? standardCursors[cursor] : standardCursors[ImGuiMouseCursor_Arrow]);
            rendererContext->SetInputMode(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    ImGui::NewFrame();

    timePointB = std::chrono::high_resolution_clock::now();
}

void ImGuiWrapper::DrawFrame(size_t frame_idx, VkCommandBuffer & cmd) {
    ImGuiFrameData* data = &frameData[frame_idx];

    updateBuffers(data);

    if ((data->currentVboSize == 0) || (data->currentEboSize == 0)) {
        return;
    }


    const auto& io = ImGui::GetIO();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vkHandle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->vkHandle(), 0, 1, &descriptorSet->vkHandle(), 0, nullptr);
    static constexpr VkDeviceSize offsets[1]{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, (VkBuffer*)&data->vbo->Handle, offsets);
    vkCmdBindIndexBuffer(cmd, (VkBuffer)data->ebo->Handle, 0, VK_INDEX_TYPE_UINT16);

    const ImDrawData* draw_data = ImGui::GetDrawData();
    VkViewport viewport{ 0, 0, io.DisplaySize.x, io.DisplaySize.y, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    float scale[2]{
        2.0f / io.DisplaySize.x,
        2.0f / io.DisplaySize.y
    };
    float translate[2] {
        -1.0f - draw_data->DisplayPos.x * scale[0], 
        -1.0f - draw_data->DisplayPos.y * scale[1]
    };

    vkCmdPushConstants(cmd, layout->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2, &scale);
    vkCmdPushConstants(cmd, layout->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, static_cast<uint32_t>(sizeof(float) * 2), static_cast<uint32_t>(sizeof(float) * 2), &translate);

    int32_t vtx_offset = 0;
    int32_t idx_offset = 0;
    const ImVec2 display_pos = draw_data->DisplayPos;
    for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
        const auto* cmd_list = draw_data->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; ++j) {
            const auto* draw_cmd = &cmd_list->CmdBuffer[j];
            VkRect2D scissor;
            scissor.offset.x = std::max(static_cast<int32_t>(draw_cmd->ClipRect.x - display_pos.x), 0);
            scissor.offset.y = std::max(static_cast<int32_t>(draw_cmd->ClipRect.y - display_pos.y), 0);
            scissor.extent.width = static_cast<uint32_t>(draw_cmd->ClipRect.z - draw_cmd->ClipRect.x);
            scissor.extent.height = static_cast<uint32_t>(draw_cmd->ClipRect.w - draw_cmd->ClipRect.y + 1);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdDrawIndexed(cmd, draw_cmd->ElemCount, 1, idx_offset, vtx_offset, 0);
            idx_offset += draw_cmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
    }
}

void ImGuiWrapper::createResources() {
    loadFontData();
    createFontImage();
    createFontSampler();
    createDescriptorSetLayout();
    createDescriptorPool();
    createDescriptorSet();
    createPipelineLayout();
    createCache();
    createShaders();
}

void ImGuiWrapper::loadFontData() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    io.Fonts->GetTexDataAsAlpha8(&fontTextureData, &imgWidth, &imgHeight);
}

void ImGuiWrapper::createFontImage() {

    const VkImageCreateInfo image_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8_UNORM,
        VkExtent3D{ static_cast<uint32_t>(imgWidth), static_cast<uint32_t>(imgHeight), 1 },
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        device->GetFormatTiling(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT),
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_info{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        VK_FORMAT_R8_UNORM,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    const gpu_image_resource_data_t image_data{
        fontTextureData,
        sizeof(uint8_t) * imgWidth * imgHeight,
        uint32_t(imgWidth),
        uint32_t(imgHeight),
        0,
        1,
        0,
		device->QueueFamilyIndices().Graphics
    };

    fontImage = resourceContext->CreateImage(&image_info, &view_info, 1, &image_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, "ImGuiFontImage");

}

void ImGuiWrapper::createFontSampler() {
    VkSamplerCreateInfo sampler_info = vpr::vk_sampler_create_info_base;
    fontSampler = resourceContext->CreateSampler(&sampler_info, ResourceCreateUserDataAsString, "ImGuiFontSampler");
}

void ImGuiWrapper::createDescriptorSetLayout() {
    setLayout = std::make_unique<vpr::DescriptorSetLayout>(device->vkHandle());
    setLayout->AddDescriptorBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
}

void ImGuiWrapper::createDescriptorPool() {
    descriptorPool = std::make_unique<vpr::DescriptorPool>(device->vkHandle(), 1);
    descriptorPool->AddResourceType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    descriptorPool->Create();
}

void ImGuiWrapper::createDescriptorSet() {
    descriptorSet = std::make_unique<vpr::DescriptorSet>(device->vkHandle());
    VkDescriptorImageInfo image_descriptor{ (VkSampler)fontSampler->Handle, (VkImageView)fontImage->ViewHandle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    descriptorSet->AddDescriptorInfo(image_descriptor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    descriptorSet->Init(descriptorPool->vkHandle(), setLayout->vkHandle());
    descriptorSet->Update();
}

void ImGuiWrapper::createPipelineLayout() {
    layout = std::make_unique<vpr::PipelineLayout>(device->vkHandle());
    const VkPushConstantRange push_constant{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4 };
    layout->Create(1, &push_constant, 1, &setLayout->vkHandle());
}

void ImGuiWrapper::createShaders() {
    vert = std::make_unique<vpr::ShaderModule>(device->vkHandle(), VK_SHADER_STAGE_VERTEX_BIT, __glsl_shader_vert_spv, static_cast<uint32_t>(sizeof(__glsl_shader_vert_spv)));
    frag = std::make_unique<vpr::ShaderModule>(device->vkHandle(), VK_SHADER_STAGE_FRAGMENT_BIT, __glsl_shader_frag_spv, static_cast<uint32_t>(sizeof(__glsl_shader_frag_spv)));
}

void ImGuiWrapper::createCache() {
    cache = std::make_unique<vpr::PipelineCache>(device->vkHandle(), rendererContext->PhysicalDevice()->vkHandle(), typeid(ImGuiWrapper).hash_code());
}

static constexpr VkVertexInputBindingDescription bind_descr{ 0, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX };
static constexpr std::array<VkVertexInputAttributeDescription, 3> attr_descr{
    VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
    VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32_SFLOAT,  sizeof(float) * 2 },
    VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 }
};

void ImGuiWrapper::createGraphicsPipeline(const VkRenderPass renderpass) {

    pipelineStateInfo = std::make_unique<vpr::GraphicsPipelineInfo>();

    pipelineStateInfo->VertexInfo.vertexBindingDescriptionCount = 1;
    pipelineStateInfo->VertexInfo.pVertexBindingDescriptions = &bind_descr;
    pipelineStateInfo->VertexInfo.vertexAttributeDescriptionCount = 3;
    pipelineStateInfo->VertexInfo.pVertexAttributeDescriptions = attr_descr.data();

    static const VkPipelineColorBlendAttachmentState color_blend{
        VK_TRUE,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    if (device->HasExtension(VK_NV_FILL_RECTANGLE_EXTENSION_NAME)) {
        pipelineStateInfo->RasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL_RECTANGLE_NV;
    }

    pipelineStateInfo->MultisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
    pipelineStateInfo->MultisampleInfo.sampleShadingEnable = VK_TRUE;
    pipelineStateInfo->ColorBlendInfo.attachmentCount = 1;
    pipelineStateInfo->ColorBlendInfo.pAttachments = &color_blend;
    // Set this through dynamic state so we can do it when rendering.
    pipelineStateInfo->DynamicStateInfo.dynamicStateCount = 2;
    static const VkDynamicState states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    pipelineStateInfo->DynamicStateInfo.pDynamicStates = states;
    pipelineStateInfo->DepthStencilInfo.depthTestEnable = VK_TRUE;
    pipelineStateInfo->DepthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineStateInfo->RasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    pipelineCreateInfo = pipelineStateInfo->GetPipelineCreateInfo();
    pipelineCreateInfo.flags = 0;
    pipelineCreateInfo.layout = layout->vkHandle();
    pipelineCreateInfo.renderPass = renderpass;
    pipelineCreateInfo.subpass = 0;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = -1;
    const std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{
        vert->PipelineInfo(),
        frag->PipelineInfo()
    };
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shader_stages.data();

    pipeline = std::make_unique<vpr::GraphicsPipeline>(device->vkHandle());
    pipeline->Init(pipelineCreateInfo, cache->vkHandle());

}

void ImGuiWrapper::updateBuffers(ImGuiFrameData* data) {

	const uint32_t graphics_queue_idx = device->QueueFamilyIndices().Graphics;

    const ImDrawData* draw_data = ImGui::GetDrawData();

    if (!draw_data) {
        return;
    }

    VkDeviceSize vtx_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize idx_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

    if ((vtx_size == 0) || (idx_size == 0)) {
        return;
    }

    if ((data->currentVboSize < vtx_size) || (data->vbo == nullptr)) {

        data->currentVboSize = bufferSize(vtx_size);

        if (data->vbo) {
            resourceContext->DestroyResource(data->vbo);
        }
        std::vector<gpu_resource_data_t> copies;

        VkDeviceSize vtx_offset = 0, idx_offset = 0;
        for (int i = 0; i < draw_data->CmdListsCount; ++i) {
            const ImDrawList* list = draw_data->CmdLists[i];
            copies.emplace_back(gpu_resource_data_t{ list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert), 0, graphics_queue_idx });
        }

        const VkBufferCreateInfo buffer_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            data->currentVboSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr
        };

        data->vbo = resourceContext->CreateBuffer(&buffer_info, nullptr, copies.size(), copies.data(), resource_usage::CPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation, nullptr);

    }
    else {
        // Just copy data.
        std::vector<gpu_resource_data_t> copies;

        VkDeviceSize vtx_offset = 0, idx_offset = 0;
        for (int i = 0; i < draw_data->CmdListsCount; ++i) {
            const ImDrawList* list = draw_data->CmdLists[i];
            copies.emplace_back(gpu_resource_data_t{ list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert), 0, graphics_queue_idx });
        }

        resourceContext->SetBufferData(data->vbo, copies.size(), copies.data());
    }

    if ((data->currentEboSize < idx_size) || (data->ebo == nullptr)) {

        data->currentEboSize = bufferSize(idx_size);

        if (data->ebo) {
            resourceContext->DestroyResource(data->ebo);
        }

        std::vector<gpu_resource_data_t> copies;

        for (int i = 0; i < draw_data->CmdListsCount; ++i) {
            const ImDrawList* list = draw_data->CmdLists[i];
            copies.emplace_back(gpu_resource_data_t{ list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx), 0, graphics_queue_idx });
        }

        const VkBufferCreateInfo buffer_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            data->currentEboSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr
        };

        data->ebo = resourceContext->CreateBuffer(&buffer_info, nullptr, copies.size(), copies.data(), resource_usage::CPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation, nullptr);

    }
    else {
        std::vector<gpu_resource_data_t> copies;

        for (int i = 0; i < draw_data->CmdListsCount; ++i) {
            const ImDrawList* list = draw_data->CmdLists[i];
            copies.emplace_back(gpu_resource_data_t{ list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx), 0, graphics_queue_idx });
        }

        resourceContext->SetBufferData(data->ebo, copies.size(), copies.data());
    }

}

void ImGuiWrapper::freeMouse(GLFWwindow * window) {
    auto& io = ImGui::GetIO();
    rendererContext->SetInputMode(GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void ImGuiWrapper::captureMouse(GLFWwindow* window) {
    auto& io = ImGui::GetIO();
    io.MousePos = ImVec2(-1, -1);
    rendererContext->SetInputMode(GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}
