
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "OceanEnvironment.hpp"
#include "math/Math.hpp"
#include "OceanSimulation.hpp"
#include "../ShaderObjectState.hpp"

#include "ColorSpaces.hpp"
#include "ExrLoader.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "GpuTimers.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "VariantKey.hpp"

#if __has_include("SourcePaths.hpp")
#include "SourcePaths.hpp"
#define HAS_SOURCE_PATHS 1
#else
#define HAS_SOURCE_PATHS 0
#endif
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <iostream>
#include <vector>

#include "vulkan/vulkan_core.h"

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr float k_Pi = std::numbers::pi_v<float>;

    constexpr std::string_view k_ModuleName = "OceanEnvironment";
    constexpr std::string_view k_PreethamEntry = "OceanEnvPreethamCS";
    constexpr std::string_view k_DownsampleEntry = "OceanEnvDownsampleMipCS";
    constexpr std::string_view k_SkyVsEntry = "OceanSkyVS";
    constexpr std::string_view k_SkyFsEntry = "OceanSkyFS";

    // Set debug utility name for Vulkan objects
    void SetDebugName(rhi::Device& device, VkObjectType type, uint64_t handle, const char* name)
    {
        if (handle == 0u)
        {
            return;
        }

        const auto& debugFns = device.GetDebugUtilFns();
        if (debugFns.vkSetDebugUtilsObjectName != nullptr)
        {
            VkDebugUtilsObjectNameInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            info.objectType = type;
            info.objectHandle = handle;
            info.pObjectName = name;
            debugFns.vkSetDebugUtilsObjectName(device.Handle().As<VkDevice>(), &info);
        }

        (void)rhi::RhiSystem::SetObjectName(type, handle, name);
    }

    std::filesystem::path ResolveHdriPath(ocean::OceanSkyPreset preset)
    {
        const std::string_view rel = ocean::SkyPresetRelativePath(preset);
        if (rel.empty())
        {
            return {};
        }

#if HAS_SOURCE_PATHS
        const std::filesystem::path assetPath = DiamondDogs::GetAssetPath(std::filesystem::path{ rel });
        if (std::filesystem::exists(assetPath))
        {
            return assetPath;
        }
#endif

        const std::filesystem::path direct{ rel };
        if (std::filesystem::exists(direct))
        {
            return direct;
        }

        const std::array<std::filesystem::path, 5> searchPrefixes{
            std::filesystem::path("assets"),
            std::filesystem::path("../assets"),
            std::filesystem::path("../../assets"),
            std::filesystem::path("../../../assets"),
            std::filesystem::path("C:/SoftwareDev/DiamondDogs/assets")
        };

        for (const auto& prefix : searchPrefixes)
        {
            const std::filesystem::path candidate = prefix / direct;
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        return direct;
    }

    // Evaluates Perez formula for sky luminance
    struct PerezCoeffs
    {
        float A, B, C, D, E;
    };

    float EvaluatePerez(PerezCoeffs c, float cosTheta, float gamma, float cosGamma)
    {
        const float num1 = 1.0f + c.A * std::exp(c.B / std::max(0.01f, cosTheta));
        const float num2 = 1.0f + c.C * std::exp(c.D * gamma) + c.E * cosGamma * cosGamma;
        return num1 * num2;
    }

    void ComputePreethamL2AndLight(
        float turbidity,
        const std::array<float, 3>& sun_dir,
        float exposure,
        ocean::SphericalHarmonicsL2& out_sh,
        std::array<float, 3>& out_dominant_dir,
        std::array<float, 3>& out_dominant_radiance)
    {
        const math::Vector sunVec{ sun_dir[0], sun_dir[1], sun_dir[2], 0.0f };
        const float lenSq = sunVec.LengthSq<3>();
        const math::Float3 sFloat = (lenSq > 1e-10f)
            ? static_cast<math::Float3>(math::FromVector(sunVec.Normalize<3>()))
            : math::Float3{ 0.0f, 1.0f, 0.0f };
        const std::array<float, 3> s{ sFloat.x, sFloat.y, sFloat.z };

        out_dominant_dir = s;

        const float cosSunZenith = std::clamp(s[1], 0.001f, 1.0f);
        const float thetaS = std::acos(cosSunZenith);

        const float T = std::clamp(turbidity, 1.0f, 10.0f);
        const float T2 = T * T;

        PerezCoeffs cy{ 0.17872f * T - 1.46303f, -0.35540f * T + 0.42749f, -0.02266f * T + 5.32505f, 0.12064f * T - 2.57705f, -0.06696f * T + 0.37027f };
        PerezCoeffs cx{ -0.01925f * T - 0.25922f, -0.06651f * T + 0.00081f, -0.00041f * T + 0.21247f, -0.06409f * T - 0.89887f, -0.00325f * T + 0.04517f };
        PerezCoeffs cz{ -0.01669f * T - 0.26078f, -0.08864f * T + 0.00921f, -0.00516f * T + 0.21023f, -0.06029f * T - 1.65369f, -0.00373f * T + 0.05291f };

        // Conversion from photometric units (cd/m^2, lux) to scene-referred linear AP1 units,
        // calibrated against DaySkyHDRI071B (~3.8 peak direct sun irradiance, ~0.8-1.5 ambient sky SH).
        constexpr float k_PhotometricToScene = 1.0f / 22000.0f;
        constexpr float k_SolarSolidAngle = 6.8e-5f;

        const float chi = (4.0f / 9.0f - T / 120.0f) * (k_Pi - 2.0f * thetaS);
        float Yz = (4.0453f * T - 4.9710f) * std::tan(chi) - 0.2155f * T + 2.4192f;
        Yz = std::max(0.0f, Yz) * 1000.0f * k_PhotometricToScene;

        const float thetaS2 = thetaS * thetaS;
        const float thetaS3 = thetaS2 * thetaS;

        const float xz = (0.00166f * T2 - 0.02903f * T + 0.11693f) * thetaS3
                       + (-0.00375f * T2 + 0.06377f * T - 0.21196f) * thetaS2
                       + (0.00209f * T2 - 0.03202f * T + 0.06052f) * thetaS
                       + (0.00394f * T + 0.25886f);

        const float yz = (0.00275f * T2 - 0.04214f * T + 0.15346f) * thetaS3
                       + (-0.00610f * T2 + 0.08970f * T - 0.26756f) * thetaS2
                       + (0.00317f * T2 - 0.04517f * T + 0.06670f) * thetaS
                       + (0.00516f * T + 0.26688f);

        // Preetham direct solar radiance / irradiance
        if (s[1] > 0.0f)
        {
            const float airMass = 1.0f / std::max(0.02f, s[1]);
            const float solarTrans = std::exp(-0.1f * T * airMass);
            const float sunLuminance = 1.3e9f * solarTrans * exposure;
            const float sunIrradiance = sunLuminance * k_SolarSolidAngle * k_PhotometricToScene;
            out_dominant_radiance = { sunIrradiance * 1.0f, sunIrradiance * 0.95f, sunIrradiance * 0.88f };
        }
        else
        {
            out_dominant_radiance = { 0.0f, 0.0f, 0.0f };
        }

        // Integrate SH on a 64x32 grid
        constexpr uint32_t gridW = 64u;
        constexpr uint32_t gridH = 32u;
        const float dOmegaFactor = (2.0f * k_Pi * k_Pi) / static_cast<float>(gridW * gridH);

        const float denomY = std::max(1e-6f, EvaluatePerez(cy, 1.0f, thetaS, cosSunZenith));
        const float denomX = std::max(1e-6f, EvaluatePerez(cx, 1.0f, thetaS, cosSunZenith));
        const float denomZ = std::max(1e-6f, EvaluatePerez(cz, 1.0f, thetaS, cosSunZenith));

        // XYZ to AP1 linear matrix
        constexpr math::Float3x3 k_XyzToAp1{
            1.64102338f,  -0.324803294f, -0.236424695f,
            -0.663662859f, 1.61533159f,   0.0167563477f,
            0.0117218943f, -0.008284442f, 0.988394859f
        };

        std::array<std::array<double, 3>, 9> shAccum{};

        for (uint32_t j = 0; j < gridH; ++j)
        {
            const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(gridH);
            const float theta = v * k_Pi;
            const auto [sinTheta, cosTheta] = math::SinCos(theta);
            const float dOmega = sinTheta * dOmegaFactor;

            for (uint32_t i = 0; i < gridW; ++i)
            {
                const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(gridW);
                const float phi = (u - 0.5f) * 2.0f * k_Pi;
                const auto [sinPhi, cosPhi] = math::SinCos(phi);

                const float dirX = sinTheta * cosPhi;
                const float dirY = cosTheta;
                const float dirZ = sinTheta * sinPhi;

                const float cosView = std::max(0.001f, dirY);
                const float cosGamma = std::clamp(dirX * s[0] + dirY * s[1] + dirZ * s[2], -1.0f, 1.0f);
                const float gamma = std::acos(cosGamma);

                const float fY = EvaluatePerez(cy, cosView, gamma, cosGamma) / denomY;
                const float fx = EvaluatePerez(cx, cosView, gamma, cosGamma) / denomX;
                const float fy = EvaluatePerez(cz, cosView, gamma, cosGamma) / denomZ;

                const float Y = Yz * fY;
                const float x = xz * fx;
                const float y = std::max(1e-4f, yz * fy);

                const float X = (x / y) * Y;
                const float Z = std::max(0.0f, (1.0f - x - y) / y * Y);

                const math::Float3 rgb = k_XyzToAp1 * math::Float3{ X, Y, Z };
                float r = rgb.x;
                float g = rgb.y;
                float b = rgb.z;

                r = std::max(0.0f, r) * exposure;
                g = std::max(0.0f, g) * exposure;
                b = std::max(0.0f, b) * exposure;

                if (dirY < 0.0f)
                {
                    const float groundFade = 0.15f * std::exp(dirY * 8.0f);
                    r *= groundFade;
                    g *= groundFade;
                    b *= groundFade;
                }

                const float Y0 = 0.28209479f;
                const float Y1 = 0.48860251f * dirY;
                const float Y2 = 0.48860251f * dirZ;
                const float Y3 = 0.48860251f * dirX;
                const float Y4 = 1.0925484f * dirX * dirY;
                const float Y5 = 1.0925484f * dirY * dirZ;
                const float Y6 = 0.31539156f * (3.0f * dirZ * dirZ - 1.0f);
                const float Y7 = 1.0925484f * dirX * dirZ;
                const float Y8 = 0.5462742f * (dirX * dirX - dirY * dirY);

                const std::array<float, 9> Y_basis{ Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7, Y8 };
                for (size_t k = 0u; k < 9u; ++k)
                {
                    shAccum[k][0] += static_cast<double>(r * Y_basis[k] * dOmega);
                    shAccum[k][1] += static_cast<double>(g * Y_basis[k] * dOmega);
                    shAccum[k][2] += static_cast<double>(b * Y_basis[k] * dOmega);
                }
            }
        }

        const double A0 = 3.14159265358979323846;
        const double A1 = 2.09439510239319549231;
        const double A2 = 0.78539816339744830962;
        const std::array<double, 9> A_factors{ A0, A1, A1, A1, A2, A2, A2, A2, A2 };

        for (size_t k = 0u; k < 9u; ++k)
        {
            out_sh[k][0] = static_cast<float>(shAccum[k][0] * A_factors[k]);
            out_sh[k][1] = static_cast<float>(shAccum[k][1] * A_factors[k]);
            out_sh[k][2] = static_cast<float>(shAccum[k][2] * A_factors[k]);
            out_sh[k][3] = 1.0f;
        }
    }
}

namespace ocean
{
    struct OceanEnvironmentImpl
    {
        ~OceanEnvironmentImpl();

        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        const rhi::ShaderLibrary* Library{ nullptr };
        OceanEnvironmentSettings Settings{};

        GraphicsResource SkyDomeImage;
        GraphicsResource SkyDomeSampler;

        std::vector<VkImageView> MipViews;
        uint32_t ExtentW{ 0u };
        uint32_t ExtentH{ 0u };
        uint32_t MipLevels{ 0u };
        bool NeedsMipGen{ false };

        SphericalHarmonicsL2 SphericalHarmonics{};
        std::array<float, 3> DominantLightDirection{ 0.0f, 1.0f, 0.0f };
        std::array<float, 3> DominantLightRadiance{ 0.0f, 0.0f, 0.0f };

        std::unique_ptr<rhi::ManifestProgram> PreethamProgram;
        std::unique_ptr<rhi::ManifestProgram> DownsampleProgram;
        std::unique_ptr<rhi::ManifestProgram> SkyProgram;

        const lodestone::ManifestIndex* Index{ nullptr };
        lodestone::VariantKey PreethamVariant{};
        lodestone::VariantKey DownsampleVariant{};
        lodestone::VariantKey SkyVariant{};

        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        VkDescriptorSet PreethamDescriptorSet{ VK_NULL_HANDLE };
        std::vector<VkDescriptorSet> DownsampleSets;
        VkDescriptorSet SkyDescriptorSet{ VK_NULL_HANDLE };

        void DestroyGpuResources();
        [[nodiscard]] bool LoadPreset(OceanSkyPreset preset);
        [[nodiscard]] bool SetupDescriptorSets();
    };

    OceanEnvironmentImpl::~OceanEnvironmentImpl()
    {
        DestroyGpuResources();
    }

    void OceanEnvironmentImpl::DestroyGpuResources()
    {
        if (Device == nullptr)
        {
            return;
        }

        const VkDevice vkDev = Device->Handle().As<VkDevice>();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vkDev, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
            PreethamDescriptorSet = VK_NULL_HANDLE;
            DownsampleSets.clear();
            SkyDescriptorSet = VK_NULL_HANDLE;
        }

        for (VkImageView view : MipViews)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(vkDev, view, nullptr);
            }
        }
        MipViews.clear();

        if (SkyDomeSampler)
        {
            (void)Resources->DestroyResource(SkyDomeSampler)->WaitForCompletion(k_TimeoutNs);
            SkyDomeSampler = GraphicsResource{};
        }

        if (SkyDomeImage)
        {
            (void)Resources->DestroyResource(SkyDomeImage)->WaitForCompletion(k_TimeoutNs);
            SkyDomeImage = GraphicsResource{};
        }
    }

    bool OceanEnvironmentImpl::SetupDescriptorSets()
    {
        if (Device == nullptr || !SkyDomeImage || !SkyDomeSampler || MipViews.empty())
        {
            return false;
        }

        const VkDevice vkDev = Device->Handle().As<VkDevice>();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vkDev, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
            PreethamDescriptorSet = VK_NULL_HANDLE;
            DownsampleSets.clear();
            SkyDescriptorSet = VK_NULL_HANDLE;
        }

        const uint32_t downsamplePairs = MipLevels > 1u ? (MipLevels - 1u) : 0u;
        const uint32_t totalSets = 2u + downsamplePairs;

        const std::array<VkDescriptorPoolSize, 3> poolSizes{
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, totalSets * 2u },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, totalSets * 2u },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, totalSets }
        };

        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = totalSets,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data()
        };

        if (vkCreateDescriptorPool(vkDev, &poolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
        {
            return false;
        }
        SetDebugName(*Device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(DescriptorPool), "Ocean_SkyDomeDescriptorPool");

        // Sky background set
        if (SkyProgram != nullptr && !SkyProgram->SetLayouts().empty() && Index != nullptr)
        {
            const VkDescriptorSetLayout skyLayout = SkyProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
            const VkDescriptorSetAllocateInfo allocInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = DescriptorPool,
                .descriptorSetCount = 1u,
                .pSetLayouts = &skyLayout
            };
            if (vkAllocateDescriptorSets(vkDev, &allocInfo, &SkyDescriptorSet) == VK_SUCCESS)
            {
                SetDebugName(*Device, VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(SkyDescriptorSet), "Ocean_SkyDomeSkyBackgroundSet");

                const BoundResources skyBound{
                    { "SkyDomeTexture", { SkyDomeImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
                    { "SkyDomeSampler", { SkyDomeSampler } }
                };
                const std::array<std::string_view, 2> skyEntries{ k_SkyVsEntry, k_SkyFsEntry };
                if (auto err = WriteManifestDescriptors(vkDev, SkyDescriptorSet, *Index, SkyVariant, skyEntries, skyBound); err.has_value())
                {
                    std::cerr << "OceanEnvironment sky descriptors: " << *err << "\n";
                }
            }
        }

        // Downsample sets (one per mip pair)
        if (DownsampleProgram != nullptr && !DownsampleProgram->SetLayouts().empty() && Index != nullptr)
        {
            const VkDescriptorSetLayout downLayout = DownsampleProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
            DownsampleSets.resize(downsamplePairs, VK_NULL_HANDLE);
            const std::array<std::string_view, 1> downsampleEntry{ k_DownsampleEntry };

            for (uint32_t m = 0; m < downsamplePairs; ++m)
            {
                const VkDescriptorSetAllocateInfo allocInfo{
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = DescriptorPool,
                    .descriptorSetCount = 1u,
                    .pSetLayouts = &downLayout
                };
                if (vkAllocateDescriptorSets(vkDev, &allocInfo, &DownsampleSets[m]) == VK_SUCCESS)
                {
                    const std::string name = std::format("Ocean_SkyDomeDownsampleMip{}Set", m);
                    SetDebugName(*Device, VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(DownsampleSets[m]), name.c_str());

                    GraphicsResource srcRes{};
                    srcRes.VkViewHandle = reinterpret_cast<uint64_t>(MipViews[m]);
                    GraphicsResource dstRes{};
                    dstRes.VkViewHandle = reinterpret_cast<uint64_t>(MipViews[m + 1u]);

                    const BoundResources downBound{
                        { "SrcMip", { srcRes, VK_IMAGE_LAYOUT_GENERAL } },
                        { "DstMip", { dstRes } }
                    };
                    if (auto err = WriteManifestDescriptors(vkDev, DownsampleSets[m], *Index, DownsampleVariant, downsampleEntry, downBound); err.has_value())
                    {
                        std::cerr << "OceanEnvironment downsample descriptors: " << *err << "\n";
                    }
                }
            }
        }

        // Preetham set
        if (PreethamProgram != nullptr && !PreethamProgram->SetLayouts().empty() && !MipViews.empty() && Index != nullptr)
        {
            const VkDescriptorSetLayout preethamLayout = PreethamProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
            const VkDescriptorSetAllocateInfo allocInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = DescriptorPool,
                .descriptorSetCount = 1u,
                .pSetLayouts = &preethamLayout
            };
            if (vkAllocateDescriptorSets(vkDev, &allocInfo, &PreethamDescriptorSet) == VK_SUCCESS)
            {
                SetDebugName(*Device, VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(PreethamDescriptorSet), "Ocean_SkyDomePreethamSet");

                GraphicsResource mip0Res{};
                mip0Res.VkViewHandle = reinterpret_cast<uint64_t>(MipViews[0]);

                const BoundResources preethamBound{
                    { "OutPreethamMip0", { mip0Res } }
                };
                const std::array<std::string_view, 1> preethamEntry{ k_PreethamEntry };
                if (auto err = WriteManifestDescriptors(vkDev, PreethamDescriptorSet, *Index, PreethamVariant, preethamEntry, preethamBound); err.has_value())
                {
                    std::cerr << "OceanEnvironment preetham descriptors: " << *err << "\n";
                }
            }
        }

        return true;
    }

    bool OceanEnvironmentImpl::LoadPreset(OceanSkyPreset preset)
    {
        Settings.Preset = preset;

        if (preset == OceanSkyPreset::AnalyticPreetham)
        {
            // Synthesize Analytic Preetham on CPU for initial data and compute SH
            ComputePreethamL2AndLight(
                Settings.PreethamTurbidity,
                Settings.SunDirection,
                Settings.Exposure,
                SphericalHarmonics,
                DominantLightDirection,
                DominantLightRadiance);

            // Default resolution for Preetham: 2048 x 1024
            ExtentW = 2048u;
            ExtentH = 1024u;
            MipLevels = static_cast<uint32_t>(std::bit_width(std::max(ExtentW, ExtentH)));

            // Prepare mip 0 CPU buffer
            std::vector<uint16_t> mip0Pixels(static_cast<size_t>(ExtentW) * static_cast<size_t>(ExtentH) * 4u);

            const math::Vector sunVec{ Settings.SunDirection[0], Settings.SunDirection[1], Settings.SunDirection[2], 0.0f };
            const float lenSq = sunVec.LengthSq<3>();
            const math::Float3 sFloat = (lenSq > 1e-10f)
                ? static_cast<math::Float3>(math::FromVector(sunVec.Normalize<3>()))
                : math::Float3{ 0.0f, 1.0f, 0.0f };
            const std::array<float, 3> s{ sFloat.x, sFloat.y, sFloat.z };

            const float cosSunZenith = std::clamp(s[1], 0.001f, 1.0f);
            const float thetaS = std::acos(cosSunZenith);
            const float T = std::clamp(Settings.PreethamTurbidity, 1.0f, 10.0f);
            const float T2 = T * T;

            const PerezCoeffs cy{ 0.17872f * T - 1.46303f, -0.35540f * T + 0.42749f, -0.02266f * T + 5.32505f, 0.12064f * T - 2.57705f, -0.06696f * T + 0.37027f };
            const PerezCoeffs cx{ -0.01925f * T - 0.25922f, -0.06651f * T + 0.00081f, -0.00041f * T + 0.21247f, -0.06409f * T - 0.89887f, -0.00325f * T + 0.04517f };
            const PerezCoeffs cz{ -0.01669f * T - 0.26078f, -0.08864f * T + 0.00921f, -0.00516f * T + 0.21023f, -0.06029f * T - 1.65369f, -0.00373f * T + 0.05291f };

            constexpr float k_PhotometricToScene = 1.0f / 22000.0f;
            const float chi = (4.0f / 9.0f - T / 120.0f) * (k_Pi - 2.0f * thetaS);
            float Yz = (4.0453f * T - 4.9710f) * std::tan(chi) - 0.2155f * T + 2.4192f;
            Yz = std::max(0.0f, Yz) * 1000.0f * k_PhotometricToScene;

            const float thetaS2 = thetaS * thetaS;
            const float thetaS3 = thetaS2 * thetaS;

            const float xz = (0.00166f * T2 - 0.02903f * T + 0.11693f) * thetaS3
                           + (-0.00375f * T2 + 0.06377f * T - 0.21196f) * thetaS2
                           + (0.00209f * T2 - 0.03202f * T + 0.06052f) * thetaS
                           + (0.00394f * T + 0.25886f);

            const float yz = (0.00275f * T2 - 0.04214f * T + 0.15346f) * thetaS3
                           + (-0.00610f * T2 + 0.08970f * T - 0.26756f) * thetaS2
                           + (0.00317f * T2 - 0.04517f * T + 0.06670f) * thetaS
                           + (0.00516f * T + 0.26688f);

            const float denomY = std::max(1e-6f, EvaluatePerez(cy, 1.0f, thetaS, cosSunZenith));
            const float denomX = std::max(1e-6f, EvaluatePerez(cx, 1.0f, thetaS, cosSunZenith));
            const float denomZ = std::max(1e-6f, EvaluatePerez(cz, 1.0f, thetaS, cosSunZenith));

            constexpr math::Float3x3 k_XyzToAp1{
                1.64102338f,  -0.324803294f, -0.236424695f,
                -0.663662859f, 1.61533159f,   0.0167563477f,
                0.0117218943f, -0.008284442f, 0.988394859f
            };

            constexpr float k_CosSolarDisk = 0.99996f;

            for (uint32_t y = 0; y < ExtentH; ++y)
            {
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(ExtentH);
                const float theta = v * k_Pi;
                const auto [sinTheta, cosTheta] = math::SinCos(theta);

                for (uint32_t x = 0; x < ExtentW; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(ExtentW);
                    const float phi = (u - 0.5f) * 2.0f * k_Pi;
                    const auto [sinPhi, cosPhi] = math::SinCos(phi);

                    const float dirX = sinTheta * cosPhi;
                    const float dirY = cosTheta;
                    const float dirZ = sinTheta * sinPhi;

                    const float cosView = std::max(0.001f, dirY);
                    const float cosGamma = std::clamp(dirX * s[0] + dirY * s[1] + dirZ * s[2], -1.0f, 1.0f);
                    const float gamma = std::acos(cosGamma);

                    const float fY = EvaluatePerez(cy, cosView, gamma, cosGamma) / denomY;
                    const float fx = EvaluatePerez(cx, cosView, gamma, cosGamma) / denomX;
                    const float fy = EvaluatePerez(cz, cosView, gamma, cosGamma) / denomZ;

                    const float Y = Yz * fY;
                    const float chrX = xz * fx;
                    const float chrY = std::max(1e-4f, yz * fy);

                    const float X = (chrX / chrY) * Y;
                    const float Z = std::max(0.0f, (1.0f - chrX - chrY) / chrY * Y);

                    const math::Float3 rgb = k_XyzToAp1 * math::Float3{ X, Y, Z };
                    float r = rgb.x;
                    float g = rgb.y;
                    float b = rgb.z;

                    r = std::max(0.0f, r) * Settings.Exposure;
                    g = std::max(0.0f, g) * Settings.Exposure;
                    b = std::max(0.0f, b) * Settings.Exposure;

                    if (cosGamma > k_CosSolarDisk)
                    {
                        const float diskRadiance = 40.0f * Settings.Exposure;
                        r += diskRadiance * 1.0f;
                        g += diskRadiance * 0.95f;
                        b += diskRadiance * 0.88f;
                    }

                    if (dirY < 0.0f)
                    {
                        const float groundFade = 0.15f * std::exp(dirY * 8.0f);
                        r *= groundFade;
                        g *= groundFade;
                        b *= groundFade;
                    }

                    const size_t idx = (static_cast<size_t>(y) * ExtentW + x) * 4u;
                    mip0Pixels[idx + 0] = math::FloatToHalf(r);
                    mip0Pixels[idx + 1] = math::FloatToHalf(g);
                    mip0Pixels[idx + 2] = math::FloatToHalf(b);
                    mip0Pixels[idx + 3] = math::FloatToHalf(1.0f);
                }
            }

            DestroyGpuResources();

            RhiImageResourceData mipData{};
            mipData.Data = std::as_bytes(std::span{ mip0Pixels });
            mipData.Width = ExtentW;
            mipData.Height = ExtentH;
            mipData.MipLevel = 0u;

            ImageCreateInfo imgInfo{};
            imgInfo.Domain = ResourceDomain::GPUOnly;
            imgInfo.Usage = ImageUsageBits::SampledImage | ImageUsageBits::TransferDestination | ImageUsageBits::TransferSource
                          | ImageUsageBits::UavComputeShader | ImageUsageBits::SrvComputeShader | ImageUsageBits::SrvFragmentShader;
            imgInfo.Type = ImageType::Type2D;
            imgInfo.Format = CommonFormats::RGBA16_Float;
            imgInfo.Width = ExtentW;
            imgInfo.Height = ExtentH;
            imgInfo.Depth = 1u;
            imgInfo.MipLevels = MipLevels;
            imgInfo.ViewInfo = ImageViewCreateInfo{};
            imgInfo.ViewInfo->ViewType = ImageViewType::Type2D;
            imgInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
            imgInfo.ViewInfo->Range.LevelCount = MipLevels;
            imgInfo.Name = "Ocean_SkyDomeImage";

            std::shared_ptr<GraphicsResourceReply> imgReply = Resources->CreateImage(imgInfo, std::span{ &mipData, 1 });
            if (imgReply == nullptr || imgReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << "OceanEnvironment: failed to create GPU image for Analytic Preetham\n";
                return false;
            }
            SkyDomeImage = imgReply->GetResource();
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE, SkyDomeImage.VkHandle, "Ocean_SkyDomeImage");
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE_VIEW, SkyDomeImage.VkViewHandle, "Ocean_SkyDomeView");
        }
        else
        {
            // HDRI OpenEXR file load
            const std::filesystem::path exrPath = ResolveHdriPath(preset);
            ExrLoadOptions loadOpts{};
            loadOpts.TargetMaxWidth = Settings.Downsample8K ? 8192u : 16384u;

            std::expected<ExrImageResult, std::string> exrResult = LoadExrEnvironmentMap(exrPath, loadOpts);
            if (!exrResult.has_value())
            {
                std::cerr << std::format("OceanEnvironment: {}\n", exrResult.error());
                return false;
            }

            ExtentW = exrResult->Width;
            ExtentH = exrResult->Height;
            MipLevels = static_cast<uint32_t>(std::bit_width(std::max(ExtentW, ExtentH)));
            SphericalHarmonics = exrResult->SphericalHarmonics;
            DominantLightDirection = exrResult->DominantLightDirection;
            DominantLightRadiance = exrResult->DominantLightRadiance;

            DestroyGpuResources();

            RhiImageResourceData mipData{};
            mipData.Data = std::as_bytes(std::span{ exrResult->PixelsFp16 });
            mipData.Width = ExtentW;
            mipData.Height = ExtentH;
            mipData.MipLevel = 0u;

            ImageCreateInfo imgInfo{};
            imgInfo.Domain = ResourceDomain::GPUOnly;
            imgInfo.Usage = ImageUsageBits::SampledImage | ImageUsageBits::TransferDestination | ImageUsageBits::TransferSource
                          | ImageUsageBits::UavComputeShader | ImageUsageBits::SrvComputeShader | ImageUsageBits::SrvFragmentShader;
            imgInfo.Type = ImageType::Type2D;
            imgInfo.Format = CommonFormats::RGBA16_Float;
            imgInfo.Width = ExtentW;
            imgInfo.Height = ExtentH;
            imgInfo.Depth = 1u;
            imgInfo.MipLevels = MipLevels;
            imgInfo.ViewInfo = ImageViewCreateInfo{};
            imgInfo.ViewInfo->ViewType = ImageViewType::Type2D;
            imgInfo.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
            imgInfo.ViewInfo->Range.LevelCount = MipLevels;
            imgInfo.Name = "Ocean_SkyDomeImage";

            std::shared_ptr<GraphicsResourceReply> imgReply = Resources->CreateImage(imgInfo, std::span{ &mipData, 1 });
            if (imgReply == nullptr || imgReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << "OceanEnvironment: failed to create GPU image for EXR HDRI\n";
                return false;
            }
            SkyDomeImage = imgReply->GetResource();
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE, SkyDomeImage.VkHandle, "Ocean_SkyDomeImage");
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE_VIEW, SkyDomeImage.VkViewHandle, "Ocean_SkyDomeView");
        }

        // Create sampler
        SamplerCreateInfo sampInfo{};
        sampInfo.MagFilter = ImageFilteringMode::Linear;
        sampInfo.MinFilter = ImageFilteringMode::Linear;
        sampInfo.MipFilter = ImageFilteringMode::Linear;
        sampInfo.AddressU = SamplerCreateInfo::AddressMode::Repeat;
        sampInfo.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        sampInfo.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        sampInfo.MinLod = 0.0f;
        sampInfo.MaxLod = static_cast<float>(MipLevels);
        sampInfo.Name = "Ocean_SkyDomeSampler";

        std::shared_ptr<GraphicsResourceReply> sampReply = Resources->CreateSampler(sampInfo, "Ocean_SkyDomeSampler");
        if (sampReply == nullptr || sampReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << "OceanEnvironment: failed to create skydome sampler\n";
            return false;
        }
        SkyDomeSampler = sampReply->GetResource();
        SetDebugName(*Device, VK_OBJECT_TYPE_SAMPLER, SkyDomeSampler.VkSamplerHandle, "Ocean_SkyDomeSampler");

        // Create per-mip image views for downsampling
        const VkDevice vkDev = Device->Handle().As<VkDevice>();
        MipViews.resize(MipLevels, VK_NULL_HANDLE);

        for (uint32_t level = 0u; level < MipLevels; ++level)
        {
            const VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = reinterpret_cast<VkImage>(SkyDomeImage.VkHandle),
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .subresourceRange = VkImageSubresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = level,
                    .levelCount = 1u,
                    .baseArrayLayer = 0u,
                    .layerCount = 1u
                }
            };
            if (vkCreateImageView(vkDev, &viewInfo, nullptr, &MipViews[level]) == VK_SUCCESS)
            {
                const std::string name = std::format("Ocean_SkyDomeMip{}View", level);
                SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(MipViews[level]), name.c_str());
            }
        }

        NeedsMipGen = true;
        (void)SetupDescriptorSets();
        return true;
    }

    std::expected<std::unique_ptr<OceanEnvironment>, std::string> OceanEnvironment::Create(
        rhi::Device& device,
        ResourceContext& resources,
        const rhi::ShaderLibrary& library,
        const OceanEnvironmentSettings& settings)
    {
        auto impl = std::make_unique<OceanEnvironmentImpl>();
        impl->Device = &device;
        impl->Resources = &resources;
        impl->Library = &library;
        impl->Settings = settings;

        std::string_view foundModuleName = k_ModuleName;
        auto moduleIndex = library.ModuleIndex(k_ModuleName);
        if (!moduleIndex.has_value())
        {
            foundModuleName = "OceanRender";
            moduleIndex = library.ModuleIndex("OceanRender");
        }

        if (moduleIndex.has_value())
        {
            impl->Index = *moduleIndex;

            // Preetham compute program
            const std::array<std::string_view, 1> preethamEntry{ k_PreethamEntry };
            if (auto var = (*moduleIndex)->Query().SingleFor(preethamEntry); var.has_value())
            {
                impl->PreethamVariant = *var;
                if (auto prog = library.CreateProgram(device.Handle(), foundModuleName, *var, preethamEntry); prog.has_value())
                {
                    impl->PreethamProgram = std::move(*prog);
                    SetDebugName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, impl->PreethamProgram->PipelineLayout().As<uint64_t>(), "Ocean_SkyDomePreethamLayout");
                }
            }

            // Downsample compute program
            const std::array<std::string_view, 1> downsampleEntry{ k_DownsampleEntry };
            if (auto var = (*moduleIndex)->Query().SingleFor(downsampleEntry); var.has_value())
            {
                impl->DownsampleVariant = *var;
                if (auto prog = library.CreateProgram(device.Handle(), foundModuleName, *var, downsampleEntry); prog.has_value())
                {
                    impl->DownsampleProgram = std::move(*prog);
                    SetDebugName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, impl->DownsampleProgram->PipelineLayout().As<uint64_t>(), "Ocean_SkyDomeDownsampleLayout");
                }
            }

            // Sky background fullscreen graphics program
            const std::array<std::string_view, 2> skyEntries{ k_SkyVsEntry, k_SkyFsEntry };
            if (auto var = (*moduleIndex)->Query().SingleFor(skyEntries); var.has_value())
            {
                impl->SkyVariant = *var;
                if (auto prog = library.CreateProgram(device.Handle(), foundModuleName, *var, skyEntries); prog.has_value())
                {
                    impl->SkyProgram = std::move(*prog);
                    SetDebugName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, impl->SkyProgram->PipelineLayout().As<uint64_t>(), "Ocean_SkyDomeSkyBackgroundLayout");
                }
            }
        }

        if (!impl->LoadPreset(settings.Preset))
        {
            // Fall back to AnalyticPreetham if initial HDRI load fails
            if (settings.Preset != OceanSkyPreset::AnalyticPreetham)
            {
                std::cerr << "OceanEnvironment: falling back to Analytic Preetham preset\n";
                (void)impl->LoadPreset(OceanSkyPreset::AnalyticPreetham);
            }
        }

        return std::unique_ptr<OceanEnvironment>(new OceanEnvironment(std::move(impl)));
    }

    OceanEnvironment::OceanEnvironment(std::unique_ptr<OceanEnvironmentImpl>&& _impl) noexcept
        : impl{ std::move(_impl) }
    {}

    OceanEnvironment::~OceanEnvironment() = default;

    bool OceanEnvironment::SetPreset(OceanSkyPreset preset)
    {
        if (impl->Settings.Preset == preset && impl->SkyDomeImage)
        {
            return true;
        }
        return impl->LoadPreset(preset);
    }

    void OceanEnvironment::SetDownsample8K(bool downsample8k)
    {
        if (impl->Settings.Downsample8K != downsample8k)
        {
            impl->Settings.Downsample8K = downsample8k;
            if (impl->Settings.Preset != OceanSkyPreset::AnalyticPreetham)
            {
                (void)impl->LoadPreset(impl->Settings.Preset);
            }
        }
    }

    void OceanEnvironment::SetPreethamParameters(float turbidity, const std::array<float, 3>& sun_dir, float exposure)
    {
        impl->Settings.PreethamTurbidity = turbidity;
        impl->Settings.SunDirection = sun_dir;
        impl->Settings.Exposure = exposure;

        if (impl->Settings.Preset == OceanSkyPreset::AnalyticPreetham)
        {
            ComputePreethamL2AndLight(
                turbidity,
                sun_dir,
                exposure,
                impl->SphericalHarmonics,
                impl->DominantLightDirection,
                impl->DominantLightRadiance);
            impl->NeedsMipGen = true;
        }
    }

    void OceanEnvironment::SetExposure(float exposure) noexcept
    {
        impl->Settings.Exposure = exposure;
    }

    void OceanEnvironment::SetSampleCount(VkSampleCountFlagBits samples) noexcept
    {
        impl->Settings.Samples = samples;
    }

    OceanSkyPreset OceanEnvironment::CurrentPreset() const noexcept
    {
        return impl->Settings.Preset;
    }

    bool OceanEnvironment::IsDownsample8K() const noexcept
    {
        return impl->Settings.Downsample8K;
    }

    GraphicsResource OceanEnvironment::Image() const noexcept
    {
        return impl->SkyDomeImage;
    }

    VkImageView OceanEnvironment::TextureView() const noexcept
    {
        return reinterpret_cast<VkImageView>(impl->SkyDomeImage.VkViewHandle);
    }

    GraphicsResource OceanEnvironment::Sampler() const noexcept
    {
        return impl->SkyDomeSampler;
    }

    VkSampler OceanEnvironment::VkSamplerHandle() const noexcept
    {
        return reinterpret_cast<VkSampler>(impl->SkyDomeSampler.VkSamplerHandle);
    }

    const SphericalHarmonicsL2& OceanEnvironment::SphericalHarmonics() const noexcept
    {
        return impl->SphericalHarmonics;
    }

    std::array<float, 3> OceanEnvironment::DominantLightDirection() const noexcept
    {
        return impl->DominantLightDirection;
    }

    std::array<float, 3> OceanEnvironment::DominantLightRadiance() const noexcept
    {
        return impl->DominantLightRadiance;
    }

    std::array<float, 3> OceanEnvironment::EvaluateIrradiance(const std::array<float, 3>& normal) const noexcept
    {
        return EvaluateShIrradiance(impl->SphericalHarmonics, normal);
    }

    void OceanEnvironment::RecordMipGen(rhi::CommandBuffer& cmd, rhi::GpuTimerBlock* timer_block)
    {
        if (!impl->NeedsMipGen || !impl->SkyDomeImage)
        {
            return;
        }

        std::optional<rhi::ScopedGpuTimer> timer;
        if (timer_block != nullptr)
        {
            timer.emplace(*timer_block, "Env mipgen");
        }

        if (impl->DownsampleProgram == nullptr || impl->MipViews.size() < 2u)
        {
            impl->NeedsMipGen = false;
            return;
        }

        RhiAssert(cmd.BindShaders(impl->DownsampleProgram->Program()));

        const rhi::ImageBarrier toGeneral{
            .Image = rhi::ImageHandle{ impl->SkyDomeImage.VkHandle },
            .SrcStages = rhi::PipelineStage::AllTransfer | rhi::PipelineStage::ComputeShader,
            .SrcAccess = rhi::Access::TransferWrite | rhi::Access::ShaderStorageWrite,
            .DstStages = rhi::PipelineStage::ComputeShader,
            .DstAccess = rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageWrite,
            .OldLayout = rhi::ImageLayout::Undefined,
            .NewLayout = rhi::ImageLayout::General,
            .Range = rhi::ImageSubresourceRange{
                .Aspects = rhi::ImageAspect::Color,
                .BaseMipLevel = 0u,
                .LevelCount = impl->MipLevels,
                .BaseArrayLayer = 0u,
                .LayerCount = 1u
            }
        };
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toGeneral, 1 } });

        const uint32_t levels = static_cast<uint32_t>(impl->MipViews.size());
        for (uint32_t m = 0u; m < levels - 1u; ++m)
        {
            const uint32_t dstW = std::max(1u, impl->ExtentW >> (m + 1u));
            const uint32_t dstH = std::max(1u, impl->ExtentH >> (m + 1u));
            const uint32_t srcW = std::max(1u, impl->ExtentW >> m);
            const uint32_t srcH = std::max(1u, impl->ExtentH >> m);

            if (m < impl->DownsampleSets.size() && impl->DownsampleSets[m] != VK_NULL_HANDLE)
            {
                const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(impl->DownsampleSets[m]) };
                cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, impl->DownsampleProgram->PipelineLayout(), 0, std::span{ &setHandle, 1 });
            }

            struct DownsamplePushConstants
            {
                uint32_t DstWidth;
                uint32_t DstHeight;
                uint32_t SrcWidth;
                uint32_t SrcHeight;
            };
            const DownsamplePushConstants push{ dstW, dstH, srcW, srcH };

            if (const std::optional<rhi::PushConstantRange> range = impl->DownsampleProgram->PushConstants(); range.has_value())
            {
                cmd.PushConstants(impl->DownsampleProgram->PipelineLayout(), range->StageFlags, 0, std::as_bytes(std::span{ &push, 1 }));
            }

            cmd.Dispatch((dstW + 7u) / 8u, (dstH + 7u) / 8u, 1u);

            const rhi::GlobalBarrier mipBarrier{
                .SrcStages = rhi::PipelineStage::ComputeShader,
                .SrcAccess = rhi::Access::ShaderStorageWrite,
                .DstStages = rhi::PipelineStage::ComputeShader,
                .DstAccess = rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageRead
            };
            cmd.Barrier(rhi::DependencyInfo{ std::span{ &mipBarrier, 1 }, {}, {} });
        }

        const rhi::ImageBarrier toSampledLayout{
            .Image = rhi::ImageHandle{ impl->SkyDomeImage.VkHandle },
            .SrcStages = rhi::PipelineStage::ComputeShader,
            .SrcAccess = rhi::Access::ShaderStorageWrite,
            .DstStages = rhi::PipelineStage::FragmentShader,
            .DstAccess = rhi::Access::ShaderSampledRead,
            .OldLayout = rhi::ImageLayout::General,
            .NewLayout = rhi::ImageLayout::ShaderReadOnly,
            .Range = rhi::ImageSubresourceRange{
                .Aspects = rhi::ImageAspect::Color,
                .BaseMipLevel = 0u,
                .LevelCount = impl->MipLevels,
                .BaseArrayLayer = 0u,
                .LayerCount = 1u
            }
        };
        cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toSampledLayout, 1 } });

        impl->NeedsMipGen = false;
    }

    void OceanEnvironment::RecordSkyBackground(
        rhi::CommandBuffer& cmd,
        rhi::GpuTimerBlock* timer_block,
        const math::Float4x4& inv_view_proj,
        const math::Float3& camera_pos,
        float lod,
        std::optional<VkSampleCountFlagBits> sample_count_override)
    {
        if (!impl->SkyDomeImage || impl->SkyProgram == nullptr)
        {
            return;
        }

        std::optional<rhi::ScopedGpuTimer> timer;
        if (timer_block != nullptr)
        {
            timer.emplace(*timer_block, "Sky background");
        }

        RhiAssert(cmd.BindShaders(impl->SkyProgram->Program()));

        SetShaderObjectDrawState(cmd.Handle().As<VkCommandBuffer>(), ShaderObjectDrawState{
            .DepthTest = true,
            .DepthWrite = false,
            .AlphaBlend = false,
            .CullMode = VK_CULL_MODE_NONE,
            .Samples = sample_count_override.value_or(impl->Settings.Samples)
        });

        // Allow far plane depth (0.0) to pass where depth buffer was cleared to 0.0 and no ocean geometry was rendered
        vkCmdSetDepthCompareOp(cmd.Handle().As<VkCommandBuffer>(), VK_COMPARE_OP_GREATER_OR_EQUAL);

        if (impl->SkyDescriptorSet != VK_NULL_HANDLE)
        {
            const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(impl->SkyDescriptorSet) };
            cmd.BindDescriptorSets(rhi::PipelineBindPoint::Graphics, impl->SkyProgram->PipelineLayout(), 0, std::span{ &setHandle, 1 });
        }

        struct SkyPushConstants
        {
            std::array<float, 4> InvViewProjRow0;
            std::array<float, 4> InvViewProjRow1;
            std::array<float, 4> InvViewProjRow2;
            std::array<float, 4> InvViewProjRow3;
            std::array<float, 4> CameraPosition;
            float Exposure;
            float Lod;
            std::array<float, 2> Pad;
        };

        const SkyPushConstants skyPush{
            .InvViewProjRow0 = { inv_view_proj[0][0], inv_view_proj[0][1], inv_view_proj[0][2], inv_view_proj[0][3] },
            .InvViewProjRow1 = { inv_view_proj[1][0], inv_view_proj[1][1], inv_view_proj[1][2], inv_view_proj[1][3] },
            .InvViewProjRow2 = { inv_view_proj[2][0], inv_view_proj[2][1], inv_view_proj[2][2], inv_view_proj[2][3] },
            .InvViewProjRow3 = { inv_view_proj[3][0], inv_view_proj[3][1], inv_view_proj[3][2], inv_view_proj[3][3] },
            .CameraPosition = { camera_pos.x, camera_pos.y, camera_pos.z, 1.0f },
            .Exposure = impl->Settings.Exposure,
            .Lod = lod,
            .Pad = { 0.0f, 0.0f }
        };

        if (const std::optional<rhi::PushConstantRange> range = impl->SkyProgram->PushConstants(); range.has_value())
        {
            cmd.PushConstants(impl->SkyProgram->PipelineLayout(), range->StageFlags, 0, std::as_bytes(std::span{ &skyPush, 1 }));
        }

        cmd.Draw(3u, 1u, 0u, 0u);
    }
}
