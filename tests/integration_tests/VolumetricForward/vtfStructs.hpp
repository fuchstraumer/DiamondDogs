#pragma once
#ifndef VTF_MODEL_STRUCTURES_HPP
#define VTF_MODEL_STRUCTURES_HPP
#include <cstdint>
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include <vector>

struct SceneConfig_t {
    bool EnableMSAA{ true };
    uint32_t MSAA_SampleCount{ 8u };
    uint32_t MaxLights{ 2048u };
    uint32_t NumDirectionalLights{ 64u };
    uint32_t NumPointLights{ 2048u };
    uint32_t NumSpotLights{ 1024u };
    glm::vec3 LightsMinBounds{ -100.0f,-100.0f,-100.0f };
    glm::vec3 LightsMaxBounds{ 100.0f, 100.0f, 100.0f };
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

struct SceneState {
    std::vector<PointLight> PointLights;
    std::vector<SpotLight> SpotLights;
    std::vector<DirectionalLight> DirectionalLights;
};

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
} BVH_Params;

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
