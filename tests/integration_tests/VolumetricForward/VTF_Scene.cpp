#include "VTF_Scene.hpp"
#include "ResourceContext.hpp"
#include "RenderingContext.hpp"
#include "ResourceTypes.hpp"
#include "Semaphore.hpp"
#include "vkAssert.hpp"
#include "LogicalDevice.hpp"
#include "Descriptor.hpp"
#include "Swapchain.hpp"
#include "Fence.hpp"
#include "DescriptorPack.hpp"
#include "Framebuffer.hpp"
#include "CommandPool.hpp"
#include "vulkan/vulkan.h"
#include "glm/gtc/random.hpp"
#include "core/ShaderPack.hpp"
#include "DescriptorPack.hpp"
#include "Descriptor.hpp"
#include "Renderpass.hpp"
#include "DescriptorBinder.hpp"
#include "PerspectiveCamera.hpp"
#include "vtfFrameData.hpp"
#include "Material.hpp"
#include "vtfTasksAndSteps.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <experimental/filesystem>
#include <future>
#include "ImGuiWrapper.hpp"
#include <thsvs_simpler_vulkan_synchronization.h>
#include "glm/gtc/type_ptr.hpp"

const st::ShaderPack* vtfShaders{ nullptr }; 
//SceneState_t SceneLightsState{};

constexpr static float GOLDEN_RATIO = 1.6180339887498948482045f;
constexpr static float FLOAT_PI = 3.14159265359f;

const static glm::vec3 CameraPosition = glm::vec3(0.0f, 0.0f, 8.0f);

static const std::array<glm::vec3, 12> ICOSPHERE_INITIAL_VERTICES{
    glm::vec3{-GOLDEN_RATIO, 1.0f, 0.0f },
    glm::vec3{ GOLDEN_RATIO, 1.0f, 0.0f },
    glm::vec3{-GOLDEN_RATIO,-1.0f, 0.0f },
    glm::vec3{ GOLDEN_RATIO,-1.0f, 0.0f },
    glm::vec3{ 0.0f,-GOLDEN_RATIO, 1.0f },
    glm::vec3{ 0.0f, GOLDEN_RATIO, 1.0f },
    glm::vec3{ 0.0f,-GOLDEN_RATIO,-1.0f },
    glm::vec3{ 0.0f, GOLDEN_RATIO,-1.0f },
    glm::vec3{ 1.0f, 0.0f,-GOLDEN_RATIO },
    glm::vec3{ 1.0f, 0.0f, GOLDEN_RATIO },
    glm::vec3{-1.0f, 0.0f,-GOLDEN_RATIO },
    glm::vec3{-1.0f, 0.0f, GOLDEN_RATIO }
};

constexpr static std::array<uint32_t, 60> INITIAL_INDICES{
    0,11, 5,
    0, 5, 1,
    0, 1, 7,
    0, 7,10,
    0,10,11,
    5,11, 4,
    1, 5, 9,
    7, 1, 8,
    10,7, 6,
    11,10,2,
    3, 9, 4,
    3, 4, 2,
    3, 2, 6,
    3, 6, 8,
    3, 8, 9,
    4, 9, 5,
    2, 4,11,
    6, 2,10,
    8, 6, 7,
    9, 8, 1
};

constexpr static std::array<VkVertexInputBindingDescription, 1> VertexBindingDescriptions{
    VkVertexInputBindingDescription{ 0, sizeof(float) * 11, VK_VERTEX_INPUT_RATE_VERTEX }
};

struct TestIcosphereMesh
{

    void CreateMesh(const size_t detail_level)
	{

        Indices.assign(std::begin(INITIAL_INDICES), std::end(INITIAL_INDICES));

        Vertices.reserve(ICOSPHERE_INITIAL_VERTICES.size());
        for (const auto& vert : ICOSPHERE_INITIAL_VERTICES) {
            Vertices.emplace_back(vert, vert, glm::vec2(0.0f,0.0f));
        }

        for (size_t j = 0u; j < detail_level; ++j)
		{
            size_t num_triangles = Indices.size() / 3u;
            Indices.reserve(Indices.capacity() + (num_triangles * 9u));
            Vertices.reserve(Vertices.capacity() + (num_triangles * 3u));

            for (uint32_t i = 0u; i < num_triangles; ++i)
			{
                uint32_t i0 = Indices[i * 3u + 0u];
                uint32_t i1 = Indices[i * 3u + 1u];
                uint32_t i2 = Indices[i * 3u + 2u];

                uint32_t i3 = static_cast<uint32_t>(Vertices.size());
                uint32_t i4 = i3 + 1;
                uint32_t i5 = i4 + 1;

                Indices[i * 3u + 1u] = i3;
                Indices[i * 3u + 2u] = i5;

                Indices.insert(Indices.cend(), { i3, i1, i4, i5, i3, i4, i5, i4, i2 });

                glm::vec3 midpoint0 = 0.5f * (Vertices[i0].Position + Vertices[i1].Position);
                glm::vec3 midpoint1 = 0.5f * (Vertices[i1].Position + Vertices[i2].Position);
                glm::vec3 midpoint2 = 0.5f * (Vertices[i2].Position + Vertices[i0].Position);

                Vertices.emplace_back(midpoint0, midpoint0, glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint1, midpoint1, glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint2, midpoint2, glm::vec2(0.0f,0.0f));

            }
        }

        for (auto& vert : Vertices)
		{
            vert.Position = glm::normalize(vert.Position);
            vert.Normal = vert.Position;
        }

        Indices.shrink_to_fit();
        Vertices.shrink_to_fit();

        for (size_t i = 0u; i < Vertices.size(); ++i)
		{
            const glm::vec3& norm = Vertices[i].Normal;
            Vertices[i].UV.x = (glm::atan(norm.x, -norm.z) / FLOAT_PI) * 0.5f + 0.5f;
            Vertices[i].UV.y = -norm.y * 0.5f + 0.5f;
        }

        auto add_vertex_w_uv = [this](const size_t i, const glm::vec2& uv)
		{
            const uint32_t& idx = Indices[i];
            Vertices.emplace_back(Vertices[idx].Position, Vertices[idx].Normal, uv);
            Indices[i] = static_cast<uint32_t>(Vertices.size() - 1u);
        };

        const size_t num_triangles = Indices.size() / 3;

        for (size_t i = 0u; i < num_triangles; ++i)
		{
            const glm::vec2& uv0 = Vertices[Indices[i * 3u]].UV;
            const glm::vec2& uv1 = Vertices[Indices[i * 3u + 1u]].UV;
            const glm::vec2& uv2 = Vertices[Indices[i * 3u + 2u]].UV;
            const float d1 = uv1.x - uv0.x;
            const float d2 = uv2.x - uv0.x;

            if (std::abs(d1) > 0.5f && std::abs(d2) > 0.5f)
			{
                add_vertex_w_uv(i * 3, uv0 + glm::vec2((d1 > 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
            else if (std::abs(d1) > 0.5f)
			{
                add_vertex_w_uv(i * 3 + 1, uv1 + glm::vec2((d1 < 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
            else if (std::abs(d2) > 0.5f)
			{
                add_vertex_w_uv(i * 3 + 2, uv2 + glm::vec2((d2 < 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
        }

		Vertices.shrink_to_fit();
		Indices.shrink_to_fit();

		const uint32_t graphics_queue_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Graphics;

        const VkBufferCreateInfo vbo_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            sizeof(vertex_t) * Vertices.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0u,
            nullptr
        };

        const gpu_resource_data_t vbo_data{
            Vertices.data(),
            vbo_info.size,
            0u, graphics_queue_idx
        };

        auto& rsrc_context = ResourceContext::Get();
        VBO = rsrc_context.CreateBuffer(&vbo_info, nullptr, 1, &vbo_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, "IcosphereTesterVBO");

        const VkBufferCreateInfo ebo_info{
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            sizeof(uint32_t) * Indices.size(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0u,
            nullptr
        };

        const gpu_resource_data_t ebo_data{
            Indices.data(),
            ebo_info.size,
            0u, graphics_queue_idx
        };

        EBO = rsrc_context.CreateBuffer(&ebo_info, nullptr, 1, &ebo_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, "IcosphereTesterEBO");

    }

    void Render(const objRenderStateData& state)
	{
        constexpr static VkDebugUtilsLabelEXT debug_label{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            nullptr,
            "RenderTestIcosphere",
            { 30.0f / 255.0f, 180.0f / 255.0f, 95.0f / 255.0f, 1.0f }
        };

        constexpr static VkDeviceSize offsets_dummy[1]{ 0u };
        const VkBuffer buffers[1]{ (VkBuffer)VBO->Handle };

        switch (state.type) {
        case render_type::Opaque:
            [[fallthrough]];
        case render_type::OpaqueAndTransparent: // no transparent geometry for this test
            if constexpr (VTF_VALIDATION_ENABLED) 
            {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(state.cmd, &debug_label);
            }
            vkCmdSetViewport(state.cmd, 0u, 1u, &viewport);
            vkCmdSetScissor(state.cmd, 0u, 1u, &scissor);
            state.binder->Bind(state.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdBindIndexBuffer(state.cmd, (VkBuffer)EBO->Handle, 0u, VK_INDEX_TYPE_UINT32);
            vkCmdBindVertexBuffers(state.cmd, 0, 1, buffers, offsets_dummy);
            vkCmdDrawIndexed(state.cmd, static_cast<uint32_t>(Indices.size()), 1u, 0u, 0, 0u);
            if constexpr (VTF_VALIDATION_ENABLED) {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(state.cmd);
            }
            break;
        default:
            break; // break for rest, e.g transparents
        };

    }

    // just renders the same mesh with a greatly shrunken model matrix
    void RenderDebugLights(const objRenderStateData& state)
    {
        auto& lights = SceneLightsState();

        constexpr static VkDebugUtilsLabelEXT debug_label{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            nullptr,
            "RenderDebugLights",
            { 66.0f / 255.0f, 210.0f / 255.0f, 95.0f / 255.0f, 1.0f }
        };
        constexpr static VkDeviceSize offsets_dummy[1]{ 0u };
        const VkBuffer buffers[1]{ (VkBuffer)VBO->Handle };

        switch (state.type) 
        {
        case render_type::Transparent:
            if constexpr (VTF_VALIDATION_ENABLED) {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(state.cmd, &debug_label);
            }
            vkCmdBindPipeline(state.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLightsPipeline);
            state.binder->BindResource("GlobalResources", "matrices", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, lightsMatrices);
            state.binder->Update();
            state.binder->Bind(state.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdBindIndexBuffer(state.cmd, (VkBuffer)EBO->Handle, 0u, VK_INDEX_TYPE_UINT32);
            vkCmdBindVertexBuffers(state.cmd, 0, 1, buffers, offsets_dummy);
            vkCmdDrawIndexed(state.cmd, static_cast<uint32_t>(Indices.size()), static_cast<uint32_t>(lights.PointLights.size()), 0u, 0, 0u);
            if constexpr (VTF_VALIDATION_ENABLED) {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(state.cmd);
            }
            break;
        default:
            break; // break for rest, e.g transparents
        };
    }

    VulkanResource* VBO{ nullptr };
    VulkanResource* EBO{ nullptr };
    std::vector<uint32_t> Indices;
    std::vector<vertex_t> Vertices;
	VkViewport viewport;
	VkRect2D scissor;
    VulkanResource* lightsMatrices;
    VkPipeline debugLightsPipeline;
    //Material icosphereMtl;

};

struct ObjModel
{
    ObjModel(const char* fname);

    VulkanResource* VBO{ nullptr };
    VulkanResource* EBO{ nullptr };
    VulkanResource* AlbedoTexture{ nullptr };
    VulkanResource* AmbientOcclusionTexture{ nullptr };

};

glm::vec3 HSV_to_RGB(float H, float S, float V)
{
    float C = V * S;
    float m = V - C;
    float H2 = H / 60.0f;
    float X = C * (1.0f - std::fabsf(std::fmodf(H2, 2.0f) - 1.0f));
    glm::vec3 RGB;
    switch (int(H2)) {
    case 0:
        RGB = { C, X, 0.0f };
        break;
    case 1:
        RGB = { X, C, 0.0f };
        break;
    case 2:
        RGB = { 0.0f, C, X };
        break;
    case 3:
        RGB = { 0.0f, X, C };
        break;
    case 4:
        RGB = { X, 0.0f, C };
        break;
    case 5:
        RGB = { C, 0.0f, X };
        break;
    default:
        throw std::domain_error("Found invalid value for H2 when converting HSV to RGB");
    }

    return RGB + m;
}

static std::vector<glm::u8vec4> GenerateColors(uint32_t num_lights)
{
    std::vector<glm::vec4> colors(num_lights);
    for (auto& color : colors) {
        color = glm::vec4(HSV_to_RGB(glm::linearRand(0.0f, 360.0f), glm::linearRand(0.0f, 1.0f), 1.0f), 1.0f);
    }
	std::vector<glm::u8vec4> results(num_lights);
	for (size_t i = 0; i < num_lights; ++i)
	{
		results[i] = glm::u8vec4{
			static_cast<glm::u8>(colors[i].x * 255.0f),
			static_cast<glm::u8>(colors[i].y * 255.0f),
			static_cast<glm::u8>(colors[i].z * 255.0f),
			static_cast<glm::u8>(255)
		};
	}
    return results;
}

template<typename LightType>
static LightType GenerateLight(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color);

template<>
PointLight GenerateLight<PointLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color)
{
    PointLight result{};
    result.positionWS = position_ws;
    result.color = color;
    result.range = range;
    result.intensity = glm::linearRand(0.6f, 1.0f);
    return result;
}

template<>
SpotLight GenerateLight<SpotLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color)
{
    SpotLight result{};
    result.positionWS = position_ws;
    result.directionWS = direction_ws;
    result.spotlightAngle = spot_angle;
    result.range = range;
    result.color = color;
    result.intensity = glm::linearRand(0.5f, 1.0f);
    return result;
}

template<>
DirectionalLight GenerateLight<DirectionalLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color)
{
    DirectionalLight result{};
    result.directionWS = direction_ws;
    result.color = color;
    result.intensity = glm::linearRand(0.5f, 1.0f);
    return result;
}

template<typename LightType>
static std::vector<LightType> GenerateLights(uint32_t num_lights)
{
    std::vector<LightType> lights;
    lights.resize(num_lights, LightType{});

    for (auto& light : lights) {
        glm::vec4 position_ws = glm::vec4{ glm::linearRand(SceneConfig.LightsMinBounds, SceneConfig.LightsMaxBounds), 1.0f };
        glm::vec4 direction_ws = glm::vec4{ glm::sphericalRand(1.0f), 0.0f };
        float spot_angle = glm::linearRand(SceneConfig.MinSpotAngle, SceneConfig.MaxSpotAngle);
        float range = glm::linearRand(SceneConfig.MinRange, SceneConfig.MaxRange);
        glm::vec3 color = HSV_to_RGB(glm::linearRand(0.0f, 360.0f), glm::linearRand(0.0f, 1.0f), 1.0f);
        light = GenerateLight<LightType>(position_ws, direction_ws, spot_angle, range, color);
    }

    return lights;
}

void GenerateLights()
{
    SceneLightsState().PointLights = GenerateLights<PointLight>(SceneConfig.NumPointLights);
    SceneLightsState().SpotLights = GenerateLights<SpotLight>(SceneConfig.NumSpotLights);
    SceneLightsState().DirectionalLights = GenerateLights<DirectionalLight>(SceneConfig.NumDirectionalLights);
	uint32_t x, y, z;
	CalculateGridDims(x, y, z);
	SceneLightsState().ClusterColors = GenerateColors(x * y * z);
}

VTF_Scene& VTF_Scene::Get()
{
    static VTF_Scene scene;
    return scene;
}


void VTF_Scene::AddObjectRenderFn(const vtf_frame_data_t::obj_render_fn_t& function)
{
    for (size_t i = 0; i < frames.size(); ++i)
    {
        frames[i]->renderFns.emplace_back(function);
    }
}

void VTF_Scene::Construct(RequiredVprObjects objects, void * user_data)
{
    vprObjects = objects;
    vtfShaders = reinterpret_cast<const st::ShaderPack*>(user_data);

	auto& camera = PerspectiveCamera::Get();
    camera.Initialize(glm::radians(70.0f), 0.1f, 5000.0f, CameraPosition, glm::vec3(0.0f) - CameraPosition);

    CreateShaders(vtfShaders);
    GenerateLights();
    // now create frames
    auto* swapchain = RenderingContext::Get().Swapchain();
    const uint32_t img_count = swapchain->ImageCount();
    frames.reserve(img_count); // this should avoid invalidating pointers (god i hope)
	frameSingleExecComputeWorkDone.resize(img_count, false);

    ImGui::CreateContext();
       
    size_t frameVar = activeFrame;
    auto imguiRender = [frameVar](const objRenderStateData& state)
    {
        if (state.type == render_type::GUI)
        {
            auto& imgui_wrapper = ImGuiWrapper::GetImGuiWrapper();
            imgui_wrapper.DrawFrame(frameVar, state.cmd);
        }
    };

    vtf_frame_data_t::obj_render_fn_t imgui_render_fn = std::bind(imguiRender, std::placeholders::_1);

    for (uint32_t i = 0; i < img_count; ++i) {
        frames.emplace_back(std::make_unique<vtf_frame_data_t>());
        frames[i]->renderFns.emplace_back(imgui_render_fn);
        FullFrameSetup(frames[i].get()); // we used to thread this, but since it's our baseline data for all rendering i've elected not to anymore
    }

    ImGuiWrapper::GetImGuiWrapper().Construct(frames[0]->renderPasses.at("DrawPass")->vkHandle());
}

void VTF_Scene::Destroy()
{
    for (auto& frame : frames) {
        DestroyFrame(*frame);
    }
}

constexpr VkCommandBufferBeginInfo base_info{
	VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	nullptr,
	VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
};

void VTF_Scene::updateGlobalUBOs()
{
	auto& extent = vprObjects.swapchain->Extent();
	auto& curr_frame = *frames[activeFrame];
	auto& camera = PerspectiveCamera::Get();
	auto& resource_context = ResourceContext::Get();
    ImGuiWrapper::GetImGuiWrapper().NewFrame();
    PerspectiveCamera::Get().UpdateMouseMovement();
    CameraController::UpdateMovement();

	curr_frame.Matrices.projection = camera.ProjectionMatrix();
    curr_frame.Matrices.projection[1][1] *= -1.0f;
	curr_frame.Matrices.view = camera.ViewMatrix();
	curr_frame.Matrices.inverseView = glm::inverse(curr_frame.Matrices.view);
	curr_frame.Matrices.model = glm::scale(glm::mat4(1.0f), glm::vec3(5.0f));
    curr_frame.Matrices.modelView = curr_frame.Matrices.view * curr_frame.Matrices.model;
    curr_frame.Matrices.modelViewProjection = curr_frame.Matrices.projection * curr_frame.Matrices.modelView;
    curr_frame.Matrices.inverseTransposeModelView = glm::inverse(glm::transpose(curr_frame.Matrices.modelView));
    curr_frame.Matrices.inverseTransposeModel = glm::inverse(glm::transpose(curr_frame.Matrices.model));
	VulkanResource* matrices_rsrc = curr_frame.rsrcMap.at("matrices");
	const gpu_resource_data_t matrices_update{
		&curr_frame.Matrices, sizeof(curr_frame.Matrices), 0u, VK_QUEUE_FAMILY_IGNORED
	};
	resource_context.SetBufferData(matrices_rsrc, 1u, &matrices_update);

    curr_frame.Matrices.model = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
    VulkanResource* lights_matrices = curr_frame.rsrcMap.at("debugLightsMatrices");
    resource_context.SetBufferData(lights_matrices, 1u, &matrices_update);

	curr_frame.Globals.depthRange.x = camera.NearPlane();
	curr_frame.Globals.depthRange.y = camera.FarPlane();
	curr_frame.Globals.frame++;
    curr_frame.Globals.viewPosition = glm::vec4(camera.Position(), 1.0f);
	curr_frame.Globals.windowSize.x = static_cast<float>(extent.width);
	curr_frame.Globals.windowSize.y = static_cast<float>(extent.height);
	VulkanResource* globals_rsrc = curr_frame.rsrcMap.at("globals");
	const gpu_resource_data_t globals_update{
		&curr_frame.Globals, sizeof(curr_frame.Globals), 0u, VK_QUEUE_FAMILY_IGNORED
	};
	resource_context.SetBufferData(globals_rsrc, 1u, &globals_update);
	
	VulkanResource* cluster_data_rsrc = curr_frame.rsrcMap.at("ClusterData");
	const gpu_resource_data_t cluster_update{
		&curr_frame.ClusterData, sizeof(curr_frame.ClusterData), 0u, VK_QUEUE_FAMILY_IGNORED
	};
	resource_context.SetBufferData(cluster_data_rsrc, 1u, &cluster_update);

    UpdateFrameResources(curr_frame);

    // make sure we flush stuff before rendering
    resource_context.Update();

}

void VTF_Scene::update()
{

	updateGlobalUBOs();
    // compute updates
	vtf_frame_data_t& curr_frame = *frames[activeFrame];
	if (curr_frame.computeClusterAABBs)
	{
		UpdateClusterGrid(curr_frame);
        curr_frame.computeClusterAABBs = false;
	}
	ComputeUpdate(curr_frame);
}

void VTF_Scene::recordCommands()
{
    static bool render_debug_clusters{ false };
    static bool update_unique_clusters{ true };
    static bool use_optimized_lighting{ true };

    ImGui::Begin("VTF Debug");
        ImGui::Checkbox("Render Debug Clusters", &render_debug_clusters);
        if (ImGui::Button("Regenerate Lights"))
        {
            GenerateLights();
        }
        ImGui::Checkbox("Update Unique Clusters", &update_unique_clusters);
        ImGui::Checkbox("Optimized Lighting", &use_optimized_lighting);
        for (auto& frame : frames)
        {
            frame->renderDebugClusters = render_debug_clusters;
            frame->updateUniqueClusters = update_unique_clusters;
            frame->optimizedLighting = use_optimized_lighting;
        }
    ImGui::End();
    ImGuiWrapper::GetImGuiWrapper().EndImGuiFrame();
	RenderVtf(*frames[activeFrame]);
	SubmitGraphicsWork(*frames[activeFrame]);
}

void VTF_Scene::draw() {
    // primary draws
}

void VTF_Scene::endFrame() 
{
    frames[activeFrame]->descriptorPack->EndFrame();
	activeFrame = (activeFrame + 1u) % frames.size();
    //const std::string memoryStatsOutputName = std::string("MemoryStatusFrame_") + std::to_string(activeFrame) + std::string(".json");
    //ResourceContext::Get().WriteMemoryStatsFile(memoryStatsOutputName.c_str());
}

void VTF_Scene::acquireImage() 
{
	// handled internally with the frames
}

void VTF_Scene::present() {

	vtf_frame_data_t& curr_frame = *frames[activeFrame];

	VkResult present_results[1]{ VK_SUCCESS };

	const VkPresentInfoKHR present_info{
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		nullptr,
		1u,
		&curr_frame.semaphores.at("RenderComplete")->vkHandle(),
		1u,
		&vprObjects.swapchain->vkHandle(),
		&curr_frame.imageIdx,
		present_results
	};

	VkResult result = vkQueuePresentKHR(vprObjects.device->GraphicsQueue(), &present_info);
	VkAssert(result);

}

VulkanResource* VTF_Scene::loadTexture(const char* file_path_str, const char* image_name) {
    auto& rsrc_context = ResourceContext::Get();
	const uint32_t graphics_queue_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Graphics;
    stbi_uc* pixels = nullptr;
    int x{ 0 };
    int y{ 0 };
    int channels{ 0 };

    pixels = stbi_load(file_path_str, &x, &y, &channels, 4);
    size_t img_footprint = channels * x * y * sizeof(stbi_uc);
    const gpu_image_resource_data_t img_rsrc_data{
        pixels,
        img_footprint,
        size_t(x),
        size_t(y),
        0u,
        1u,
        0u,
		graphics_queue_idx
    };

    VkFormat img_format = VK_FORMAT_UNDEFINED;
    if (channels == 4) {
        img_format = VK_FORMAT_R8G8B8A8_UNORM;
    }
    else if (channels == 3) {
        img_format = VK_FORMAT_R8G8B8_UNORM;
    }
    else if (channels == 2) {
        img_format = VK_FORMAT_R8G8_UNORM;
    }
    else if (channels == 1) {
        img_format = VK_FORMAT_R8_UNORM;
    }
    else {
        throw std::runtime_error("Bad format");
    }

    const auto* device = RenderingContext::Get().Device();
    const uint32_t transfer_idx = device->QueueFamilyIndices().Transfer;
    const uint32_t graphics_idx = device->QueueFamilyIndices().Graphics;
    const VkSharingMode sharing_mode = (transfer_idx != graphics_idx) ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    const std::array<uint32_t, 2> queue_family_indices{ graphics_idx, transfer_idx };

    const VkImageCreateInfo img_create_info{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0,
        VK_IMAGE_TYPE_2D,
        img_format,
        VkExtent3D{ uint32_t(x), uint32_t(y), 1u },
        1u,
        1u,
        VK_SAMPLE_COUNT_1_BIT,
        vprObjects.device->GetFormatTiling(img_format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
        VK_IMAGE_USAGE_SAMPLED_BIT,
        sharing_mode,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? static_cast<uint32_t>(queue_family_indices.size()) : 0u,
        sharing_mode == VK_SHARING_MODE_CONCURRENT ? queue_family_indices.data() : nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    const VkImageViewCreateInfo view_create_info {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        VK_NULL_HANDLE,
        VK_IMAGE_VIEW_TYPE_2D,
        img_format,
        VkComponentMapping{ VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A },
        VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    VulkanResource* result = rsrc_context.CreateImage(&img_create_info, &view_create_info, 1, &img_rsrc_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, (void*)image_name);
    // we do it like this so we can safely free "pixels" before returning
    stbi_image_free(pixels);
    return result;
}

void VTF_Scene::createIcosphereTester() {
    namespace fs = std::experimental::filesystem;

    icosphereTester = std::make_unique<TestIcosphereMesh>();
    icosphereTester->CreateMesh(4u);

	auto current_dims = vprObjects.swapchain->Extent();
	icosphereTester->viewport.width = static_cast<float>(current_dims.width);
	icosphereTester->viewport.height = static_cast<float>(current_dims.height);
	icosphereTester->viewport.x = 0;
	icosphereTester->viewport.y = 0;
	icosphereTester->viewport.minDepth = 0.0f;
	icosphereTester->viewport.maxDepth = 1.0f;
	icosphereTester->scissor.extent = current_dims;
	icosphereTester->scissor.offset.x = 0;
	icosphereTester->scissor.offset.y = 0;

    const static fs::path prefix_path_to_textures{ "../../../../assets/textures/harsh_bricks" };
    if (!fs::exists(prefix_path_to_textures)) {
        throw std::runtime_error("Error in prefix path to harsh brick textures!");
    }

    const fs::path albedo_path = prefix_path_to_textures / fs::path("harshbricks-albedo.png");
    const std::string albedo_str = albedo_path.string();
    const fs::path ao_path = prefix_path_to_textures / fs::path("harshbricks-ao2.png");
    const std::string ao_str = ao_path.string();
    const fs::path metallic_path = prefix_path_to_textures / fs::path("harshbricks-metalness.png");
    const std::string metallic_str = metallic_path.string();
    const fs::path normal_path = prefix_path_to_textures / fs::path("harshbricks-normal.png");
    const std::string normal_str = normal_path.string();
    const fs::path roughness_path = prefix_path_to_textures / fs::path("harshbricks-roughness.png");
    const std::string roughness_str = roughness_path.string();

    tinyobj_opt::material_t material;
    material.ambient_texname = ao_str;
    material.diffuse_texname = albedo_str;
    material.roughness_texname = roughness_str;
    material.metallic_texname = metallic_str;
    material.normal_texname = normal_str;
    material.roughness = 5.0f;
    material.metallic = 0.1f;

    //icosphereTester->icosphereMtl = std::move(Material(material, prefix_path_to_textures.string().c_str()));
    
}
