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
#include "DescriptorBinder.hpp"
#include "PerspectiveCamera.hpp"
#include "vtfFrameData.hpp"
#include "material/MaterialParameters.hpp"
#include "vtfTasksAndSteps.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include <experimental/filesystem>
#include <future>

const st::ShaderPack* vtfShaders{ nullptr }; 
//SceneState_t SceneLightsState{};

struct vertex_t {
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

constexpr static std::array<VkVertexInputBindingDescription, 1> VertexBindingDescriptions {
    VkVertexInputBindingDescription{ 0, sizeof(float) * 11, VK_VERTEX_INPUT_RATE_VERTEX }
};

struct TestIcosphereMesh {

    void CreateMesh(const size_t detail_level) {

        Indices.assign(std::begin(INITIAL_INDICES), std::end(INITIAL_INDICES));
        Vertices.reserve(ICOSPHERE_INITIAL_VERTICES.size());
        for (const auto& vert : ICOSPHERE_INITIAL_VERTICES) {
            Vertices.emplace_back(vert, vert, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
        }

        for (size_t j = 0; j < detail_level; ++j) {
            size_t num_triangles = Indices.size() / 3;
            Indices.reserve(Indices.capacity() + (num_triangles * 9));
            Vertices.reserve(Vertices.capacity() + (num_triangles * 3));
            for (uint32_t i = 0; i < num_triangles; ++i) {
                uint32_t i0 = Indices[i * 3 + 0];
                uint32_t i1 = Indices[i * 3 + 1];
                uint32_t i2 = Indices[i * 3 + 2];

                uint32_t i3 = static_cast<uint32_t>(Vertices.size());
                uint32_t i4 = i3 + 1;
                uint32_t i5 = i4 + 1;

                Indices[i * 3 + 1] = i3;
                Indices[i * 3 + 2] = i5;

                Indices.insert(Indices.cend(), { i3, i1, i4, i5, i3, i4, i5, i4, i2 });

                glm::vec3 midpoint0 = 0.5f * (Vertices[i0].Position + Vertices[i1].Position);
                glm::vec3 midpoint1 = 0.5f * (Vertices[i1].Position + Vertices[i2].Position);
                glm::vec3 midpoint2 = 0.5f * (Vertices[i2].Position + Vertices[i0].Position);

                Vertices.emplace_back(midpoint0, midpoint0, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint1, midpoint1, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
                Vertices.emplace_back(midpoint2, midpoint2, glm::vec3(0.0f,0.0f,0.0f), glm::vec2(0.0f,0.0f));
            }
        }

        for (auto& vert : Vertices) {
            vert.Position = glm::normalize(vert.Position);
            vert.Normal = vert.Position;
        }

        Indices.shrink_to_fit();
        Vertices.shrink_to_fit();

        for (size_t i = 0; i < Vertices.size(); ++i) {
            const glm::vec3& norm = Vertices[i].Normal;
            Vertices[i].UV.x = (glm::atan(norm.x, -norm.z) / FLOAT_PI) * 0.5f + 0.5f;
            Vertices[i].UV.y = -norm.y * 0.5f + 0.5f;
        }

        auto add_vertex_w_uv = [this](const size_t i, const glm::vec2& uv) {
            const uint32_t& idx = Indices[i];
            Vertices.emplace_back(Vertices[idx].Position, Vertices[idx].Normal, glm::vec3(0.0f, 0.0f, 0.0f), uv);
            Indices[i] = static_cast<uint32_t>(Vertices.size());
        };

        const size_t num_triangles = Indices.size() / 3;
        for (size_t i = 0; i < num_triangles; ++i) {
            const glm::vec2& uv0 = Vertices[Indices[i * 3]].UV;
            const glm::vec2& uv1 = Vertices[Indices[i * 3 + 1]].UV;
            const glm::vec2& uv2 = Vertices[Indices[i * 3 + 2]].UV;
            const float d1 = uv1.x - uv0.x;
            const float d2 = uv2.x - uv0.x;
            if (std::abs(d1) > 0.5f && std::abs(d2) > 0.5f) {
                add_vertex_w_uv(i * 3, uv0 + glm::vec2((d1 > 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
            else if (std::abs(d1) > 0.5f) {
                add_vertex_w_uv(i * 3 + 1, uv1 + glm::vec2((d1 < 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
            else if (std::abs(d2) > 0.5f) {
                add_vertex_w_uv(i * 3 + 2, uv2 + glm::vec2((d2 < 0.0f) ? 1.0f : -1.0f, 0.0f));
            }
        }

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
            0u, 0u, 0u
        };

        auto& rsrc_context = ResourceContext::Get();
        VBO = rsrc_context.CreateBuffer(&vbo_info, nullptr, 1, &vbo_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation, nullptr);

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
            0u, 0u, 0u
        };

        EBO = rsrc_context.CreateBuffer(&ebo_info, nullptr, 1, &ebo_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation, nullptr);

    }

    void BindTextures(Descriptor& descr) {

        auto& rsrc_context = ResourceContext::Get();
        const size_t albedo_loc = descr.BindingLocation("AlbedoMap");
        const size_t normal_loc = descr.BindingLocation("NormalMap");
        const size_t ao_loc = descr.BindingLocation("AmbientOcclusionMap");
        const size_t metallic_loc = descr.BindingLocation("MetallicMap");
        const size_t roughness_loc = descr.BindingLocation("RoughnessMap");
        const size_t height_loc = descr.BindingLocation("HeightMap");
        const size_t params_loc = descr.BindingLocation("MaterialParameters");
        
        descr.BindResourceToIdx(albedo_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, AlbedoTexture);
        descr.BindResourceToIdx(normal_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, NormalMap);
        descr.BindResourceToIdx(ao_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, AmbientOcclusionTexture);
        descr.BindResourceToIdx(metallic_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MetallicMap);
        descr.BindResourceToIdx(roughness_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, RoughnessMap);
        descr.BindResourceToIdx(height_loc, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, HeightMap);

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

        const gpu_resource_data_t material_params_data {
            &MaterialParams,
            sizeof(MaterialParameters),
            0u, 0u, 0u
        };

        vkMaterialParams = rsrc_context.CreateBuffer(&material_params_info, nullptr, 1u, &material_params_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation, nullptr);
        descr.BindResourceToIdx(params_loc, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vkMaterialParams);

    }

    void Render(VkCommandBuffer cmd, DescriptorBinder& binder, vtf_frame_data_t::render_type render_type) {

        constexpr static VkDeviceSize offsets_dummy[1]{ 0u };
        const VkBuffer buffers[1]{ (VkBuffer)VBO->Handle };

        switch (render_type) {
        case vtf_frame_data_t::render_type::Opaque:
            [[fallthrough]];
        case vtf_frame_data_t::render_type::OpaqueAndTransparent: // no transparent geometry for this test
            binder.BindSingle(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, "Material");
            vkCmdBindIndexBuffer(cmd, (VkBuffer)EBO->Handle, 0u, VK_INDEX_TYPE_UINT32);
            vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets_dummy);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(Indices.size()), 1u, 0u, 0, 0u);
            break;
        default:
            break; // break for rest
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

};

glm::vec3 HSV_to_RGB(float H, float S, float V) {
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

static std::vector<glm::vec4> GenerateColors(uint32_t num_lights) {
    std::vector<glm::vec4> colors(num_lights);
    for (auto& color : colors) {
        color = glm::vec4(HSV_to_RGB(glm::linearRand(0.0f, 360.0f), glm::linearRand(0.0f, 1.0f), 1.0f), 1.0f);
    }
    return colors;
}

template<typename LightType>
static LightType GenerateLight(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color);

template<>
static PointLight GenerateLight<PointLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color) {
    PointLight result{};
    result.positionWS = position_ws;
    result.color = color;
    result.range = range;
    return result;
}

template<>
static SpotLight GenerateLight<SpotLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color) {
    SpotLight result{};
    result.positionWS = position_ws;
    result.directionWS = direction_ws;
    result.spotlightAngle = spot_angle;
    result.range = range;
    result.color = color;
    return result;
}

template<>
static DirectionalLight GenerateLight<DirectionalLight>(const glm::vec4& position_ws, const glm::vec4& direction_ws, float spot_angle, float range, const glm::vec3& color) {
    DirectionalLight result{};
    result.directionWS = direction_ws;
    result.color = color;
    return result;
}

template<typename LightType>
static std::vector<LightType> GenerateLights(uint32_t num_lights) {
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

void GenerateLights() {
    SceneLightsState().PointLights = std::move(GenerateLights<PointLight>(SceneConfig.NumPointLights));
    SceneLightsState().SpotLights = std::move(GenerateLights<SpotLight>(SceneConfig.NumSpotLights));
    SceneLightsState().DirectionalLights = std::move(GenerateLights<DirectionalLight>(SceneConfig.NumDirectionalLights));
}

VTF_Scene& VTF_Scene::Get() {
    static VTF_Scene scene;
    return scene;
}

void VTF_Scene::Construct(RequiredVprObjects objects, void * user_data) {
    vprObjects = objects;
    vtfShaders = reinterpret_cast<const st::ShaderPack*>(user_data);
    CreateShaders(vtfShaders);
    GenerateLights();
    // now create frames
    std::vector<std::future<void>> setupFutures;
    auto* swapchain = RenderingContext::Get().Swapchain();
    const uint32_t img_count = swapchain->ImageCount();
    frames.reserve(img_count); // this should avoid invalidating pointers (god i hope)
	frameSingleExecComputeWorkDone.resize(img_count, false);

    for (uint32_t i = 0; i < img_count; ++i) {
        frames.emplace_back(std::make_unique<vtf_frame_data_t>());
        setupFutures.emplace_back(std::async(std::launch::async, FullFrameSetup, frames[i].get()));
    }

    for (auto& fut : setupFutures) {
        fut.get(); // even if we block for one the rest should still be running
    }


	auto& resource_context = ResourceContext::Get();
	resource_context.WriteMemoryStatsFile("MemoryStats.json");
    std::cerr << "Setup Complete\n";
	// need to ensure this is enabled currently, so we can see debug stats
}

void VTF_Scene::Destroy() {
    for (auto& frame : frames) {

    }
}

constexpr VkCommandBufferBeginInfo base_info{
	VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	nullptr,
	VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
};

void VTF_Scene::update() {
    // compute updates
	vtf_frame_data_t& curr_frame = *frames[activeFrame];
	vkBeginCommandBuffer(curr_frame.computePool->GetCmdBuffer(0u), &base_info);
	ComputeUpdateLights(curr_frame);
	ComputeReduceLights(curr_frame);
	ComputeMortonCodes(curr_frame);
	SortMortonCodes(curr_frame);
	BuildLightBVH(curr_frame);
	if (!frameSingleExecComputeWorkDone[activeFrame])
	{
		UpdateClusterGrid(curr_frame);
		ComputeClusterAABBs(curr_frame);
	}
	vkEndCommandBuffer(curr_frame.computePool->GetCmdBuffer(0u));
	SubmitComputeWork(curr_frame);
}

void VTF_Scene::recordCommands() 
{
	RenderVtf(*frames[activeFrame]);
	SubmitGraphicsWork(*frames[activeFrame]);
}

void VTF_Scene::draw() {
    // primary draws
}

void VTF_Scene::endFrame() {
	activeFrame = (activeFrame + 1u) % frames.size();
	auto& ctxt = ResourceContext::Get();
	for (auto* resource : frames[activeFrame]->transientResources)
	{
		ctxt.DestroyResource(resource);
	}
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
		0u,
		VK_NULL_HANDLE,
		&currentBuffer,
		present_results
	};

	VkResult result = vkQueuePresentKHR(vprObjects.device->GraphicsQueue(), &present_info);
	VkAssert(result);

}

VulkanResource* VTF_Scene::loadTexture(const char* file_path_str) {
    auto& rsrc_context = ResourceContext::Get();
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
        0u
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

    VulkanResource* result = rsrc_context.CreateImage(&img_create_info, &view_create_info, 1, &img_rsrc_data, resource_usage::GPU_ONLY, ResourceCreateMemoryStrategyMinFragmentation, nullptr);
    // we do it like this so we can safely free "pixels" before returning
    rsrc_context.Update();
    stbi_image_free(pixels);
    return result;
}

void VTF_Scene::createIcosphereTester() {
    namespace fs = std::experimental::filesystem;

    icosphereTester = std::make_unique<TestIcosphereMesh>();
    icosphereTester->CreateMesh(5u);

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

    icosphereTester->AlbedoTexture = loadTexture(albedo_str.c_str());
    icosphereTester->AmbientOcclusionTexture = loadTexture(ao_str.c_str());
    icosphereTester->NormalMap = loadTexture(normal_str.c_str());
    icosphereTester->MetallicMap = loadTexture(metallic_str.c_str());
    icosphereTester->RoughnessMap = loadTexture(roughness_str.c_str());
    
}
