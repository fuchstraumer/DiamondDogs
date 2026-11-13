#include "Device.hpp"
#include "DevicePlanner.hpp"
#include "FeatureChain.hpp"
#include "GeneratedFeatureTable.hpp"
#include "Instance.hpp"
#include "RhiSystem.hpp"
#include "RhiSystemStart.hpp"
#include "ShaderManifestDevice.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

using rhi::DeviceNeed;
using rhi::DeviceNeedLevel;
using rhi::DevicePlan;
using rhi::DeviceStartInfo;
using rhi::RhiSystemResult;
using rhi::RhiSystemResultCode;

namespace
{
    constexpr std::array<DeviceNeed, 1> k_DebugUtils{ DeviceNeed{ .Name = "VK_EXT_debug_utils", .Level = DeviceNeedLevel::Required } };

    [[nodiscard]] rhi::InstanceStartInfo TestInstanceInfo();
    [[nodiscard]] ::testing::AssertionResult StartTestInstance(rhi::RhiSystem& system);
    [[nodiscard]] const rhi::generated::MemberInfo& Member(std::string_view struct_name, std::string_view member_name);
    [[nodiscard]] VkPhysicalDevice FirstPhysicalDevice(rhi::RhiSystem& system);
    [[nodiscard]] bool Contains(std::span<const std::string> names, std::string_view name);
}

TEST(RhiSystemStart, APlanWithNoNeedsEnablesNothing)
{
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system), system.GetVulkanApiVersion(), DeviceStartInfo{});

    EXPECT_TRUE(plan.UnmetNeed.empty()) << plan.UnmetNeed;
    EXPECT_TRUE(plan.Extensions.empty());
    // the old path enabled every supported feature, robustBufferAccess included
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceFeatures", "robustBufferAccess")), 0u);
    EXPECT_FALSE(plan.Enabled.Contains("VkPhysicalDeviceVulkan13Features"));
}

TEST(RhiSystemStart, APromotedFeatureIsSetInTheCoreStruct)
{
    const std::array<DeviceNeed, 1> features
    {
        DeviceNeed{ .Name = "VkPhysicalDeviceTimelineSemaphoreFeatures.timelineSemaphore", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system), system.GetVulkanApiVersion(), DeviceStartInfo{ .Features = features });

    ASSERT_TRUE(plan.UnmetNeed.empty()) << plan.UnmetNeed << ": " << plan.UnmetReason;
    // device creation must not chain a promoted struct beside the core struct that absorbed it
    EXPECT_TRUE(plan.Enabled.Contains("VkPhysicalDeviceVulkan12Features"));
    EXPECT_FALSE(plan.Enabled.Contains("VkPhysicalDeviceTimelineSemaphoreFeatures"));
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceVulkan12Features", "timelineSemaphore")), 1u);
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceVulkan12Features", "bufferDeviceAddress")), 0u);
    EXPECT_TRUE(Contains(plan.EnabledFeatures, "VkPhysicalDeviceTimelineSemaphoreFeatures.timelineSemaphore"));
}

TEST(RhiSystemStart, AnExtensionFeatureEnablesItsExtension)
{
    const std::array<DeviceNeed, 1> features
    {
        DeviceNeed{ .Name = "VkPhysicalDeviceShaderObjectFeaturesEXT.shaderObject", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system), system.GetVulkanApiVersion(), DeviceStartInfo{ .Features = features });

    ASSERT_TRUE(plan.UnmetNeed.empty()) << plan.UnmetNeed << ": " << plan.UnmetReason;
    EXPECT_TRUE(Contains(plan.Extensions, "VK_EXT_shader_object"));
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceShaderObjectFeaturesEXT", "shaderObject")), 1u);
}

TEST(RhiSystemStart, CapabilitiesUseTheFirstSupportedEnable)
{
    // Int64 is a 1.0 feature. GroupNonUniformArithmetic is a property bit: it enables nothing, and only tests
    const std::array<DeviceNeed, 2> capabilities
    {
        DeviceNeed{ .Name = "Int64", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "GroupNonUniformArithmetic", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system), system.GetVulkanApiVersion(), DeviceStartInfo{ .Capabilities = capabilities });

    ASSERT_TRUE(plan.UnmetNeed.empty()) << plan.UnmetNeed << ": " << plan.UnmetReason;
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceFeatures", "shaderInt64")), 1u);
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceFeatures", "shaderFloat64")), 0u);
    EXPECT_TRUE(Contains(plan.EnabledCapabilities, "Int64"));
    EXPECT_TRUE(Contains(plan.EnabledCapabilities, "GroupNonUniformArithmetic"));
    EXPECT_TRUE(plan.Extensions.empty());
}

TEST(RhiSystemStart, ADeclinedWantedCapabilityStaysOff)
{
    const std::array<DeviceNeed, 1> capabilities
    {
        DeviceNeed{ .Name = "Int64", .Level = DeviceNeedLevel::Wanted },
    };
    const std::array<std::string_view, 1> declined{ "Int64" };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system),
                                      system.GetVulkanApiVersion(),
                                      DeviceStartInfo{ .Capabilities = capabilities, .Declined = declined });

    EXPECT_TRUE(plan.UnmetNeed.empty());
    EXPECT_FALSE(Contains(plan.EnabledCapabilities, "Int64"));
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceFeatures", "shaderInt64")), 0u);
}

TEST(RhiSystemStart, DeclinedWantedExtensionsAndFeaturesStayOff)
{
    const std::array<DeviceNeed, 1> extensions
    {
        DeviceNeed{ .Name = "VK_EXT_shader_object", .Level = DeviceNeedLevel::Wanted },
    };
    const std::array<DeviceNeed, 1> features
    {
        DeviceNeed{ .Name = "VkPhysicalDeviceFeatures.shaderInt64", .Level = DeviceNeedLevel::Wanted },
    };
    const std::array<std::string_view, 2> declined{ "VK_EXT_shader_object", "VkPhysicalDeviceFeatures.shaderInt64" };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system),
                                      system.GetVulkanApiVersion(),
                                      DeviceStartInfo{ .Extensions = extensions, .Features = features, .Declined = declined });

    EXPECT_TRUE(plan.UnmetNeed.empty());
    EXPECT_FALSE(Contains(plan.Extensions, "VK_EXT_shader_object"));
    EXPECT_TRUE(plan.EnabledFeatures.empty());
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceFeatures", "shaderInt64")), 0u);
}

TEST(RhiSystemStart, DeclinedHasNoEffectOnARequiredNeed)
{
    const std::array<DeviceNeed, 1> features
    {
        DeviceNeed{ .Name = "VkPhysicalDeviceFeatures.shaderInt64", .Level = DeviceNeedLevel::Required },
    };
    const std::array<std::string_view, 1> declined{ "VkPhysicalDeviceFeatures.shaderInt64" };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system),
                                      system.GetVulkanApiVersion(),
                                      DeviceStartInfo{ .Features = features, .Declined = declined });

    ASSERT_TRUE(plan.UnmetNeed.empty()) << plan.UnmetNeed << ": " << plan.UnmetReason;
    EXPECT_EQ(plan.Enabled.Read(Member("VkPhysicalDeviceFeatures", "shaderInt64")), 1u);
}

TEST(RhiSystemStart, ARequiredLimitAboveTheDeviceIsNamed)
{
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    const lodestone::manifest::DeviceLimits limits{ .MaxWorkgroupSizeY = 1u << 30u };
    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system),
                                      system.GetVulkanApiVersion(),
                                      DeviceStartInfo{ .RequiredLimits = limits });

    EXPECT_EQ(plan.UnmetNeed, "maxComputeWorkGroupSize[1]");
    EXPECT_FALSE(plan.UnmetReason.empty());
}

TEST(RhiSystemStart, AnUnknownRequiredNeedIsNamed)
{
    const std::array<DeviceNeed, 1> capabilities
    {
        DeviceNeed{ .Name = "NotARealCapability", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    DevicePlan plan = rhi::PlanDevice(FirstPhysicalDevice(system), system.GetVulkanApiVersion(), DeviceStartInfo{ .Capabilities = capabilities });

    EXPECT_EQ(plan.UnmetNeed, "NotARealCapability");
    EXPECT_FALSE(plan.UnmetReason.empty());
}

TEST(RhiSystemStart, StartDeviceCreatesTheDeviceWithTheNeeds)
{
    const std::array<DeviceNeed, 4> features
    {
        DeviceNeed{ .Name = "VkPhysicalDeviceVulkan13Features.synchronization2", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "VkPhysicalDeviceVulkan13Features.dynamicRendering", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "VkPhysicalDeviceTimelineSemaphoreFeatures.timelineSemaphore", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "VkPhysicalDeviceShaderObjectFeaturesEXT.shaderObject", .Level = DeviceNeedLevel::Required },
    };
    const std::array<DeviceNeed, 3> capabilities
    {
        DeviceNeed{ .Name = "Int64", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "StorageBuffer16BitAccess", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "GroupNonUniformArithmetic", .Level = DeviceNeedLevel::Wanted },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    const RhiSystemResult started = system.StartDevice(DeviceStartInfo{ .Features = features, .Capabilities = capabilities });

    ASSERT_TRUE(started.Succeeded()) << rhi::ToString(started.Code) << " " << started.Subject << ": " << started.Detail;
    ASSERT_NE(system.GetDevice(), nullptr);
    EXPECT_TRUE(system.GetDevice()->Handle().IsValid());
    EXPECT_TRUE(system.GetDevice()->HasExtension("VK_EXT_shader_object"));
    EXPECT_EQ(system.EnabledFeatures().size(), 4u);

    // the filter answers from the enabled set: Float64 is supported on the device, and was not asked for
    const lodestone::manifest::CapabilitySupportFn supports = &rhi::RhiSystem::SupportsCapabilityFn;

    EXPECT_TRUE(supports(&system, "Int64"));
    EXPECT_TRUE(supports(&system, "GroupNonUniformArithmetic"));
    EXPECT_FALSE(supports(&system, "Float64"));
    // an alias counts as its primary name
    EXPECT_TRUE(supports(&system, "StorageUniformBufferBlock16"));
    EXPECT_GE(system.Limits().MaxInvocationsPerWorkgroup, 128u);
    EXPECT_GT(system.Limits().MaxWorkgroupMemoryBytes, 0u);
}

TEST(RhiSystemStart, StartDeviceNamesTheUnmetNeed)
{
    const std::array<DeviceNeed, 1> extensions
    {
        DeviceNeed{ .Name = "VK_NOT_a_real_extension", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    const RhiSystemResult started = system.StartDevice(DeviceStartInfo{ .Extensions = extensions });

    EXPECT_EQ(started.Code, RhiSystemResultCode::NoDeviceMeetsRequirements);
    EXPECT_EQ(started.Subject, "VK_NOT_a_real_extension");
    EXPECT_FALSE(started.Detail.empty());
    EXPECT_EQ(system.GetDevice(), nullptr);
}

TEST(RhiSystemStart, AWantedNeedThatIsMissingIsSkipped)
{
    const std::array<DeviceNeed, 1> extensions
    {
        DeviceNeed{ .Name = "VK_NOT_a_real_extension", .Level = DeviceNeedLevel::Wanted },
    };
    rhi::RhiSystem system;

    ASSERT_TRUE(StartTestInstance(system));

    const RhiSystemResult started = system.StartDevice(DeviceStartInfo{ .Extensions = extensions });

    ASSERT_TRUE(started.Succeeded()) << started.Subject << ": " << started.Detail;
    EXPECT_FALSE(system.GetDevice()->HasExtension("VK_NOT_a_real_extension"));
}

TEST(RhiSystemStart, StartDeviceNeedsAnInstance)
{
    rhi::RhiSystem system;

    EXPECT_EQ(system.StartDevice(DeviceStartInfo{}).Code, RhiSystemResultCode::InstanceNotStarted);
}

TEST(RhiSystemStart, StartInstanceRefusesAMissingExtension)
{
    const std::array<DeviceNeed, 1> extensions
    {
        DeviceNeed{ .Name = "VK_NOT_a_real_instance_extension", .Level = DeviceNeedLevel::Required },
    };
    rhi::InstanceStartInfo info = TestInstanceInfo();

    info.Extensions = extensions;

    rhi::RhiSystem system;
    const RhiSystemResult started = system.StartInstance(info);

    EXPECT_EQ(started.Code, RhiSystemResultCode::InstanceExtensionMissing);
    EXPECT_EQ(started.Subject, "VK_NOT_a_real_instance_extension");
    EXPECT_EQ(system.GetInstance(), nullptr);
}

namespace
{
    rhi::InstanceStartInfo TestInstanceInfo()
    {
        return rhi::InstanceStartInfo
        {
            .VkVersion = rhi::ApiVersion::Vulkan13,
            .ValidationLevel = rhi::ValidationLayers::Full,
            .ApplicationName = "RhiSystemStartTest",
            .EngineName = "DiamondDogsTestEngine",
            .Extensions = k_DebugUtils,
        };
    }

    ::testing::AssertionResult StartTestInstance(rhi::RhiSystem& system)
    {
        const RhiSystemResult started = system.StartInstance(TestInstanceInfo());

        if (started.Succeeded())
        {
            return ::testing::AssertionSuccess();
        }

        return ::testing::AssertionFailure() << rhi::ToString(started.Code) << " " << started.Subject << ": " << started.Detail;
    }

    const rhi::generated::MemberInfo& Member(std::string_view struct_name, std::string_view member_name)
    {
        const auto matches = [&](const rhi::generated::MemberInfo& row)
        {
            return row.Struct == struct_name && row.Member == member_name;
        };
        const auto found = std::ranges::find_if(rhi::generated::k_Members, matches);

        EXPECT_NE(found, rhi::generated::k_Members.end()) << struct_name << "." << member_name;
        return *found;
    }

    VkPhysicalDevice FirstPhysicalDevice(rhi::RhiSystem& system)
    {
        const VkInstance instance = system.GetInstance()->Handle().As<VkInstance>();
        uint32_t count = 1u;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

        // VK_INCOMPLETE is fine: one device is enough for a plan
        (void)vkEnumeratePhysicalDevices(instance, &count, &physicalDevice);
        return physicalDevice;
    }

    bool Contains(std::span<const std::string> names, std::string_view name)
    {
        return std::ranges::contains(names, name);
    }
}
