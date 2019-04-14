#pragma once
#ifndef VTF_MODEL_STRUCTURES_HPP
#define VTF_MODEL_STRUCTURES_HPP
#include <cstdint>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include <vector>
#include <array>
#include <vulkan/vulkan.h>
#include "glm/gtc/type_precision.hpp"
#include <string>
#include <unordered_map>

struct SceneConfig_t {
    bool EnableMSAA{ true };
    VkSampleCountFlagBits MSAA_SampleCount{ VK_SAMPLE_COUNT_8_BIT };
    float AnisotropyAmount{ 16.0f };
    uint32_t MaxLights{ 2048u };
    uint32_t NumPointLights{ 2048u };
    uint32_t NumSpotLights{ 512u };
    uint32_t NumDirectionalLights{ 1u };
    glm::vec3 LightsMinBounds{ -30.0f,-30.0f,-30.0f };
    glm::vec3 LightsMaxBounds{ 30.0f, 30.0f, 30.0f };
    float MinSpotAngle{ -60.0f };
    float MaxSpotAngle{ 60.0f };
    float MinRange{ 1.0f };
    float MaxRange{ 32.0f };
};

inline static SceneConfig_t SceneConfig;

struct alignas(16) PointLight {
    glm::vec4 positionWS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 positionVS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float range{ 100.0f };
    float intensity{ 1.0f };
    uint32_t enabled{ 1u };
    uint32_t selected{ 0u };
    float padding{ 0.0f };
};

struct alignas(16) SpotLight {
    glm::vec4 positionWS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 positionVS{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 directionWS{ 0.0f, 0.0f, 1.0f, 0.0f };
    glm::vec4 directionVS{ 0.0f, 0.0f,-1.0f, 0.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float spotlightAngle{ 45.0f };
    float range{ 100.0f };
    float intensity{ 1.0f };
    uint32_t enabled{ 1u };
    uint32_t selected{ 0u };
};

struct alignas(16) DirectionalLight {
    glm::vec4 directionWS{};
    glm::vec4 directionVS{};
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    uint32_t enabled{ 1u };
    uint32_t selected{ 0u };
    float padding[2]{ 0.0f, 0.0f };
};

struct SceneState_t {
    SceneState_t() = default;
    SceneState_t(const SceneState_t&) = delete;
    SceneState_t& operator=(const SceneState_t&) = delete;
    static SceneState_t& Get() noexcept {
        static SceneState_t state;
        return state;
    }
    std::vector<PointLight> PointLights;
    std::vector<SpotLight> SpotLights;
    std::vector<DirectionalLight> DirectionalLights;
	std::vector<glm::u8vec4> ClusterColors;
};

static SceneState_t& SceneLightsState() {
    return SceneState_t::Get();
}

struct alignas(16) Matrices_t {
    glm::mat4 model{ glm::identity<glm::mat4>() };
    glm::mat4 view{ glm::identity<glm::mat4>() };
    glm::mat4 inverseView{ glm::identity<glm::mat4>() };
    glm::mat4 projection{ glm::identity<glm::mat4>() };
    glm::mat4 modelView{ glm::identity<glm::mat4>() };
    glm::mat4 modelViewProjection{ glm::identity<glm::mat4>() };
    glm::mat4 inverseTransposeModel{ glm::identity<glm::mat4>() };
    glm::mat4 inverseTransposeModelView{ glm::identity<glm::mat4>() };
};

struct alignas(16) GlobalsData {
    glm::vec4 viewPosition{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec2 mousePosition{ 0.0f, 0.0f };
    glm::vec2 windowSize{ 0.0f, 0.0f };
    glm::vec2 depthRange{ 0.0f, 0.0f };
    uint32_t frame{ 0 };
    float exposure{ 0.0f };
    float gamma{ 0.0f };
    float brightness{ 0.0f };
};

struct alignas(16) ClusterData_t {
    glm::uvec3 GridDim;
    float ViewNear;
    glm::uvec2 ScreenSize;
    float NearK;
    float LogGridDimY;
};

struct alignas(4) DispatchParams_t {
    glm::uvec3 NumThreadGroups{};
    uint32_t Padding0{ 0u };
    glm::uvec3 NumThreads{};
    uint32_t Padding1{ 0u };
};

struct alignas(4) SortParams {
    uint32_t NumElements;
    uint32_t ChunkSize;
};

struct alignas(16) Frustum {
    glm::vec4 Planes[4];
};

struct alignas(16) AABB {
    glm::vec4 Min;
    glm::vec4 Max;
};

constexpr static uint32_t DEFAULT_MAX_LIGHTS = 2048u;

struct alignas(4) LightCountsData {
    uint32_t NumPointLights{ DEFAULT_MAX_LIGHTS };
    uint32_t NumSpotLights{ DEFAULT_MAX_LIGHTS };
    uint32_t NumDirectionalLights{ 4u };
};

struct alignas(16) Cone {
    glm::vec3 T;
    float h;
    glm::vec3 d;
    float r;
};

struct alignas(4) BVH_Params_t {
    uint32_t PointLightLevels{ 0u };
    uint32_t SpotLightLevels{ 0u };
    uint32_t ChildLevel{ 0u };
    uint32_t Padding{ 0u };
};

struct QueryPool
{
	QueryPool(VkDevice _device, uint32_t nanoseconds_period) : device(_device), devicePeriod(nanoseconds_period)
	{
		data.fill(0u);

		constexpr static VkQueryPoolCreateInfo create_info{
			VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			nullptr,
			0,
			VK_QUERY_TYPE_TIMESTAMP,
			NUM_QUERIES,
			0
		};

		vkCreateQueryPool(device, &create_info, nullptr, &pool);
	}

	~QueryPool()
	{
		vkDestroyQueryPool(device, pool, nullptr);
	}

	QueryPool(const QueryPool&) = delete;
	QueryPool& operator=(const QueryPool&) = delete;

	void WriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits pipeline_stage, std::string name)
	{
		if (!unexecuted && (lastQueryIdx == 0u))
		{
			vkCmdResetQueryPool(cmd, pool, 0u, NUM_QUERIES);
		}

		vkCmdWriteTimestamp(cmd, pipeline_stage, pool, lastQueryIdx);
		timeStampNames[lastQueryIdx] = std::move(name);
		++lastQueryIdx;
	}

	std::unordered_map<std::string, float> GetTimestamps()
	{
		// This should ONLY be called at the end of a frame!
		vkGetQueryPoolResults(device, pool, 0, lastQueryIdx, data.size() * sizeof(uint32_t), data.data(), sizeof(uint32_t), VK_QUERY_RESULT_WAIT_BIT);

		std::unordered_map<std::string, float> results;
		results.reserve(lastQueryIdx - 1);

		// Have to skip first to get results
		for (uint32_t i = 1; i < lastQueryIdx + 1; i += 2)
		{
			uint32_t timestamp_diff = data[i] - data[i - 1];
			if (timestamp_diff < devicePeriod)
			{
				// just emplace one ns
				results.emplace(timeStampNames[(i - 1) / 2], 1.0f);
			}
			else
			{
				// whatever we lose doing this division is just too bad
				float timestamp_ns = static_cast<float>(timestamp_diff) / static_cast<float>(devicePeriod);
				// divide by 1e-6f to get ms
				results.emplace(timeStampNames[(i - 1) / 2], timestamp_ns * 1e-6f);
			}
		}

		lastQueryIdx = 0u;
		unexecuted = false;
		return results;
	}

private:

	friend struct ScopedRenderSection;
	bool unexecuted{ true }; // used to avoid resetting the first time
	uint32_t lastQueryIdx{ 0u };
	VkDevice device{ VK_NULL_HANDLE };
	VkQueryPool pool{ VK_NULL_HANDLE };
	constexpr static size_t NUM_QUERIES{ 512u };
	std::array<uint32_t, NUM_QUERIES> data;
	std::array<std::string, NUM_QUERIES / 2> timeStampNames;
	uint32_t devicePeriod{ 0u };
};

struct ScopedRenderSection
{

	ScopedRenderSection(QueryPool& _pool, VkCommandBuffer _cmd, VkPipelineStageFlagBits start, VkPipelineStageFlagBits _end, std::string name) : pool(_pool), cmd(_cmd), end(_end)
	{
		pool.timeStampNames[pool.lastQueryIdx / 2u] = std::move(name);
		vkCmdWriteTimestamp(cmd, start, pool.pool, pool.lastQueryIdx);
		++pool.lastQueryIdx;
	}

	~ScopedRenderSection()
	{
		vkCmdWriteTimestamp(cmd, end, pool.pool, pool.lastQueryIdx);
		++pool.lastQueryIdx;
		assert(pool.lastQueryIdx % 2u == 0u);
	}

private:
	QueryPool& pool;
	VkCommandBuffer cmd;
	VkPipelineStageFlagBits end;
};

struct ComputePipelineState {

    ComputePipelineState() = default;
    ComputePipelineState(VkDevice dev) : Device(dev) {}

    VkDevice Device{ VK_NULL_HANDLE };
    VkPipeline Handle{ VK_NULL_HANDLE };

    void Destroy() {
        if (Handle != VK_NULL_HANDLE) {
            vkDestroyPipeline(Device, Handle, nullptr);
            Handle = VK_NULL_HANDLE;
        }
    }

};

#endif // !VTF_MODEL_STRUCTURES_HPP
