#include "BootResult.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "DeviceRequirements.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "test_modules.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using bootstrap::BundleRequest;
using bootstrap::ContentStage;
using bootstrap::CoreStage;
using bootstrap::DeviceRequirements;
using bootstrap::LaunchStage;
using bootstrap::ModuleDescriptor;
using bootstrap::Need;
using bootstrap::NeedKind;
using bootstrap::NeedLevel;
using bootstrap::Stage;
using bootstrap_tests::TestArguments;

namespace
{
    // the subgroup arithmetic capability that only the wave variant of WaveSumCS needs
    constexpr std::string_view k_WaveCapability{ "GroupNonUniformArithmetic" };
    // both variants of WaveSumCS, and PlainFillCS, do 64-bit integer math
    constexpr std::string_view k_Int64Capability{ "Int64" };

    const std::array<BundleRequest, 1> k_AppBundles{ bootstrap_tests::k_BootNeedsRequest };
    const std::array<BundleRequest, 1> k_HashFillOnly{ bootstrap_tests::k_HashFillRequest };

    /** Boots the launch, core and content stages for one test. */
    class BootedContent
    {
    public:
        BootedContent(std::span<const ModuleDescriptor> linked, std::span<const BundleRequest> app_bundles);

        TestArguments arguments;
        LaunchStage launch;
        CoreStage core;
        ContentStage content;
    };

    [[nodiscard]] const Need* FindNeed(std::span<const Need> needs, std::string_view name);
    [[nodiscard]] int32_t CountNeeds(std::span<const Need> needs, std::string_view name);
    [[nodiscard]] std::string DescribeNeeds(std::span<const Need> needs);
}

TEST(DeviceRequirements, ACapabilityOfOnlySomeVariantsIsWanted)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});
    const Need* wave = FindNeed(requirements.Capabilities, k_WaveCapability);

    ASSERT_NE(wave, nullptr) << DescribeNeeds(requirements.Capabilities);
    EXPECT_EQ(wave->Kind, NeedKind::Capability);
    EXPECT_EQ(wave->Level, NeedLevel::Wanted);
    EXPECT_TRUE(wave->Source.Module.empty());
    EXPECT_EQ(wave->Source.BundleFile, bootstrap_tests::k_BootNeedsRequest.FileName);
    EXPECT_EQ(wave->Source.ShaderModule, "BootNeedsWave");
    EXPECT_EQ(wave->Source.EntryPoint, "WaveSumCS");
}

TEST(DeviceRequirements, ACapabilityOfEveryVariantIsRequired)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});
    const Need* int64 = FindNeed(requirements.Capabilities, k_Int64Capability);

    ASSERT_NE(int64, nullptr) << DescribeNeeds(requirements.Capabilities);
    EXPECT_EQ(int64->Level, NeedLevel::Required);
    EXPECT_FALSE(int64->Source.ShaderModule.empty());
    EXPECT_FALSE(int64->Source.EntryPoint.empty());
}

TEST(DeviceRequirements, ABundleThatNeedsNoCapabilityAddsOnlyLimits)
{
    // HashFill uses only base capabilities, which Lodestone does not list. Its parameters are a uniform buffer
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_HashFillOnly);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});

    EXPECT_TRUE(requirements.Capabilities.empty()) << DescribeNeeds(requirements.Capabilities);
    EXPECT_EQ(requirements.RequiredLimits.MaxInvocationsPerWorkgroup, 64u);
    EXPECT_EQ(requirements.RequiredLimits.MaxWorkgroupSizeX, 64u);
    EXPECT_EQ(requirements.RequiredLimits.MaxPushConstantBytes, 0u);
}

TEST(DeviceRequirements, TheFirstModuleKeepsASharedName)
{
    // both modules of the bundle require Int64: the entry keeps the source of the module that the bundle lists first
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});
    const Need* int64 = FindNeed(requirements.Capabilities, k_Int64Capability);
    const std::vector<std::string_view> moduleNames = booted.content.Bundles()[0].Bundle.ModuleNames();

    ASSERT_NE(int64, nullptr);
    ASSERT_EQ(moduleNames.size(), 2u);
    EXPECT_EQ(CountNeeds(requirements.Capabilities, k_Int64Capability), 1);
    EXPECT_EQ(int64->Source.ShaderModule, moduleNames[0]);
}

TEST(DeviceRequirements, EachNameKeepsOneEntry)
{
    const std::array<ModuleDescriptor, 2> linked{ bootstrap_tests::k_HashFillUserModule, bootstrap_tests::k_ExtensionsModule };
    const BootedContent booted(linked, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});

    for (const Need& need : requirements.Capabilities)
    {
        EXPECT_EQ(CountNeeds(requirements.Capabilities, need.Name), 1) << need.Name;
    }

    for (const Need& need : requirements.DeviceExtensions)
    {
        EXPECT_EQ(CountNeeds(requirements.DeviceExtensions, need.Name), 1) << need.Name;
    }
}

TEST(DeviceRequirements, ModuleNeedsGoToTheirKind)
{
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_ExtensionsModule };
    const BootedContent booted(linked, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});

    ASSERT_EQ(requirements.InstanceExtensions.size(), 1u);
    EXPECT_EQ(requirements.InstanceExtensions[0].Name, "VK_EXT_debug_utils");
    EXPECT_EQ(requirements.InstanceExtensions[0].Kind, NeedKind::InstanceExtension);
    EXPECT_EQ(requirements.InstanceExtensions[0].Level, NeedLevel::Wanted);
    EXPECT_EQ(requirements.InstanceExtensions[0].Source.Module, "extensions");

    ASSERT_EQ(requirements.DeviceExtensions.size(), 2u);
    EXPECT_EQ(requirements.DeviceExtensions[0].Name, "VK_KHR_swapchain");
    EXPECT_EQ(requirements.DeviceExtensions[1].Name, "VK_EXT_shader_object");
    EXPECT_EQ(requirements.DeviceExtensions[1].Kind, NeedKind::DeviceExtension);
    EXPECT_EQ(requirements.DeviceExtensions[1].Level, NeedLevel::Required);
}

TEST(DeviceRequirements, RequiredWinsAndTakesTheRequiredSource)
{
    // the module requires the capability that the bundle only wants. The module's need comes first and stays
    const std::array<ModuleDescriptor, 1> linked{ bootstrap_tests::k_ExtensionsModule };
    const BootedContent booted(linked, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});
    const Need* wave = FindNeed(requirements.Capabilities, k_WaveCapability);

    ASSERT_NE(wave, nullptr);
    EXPECT_EQ(wave->Level, NeedLevel::Required);
    EXPECT_EQ(wave->Source.Module, "extensions");
    EXPECT_TRUE(wave->Source.EntryPoint.empty());
}

TEST(DeviceRequirements, AWantedSpineNeedBecomesRequiredWithTheBundleSource)
{
    const std::array<Need, 2> spine
    {
        Need{ .Name = k_Int64Capability, .Kind = NeedKind::Capability, .Level = NeedLevel::Wanted },
        Need{ .Name = "VK_KHR_surface", .Kind = NeedKind::InstanceExtension, .Level = NeedLevel::Required },
    };
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    const Need* int64 = FindNeed(requirements.Capabilities, k_Int64Capability);

    ASSERT_NE(int64, nullptr);
    EXPECT_EQ(int64->Level, NeedLevel::Required);
    EXPECT_EQ(int64->Source.BundleFile, bootstrap_tests::k_BootNeedsRequest.FileName);
    ASSERT_EQ(requirements.InstanceExtensions.size(), 1u);
    EXPECT_EQ(requirements.InstanceExtensions[0].Name, "VK_KHR_surface");
    EXPECT_TRUE(requirements.DeviceExtensions.empty());
}

TEST(DeviceRequirements, ARequiredSpineNeedKeepsItsSource)
{
    const std::array<Need, 1> spine
    {
        Need{ .Name = k_WaveCapability, .Kind = NeedKind::Capability, .Level = NeedLevel::Required },
    };
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    const Need* wave = FindNeed(requirements.Capabilities, k_WaveCapability);

    ASSERT_NE(wave, nullptr);
    EXPECT_EQ(wave->Level, NeedLevel::Required);
    EXPECT_TRUE(wave->Source.BundleFile.empty());
}

TEST(DeviceRequirements, AFeatureNeedGoesToTheFeatureList)
{
    constexpr std::string_view k_Feature{ "VkPhysicalDeviceVulkan13Features.synchronization2" };
    const std::array<Need, 2> spine
    {
        Need{ .Name = k_Feature, .Kind = NeedKind::DeviceFeature, .Level = NeedLevel::Wanted },
        Need{ .Name = k_Feature, .Kind = NeedKind::DeviceFeature, .Level = NeedLevel::Required },
    };
    const BootedContent booted(std::span<const ModuleDescriptor>{}, std::span<const BundleRequest>{});
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);

    ASSERT_EQ(requirements.DeviceFeatures.size(), 1u);
    EXPECT_EQ(requirements.DeviceFeatures[0].Name, k_Feature);
    EXPECT_EQ(requirements.DeviceFeatures[0].Kind, NeedKind::DeviceFeature);
    EXPECT_EQ(requirements.DeviceFeatures[0].Level, NeedLevel::Required);
    EXPECT_EQ(FindNeed(requirements.DeviceExtensions, k_Feature), nullptr);
    EXPECT_EQ(FindNeed(requirements.Capabilities, k_Feature), nullptr);
}

TEST(DeviceRequirements, LimitsComeFromTheEntryPoints)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, k_AppBundles);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, std::span<const Need>{});
    const lodestone::manifest::DeviceLimits& required = requirements.RequiredLimits;
    const lodestone::manifest::DeviceLimits& wanted = requirements.WantedLimits;

    // WaveSumCS: 64 x 1 x 1 and 64 uints of workgroup memory. PlainFillCS: 8 x 8 x 2, and two uints of push constants
    EXPECT_EQ(required.MaxWorkgroupMemoryBytes, 256u);
    EXPECT_EQ(required.MaxInvocationsPerWorkgroup, 128u);
    EXPECT_EQ(required.MaxWorkgroupSizeX, 64u);
    EXPECT_EQ(required.MaxWorkgroupSizeY, 8u);
    EXPECT_EQ(required.MaxWorkgroupSizeZ, 2u);
    EXPECT_EQ(required.MaxPushConstantBytes, 8u);

    // no axis here changes a size, so both sets agree
    EXPECT_EQ(wanted.MaxWorkgroupMemoryBytes, required.MaxWorkgroupMemoryBytes);
    EXPECT_EQ(wanted.MaxInvocationsPerWorkgroup, required.MaxInvocationsPerWorkgroup);
    EXPECT_EQ(wanted.MaxWorkgroupSizeX, required.MaxWorkgroupSizeX);
    EXPECT_EQ(wanted.MaxWorkgroupSizeY, required.MaxWorkgroupSizeY);
    EXPECT_EQ(wanted.MaxWorkgroupSizeZ, required.MaxWorkgroupSizeZ);
    EXPECT_EQ(wanted.MaxPushConstantBytes, required.MaxPushConstantBytes);
}

namespace
{
    BootedContent::BootedContent(std::span<const ModuleDescriptor> linked, std::span<const BundleRequest> app_bundles) :
        arguments({})
    {
        const bootstrap::BootResult launched = launch.Start(arguments.Span(), linked, Stage::Gpu);
        const bootstrap::BootResult cored = core.Start(launch);
        const bootstrap::BootResult contented = content.Start(core, app_bundles);

        EXPECT_TRUE(launched.Succeeded()) << bootstrap::FormatFailure(launched);
        EXPECT_TRUE(cored.Succeeded()) << bootstrap::FormatFailure(cored);
        EXPECT_TRUE(contented.Succeeded()) << bootstrap::FormatFailure(contented);
    }

    const Need* FindNeed(std::span<const Need> needs, std::string_view name)
    {
        const auto found = std::ranges::find(needs, name, &Need::Name);

        if (found == needs.end())
        {
            return nullptr;
        }

        return &*found;
    }

    int32_t CountNeeds(std::span<const Need> needs, std::string_view name)
    {
        return static_cast<int32_t>(std::ranges::count(needs, name, &Need::Name));
    }

    std::string DescribeNeeds(std::span<const Need> needs)
    {
        std::string text = "needs:";

        for (const Need& need : needs)
        {
            text += " ";
            text += need.Name;
            text += "(";
            text += bootstrap::ToString(need.Level);
            text += ")";
        }

        return text;
    }
}
