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
#include "material/MaterialParameters.hpp"
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

struct vertex_t
{
    vertex_t(glm::vec3 p, glm::vec3 n, glm::vec3 t, glm::vec2 uv) : Position(std::move(p)), Normal(std::move(n)), Tangent(std::move(t)),
        UV(std::move(uv)) {}
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec3 Tangent;
    glm::vec2 UV;
};

constexpr static float GOLDEN_RATIO = 1.6180339887498948482045f;
constexpr static float FLOAT_PI = 3.14159265359f;

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
            Vertices.emplace_back(vert, vert, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
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

                Vertices.emplace_back(midpoint0, midpoint0, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint1, midpoint1, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint2, midpoint2, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));

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
            Vertices.emplace_back(Vertices[idx].Position, Vertices[idx].Normal, glm::vec3(0.0f, 0.0f, 0.0f), uv);
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

		// any values left that are also read from texture use this UBOs quantity
		// as a base "multiplier", in effect. setting it to 1.0 should leave it
		// at the intended power I believe
		MaterialParams.ambientOcclusion = 1.0f;

		constexpr static VkBufferCreateInfo material_params_info{
			VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			nullptr,
			0,
			sizeof(MaterialParameters),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_SHARING_MODE_EXCLUSIVE,
			0u,
			nullptr
		};

		const gpu_resource_data_t material_params_data{
			&MaterialParams,
			sizeof(MaterialParameters),
			0u, graphics_queue_idx
		};

		vkMaterialParams = rsrc_context.CreateBuffer(&material_params_info, nullptr, 1u, &material_params_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation | ResourceCreateUserDataAsString, 
			"IcosphereTesterMaterialParams");
    }

    void BindTextures(Descriptor* descr)
    {

        auto& rsrc_context = ResourceContext::Get();
        const size_t albedo_loc = descr->BindingLocation("AlbedoMap");
        const size_t normal_loc = descr->BindingLocation("NormalMap");
        const size_t ao_loc = descr->BindingLocation("AmbientOcclusionMap");
        const size_t metallic_loc = descr->BindingLocation("MetallicMap");
        const size_t roughness_loc = descr->BindingLocation("RoughnessMap");
        const size_t params_loc = descr->BindingLocation("MaterialParameters");
        
        descr->BindResourceToIdx(albedo_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, AlbedoTexture);
        descr->BindResourceToIdx(normal_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, NormalMap);
        descr->BindResourceToIdx(ao_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, AmbientOcclusionTexture);
        descr->BindResourceToIdx(metallic_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MetallicMap);
        descr->BindResourceToIdx(roughness_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, RoughnessMap);
        descr->BindResourceToIdx(params_loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vkMaterialParams);

    }

    void Render(VkCommandBuffer cmd, DescriptorBinder* binder, vtf_frame_data_t::render_type render_type)
	{
		static bool first_render{ true };

        constexpr static VkDebugUtilsLabelEXT debug_label{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
            nullptr,
            "RenderTestIcosphere",
            { 30.0f / 255.0f, 180.0f / 255.0f, 95.0f / 255.0f, 1.0f }
        };

		/*
			Transition images
			Only need to do this once across threads
		*/
        auto first_render_mem_fn = [&](const VkCommandBuffer cmd)
        {
            const uint32_t transfer_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Transfer;
            const uint32_t graphics_idx = RenderingContext::Get().Device()->QueueFamilyIndices().Graphics;

            const ThsvsAccessType starting_access_type[1]{
                THSVS_ACCESS_TRANSFER_WRITE
            };

            const ThsvsAccessType final_access_types[2]{
                THSVS_ACCESS_VERTEX_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER,
                THSVS_ACCESS_FRAGMENT_SHADER_READ_SAMPLED_IMAGE_OR_UNIFORM_TEXEL_BUFFER
            };

            const ThsvsImageBarrier barrier_base{
                1u,
                starting_access_type,
                2u,
                final_access_types,
                THSVS_IMAGE_LAYOUT_OPTIMAL,
                THSVS_IMAGE_LAYOUT_OPTIMAL,
                VK_FALSE,
                transfer_idx,
                graphics_idx,
                VK_NULL_HANDLE,
                VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
            };

            std::array<ThsvsImageBarrier, 5> image_barriers;
            image_barriers.fill(barrier_base);
            image_barriers[0].image = (VkImage)AlbedoTexture->Handle;
            image_barriers[1].image = (VkImage)NormalMap->Handle;
            image_barriers[2].image = (VkImage)AmbientOcclusionTexture->Handle;
            image_barriers[3].image = (VkImage)MetallicMap->Handle;
            image_barriers[4].image = (VkImage)RoughnessMap->Handle;

            thsvsCmdPipelineBarrier(cmd, nullptr, 0u, nullptr, static_cast<uint32_t>(image_barriers.size()), image_barriers.data());

        };

        constexpr static VkDeviceSize offsets_dummy[1]{ 0u };
        const VkBuffer buffers[1]{ (VkBuffer)VBO->Handle };

        switch (render_type) {
        case vtf_frame_data_t::render_type::Opaque:
            [[fallthrough]];
        case vtf_frame_data_t::render_type::OpaqueAndTransparent: // no transparent geometry for this test
            if constexpr (VTF_VALIDATION_ENABLED) 
            {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
            }
            if (first_render)
            {
                first_render_mem_fn(cmd);
            }
            vkCmdSetViewport(cmd, 0u, 1u, &viewport);
            vkCmdSetScissor(cmd, 0u, 1u, &scissor);
            binder->Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdBindIndexBuffer(cmd, (VkBuffer)EBO->Handle, 0u, VK_INDEX_TYPE_UINT32);
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets_dummy);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(Indices.size()), 1u, 0u, 0, 0u);
            if constexpr (VTF_VALIDATION_ENABLED) {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(cmd);
            }
            break;
        default:
            break; // break for rest, e.g transparents
        };

		first_render = false;

    }

    // just renders the same mesh with a greatly shrunken model matrix
    void RenderDebugLights(VkCommandBuffer cmd, DescriptorBinder* binder, vtf_frame_data_t::render_type render_type)
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

        switch (render_type) 
        {
        case vtf_frame_data_t::render_type::Transparent:
            if constexpr (VTF_VALIDATION_ENABLED) {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdBeginDebugUtilsLabel(cmd, &debug_label);
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLightsPipeline);
            binder->BindResourceToIdx("GlobalResources", "matrices", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, lightsMatrices);
            binder->Update();
            binder->Bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdBindIndexBuffer(cmd, (VkBuffer)EBO->Handle, 0u, VK_INDEX_TYPE_UINT32);
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets_dummy);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(Indices.size()), static_cast<uint32_t>(lights.PointLights.size()), 0u, 0, 0u);
            if constexpr (VTF_VALIDATION_ENABLED) {
                RenderingContext::Get().Device()->DebugUtilsHandler().vkCmdEndDebugUtilsLabel(cmd);
            }
            break;
        default:
            break; // break for rest, e.g transparents
        };
    }

    VulkanResource* VBO{ nullptr };
    VulkanResource* EBO{ nullptr };
    VulkanResource* AlbedoTexture{ nullptr };
    VulkanResource* AmbientOcclusionTexture{ nullptr };
    VulkanResource* HeightMap{ nullptr };
    VulkanResource* NormalMap{ nullptr };
    VulkanResource* MetallicMap{ nullptr };
    VulkanResource* RoughnessMap{ nullptr };
    VulkanResource* vkMaterialParams{ nullptr };
    std::vector<uint32_t> Indices;
    std::vector<vertex_t> Vertices;
    MaterialParameters MaterialParams;
	VkViewport viewport;
	VkRect2D scissor;
    VulkanResource* lightsMatrices;
    VkPipeline debugLightsPipeline;

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
static PointLight GenerateLight<PointLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color)
{
    PointLight result{};
    result.positionWS = position_ws;
    result.color = color;
    result.range = range;
    return result;
}

template<>
static SpotLight GenerateLight<SpotLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color)
{
    SpotLight result{};
    result.positionWS = position_ws;
    result.directionWS = direction_ws;
    result.spotlightAngle = spot_angle;
    result.range = range;
    result.color = color;
    return result;
}

template<>
static DirectionalLight GenerateLight<DirectionalLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color)
{
    DirectionalLight result{};
    result.directionWS = direction_ws;
    result.color = color;
    return result;
}

template<typename LightType>
static std::vector<LightType> GenerateLights(uint32_t num_lights)
{
    std::vector<LightType> lights(num_lights);

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
    SceneLightsState().PointLights = std::move(GenerateLights<PointLight>(SceneConfig.NumPointLights));
    SceneLightsState().SpotLights = std::move(GenerateLights<SpotLight>(SceneConfig.NumSpotLights));
    SceneLightsState().DirectionalLights = std::move(GenerateLights<DirectionalLight>(SceneConfig.NumDirectionalLights));
	uint32_t x, y, z;
	CalculateGridDims(x, y, z);
	SceneLightsState().ClusterColors = std::move(GenerateColors(x * y * z));
}

VTF_Scene& VTF_Scene::Get()
{
    static VTF_Scene scene;
    return scene;
}


void VTF_Scene::Construct(RequiredVprObjects objects, void * user_data)
{
    vprObjects = objects;
    vtfShaders = reinterpret_cast<const st::ShaderPack*>(user_data);

	auto& camera = PerspectiveCamera::Get();
    camera.Initialize(glm::radians(70.0f), 0.01f, 3000.0f, CameraPosition, glm::vec3(0.0f) - CameraPosition);

    CreateShaders(vtfShaders);
	createIcosphereTester();
    GenerateLights();
    // now create frames
    std::vector<std::future<void>> setupFutures;
    auto* swapchain = RenderingContext::Get().Swapchain();
    const uint32_t img_count = swapchain->ImageCount();
    frames.reserve(img_count); // this should avoid invalidating pointers (god i hope)
	frameSingleExecComputeWorkDone.resize(img_count, false);

    ImGui::CreateContext();

    auto imguiRender = [&](VkCommandBuffer cmd, DescriptorBinder * descriptor, vtf_frame_data_t::render_type type)
    {
        if (type == vtf_frame_data_t::render_type::GUI)
        {
            auto& imgui_wrapper = ImGuiWrapper::GetImGuiWrapper();
            imgui_wrapper.DrawFrame(activeFrame, cmd);
        }
    };

	vtf_frame_data_t::obj_render_fn_t render_fn = std::bind(&TestIcosphereMesh::Render, icosphereTester.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    vtf_frame_data_t::obj_render_fn_t lights_render_fn = std::bind(&TestIcosphereMesh::RenderDebugLights, icosphereTester.get(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    vtf_frame_data_t::obj_render_fn_t imgui_render_fn = std::bind(imguiRender, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	vtf_frame_data_t::binder_fn_t binder_fn = std::bind(&TestIcosphereMesh::BindTextures, icosphereTester.get(), std::placeholders::_1);

    for (uint32_t i = 0; i < img_count; ++i) {
        frames.emplace_back(std::make_unique<vtf_frame_data_t>());
		frames[i]->renderFns.emplace_back(render_fn);
        frames[i]->renderFns.emplace_back(lights_render_fn);
        frames[i]->renderFns.emplace_back(imgui_render_fn);
		frames[i]->bindFns.emplace_back(binder_fn);
        setupFutures.emplace_back(std::async(std::launch::async, FullFrameSetup, frames[i].get()));
    }

    for (auto& fut : setupFutures) {
        fut.get(); // even if we block for one the rest should still be running
    }

	for (uint32_t i = 0; i < img_count; ++i)
	{
		auto* descr = frames[i]->descriptorPack->RetrieveDescriptor("Material");
		for (uint32_t j = 0; j < frames[i]->bindFns.size(); ++j)
		{
			frames[i]->bindFns[j](descr);
		}
	}

    ImGuiWrapper::GetImGuiWrapper().Construct(frames[0]->renderPasses.at("DrawPass")->vkHandle());

    std::cerr << "Setup Complete\n";
}

void VTF_Scene::Destroy()
{
    for (auto& frame : frames) {

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

    curr_frame.Matrices.model = glm::scale(glm::mat4(1.0f), glm::vec3(0.02f));
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

	resource_context.Update();
    icosphereTester->debugLightsPipeline = curr_frame.graphicsPipelines.at("DebugPointLights")->vkHandle();
    icosphereTester->lightsMatrices = curr_frame.rsrcMap.at("debugLightsMatrices");
}

void VTF_Scene::update()
{
	updateGlobalUBOs();
    // compute updates
	vtf_frame_data_t& curr_frame = *frames[activeFrame];
	if (curr_frame.updateUniqueClusters)
	{
		UpdateClusterGrid(curr_frame);
	}
	ComputeUpdate(curr_frame);
}

void VTF_Scene::recordCommands()
{
    static bool render_debug_clusters{ true };
    static bool update_unique_clusters{ true };
    static bool use_optimized_lighting{ false };

    ImGui::Begin("VTF Debug");
        ImGui::Checkbox("Render Debug Clusters", &render_debug_clusters);
        frames[activeFrame]->renderDebugClusters = render_debug_clusters;
        if (ImGui::Button("Regenerate Lights"))
        {
            GenerateLights();
        }
        ImGui::Checkbox("Update Unique Clusters", &update_unique_clusters);
        frames[activeFrame]->updateUniqueClusters = update_unique_clusters;
        ImGui::Checkbox("Optimized Lighting", &use_optimized_lighting);
        frames[activeFrame]->optimizedLighting = use_optimized_lighting;
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
	activeFrame = (activeFrame + 1u) % frames.size();;
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
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
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
    icosphereTester->CreateMesh(3u);

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

    icosphereTester->AlbedoTexture = loadTexture(albedo_str.c_str(), "IcosphereTesterAlbedoTexture");
    icosphereTester->AmbientOcclusionTexture = loadTexture(ao_str.c_str(), "IcosphereTesterAmbientOcclusionTexture");
    icosphereTester->NormalMap = loadTexture(normal_str.c_str(), "IcosphereTesterNormalMap");
    icosphereTester->MetallicMap = loadTexture(metallic_str.c_str(), "IcosphereTesterMetallicMap");
    icosphereTester->RoughnessMap = loadTexture(roughness_str.c_str(), "IcosphereTesterRoughnessMap");
    
}
