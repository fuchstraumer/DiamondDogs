#include "Device.hpp"
#include "Instance.hpp"
#include "RhiSystem.hpp"
#include "RhiSystemStart.hpp"
#include "StartTestRhi.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <string_view>
#include <vulkan/vulkan_core.h>

using rhi::DeviceNeed;
using rhi::DeviceNeedLevel;

namespace
{
    constexpr std::string_view k_ApplicationName{ "RhiSystemTest" };
}

TEST(RhiSystemTest, StartsAtEachApiVersion)
{
    constexpr rhi::ApiVersion k_Versions[]
    {
        rhi::ApiVersion::Vulkan10,
        rhi::ApiVersion::Vulkan11,
        rhi::ApiVersion::Vulkan13,
        rhi::ApiVersion::Latest,
    };

    for (const rhi::ApiVersion version : k_Versions)
    {
        rhi::RhiSystem system;
        const rhi::RhiSystemResult started = test_rhi::StartTestRhi(system, k_ApplicationName, version, rhi::ValidationLayers::BaseOnly, test_rhi::Needs{});

        EXPECT_TRUE(started.Succeeded()) << "version " << static_cast<int>(version) << ": " << started.Subject << ": " << started.Detail;
        EXPECT_NE(system.GetInstance(), nullptr);
        EXPECT_NE(system.GetDevice(), nullptr);
    }
}

TEST(RhiSystemTest, ValidationLevelsReachTheInstance)
{
    constexpr rhi::ValidationLayers k_Levels[]
    {
        rhi::ValidationLayers::None,
        rhi::ValidationLayers::BaseOnly,
        rhi::ValidationLayers::WithSynchronization,
        rhi::ValidationLayers::Full,
    };

    for (const rhi::ValidationLayers level : k_Levels)
    {
        rhi::RhiSystem system;
        const rhi::RhiSystemResult started = test_rhi::StartTestRhi(system, k_ApplicationName, rhi::ApiVersion::Vulkan13, level, test_rhi::Needs{});

        ASSERT_TRUE(started.Succeeded()) << "level " << static_cast<int>(level) << ": " << started.Detail;
        EXPECT_EQ(system.GetInstance()->GetValidationLevel(), level);
    }
}

TEST(RhiSystemTest, WantedExtensionsAreEnabledOnlyWhenSupported)
{
    const std::array<DeviceNeed, 2> instanceExtensions
    {
        DeviceNeed{ .Name = "VK_KHR_get_physical_device_properties2", .Level = DeviceNeedLevel::Wanted },
        DeviceNeed{ .Name = "VK_FAKE_extension_not_real", .Level = DeviceNeedLevel::Wanted },
    };
    const std::array<DeviceNeed, 3> deviceExtensions
    {
        DeviceNeed{ .Name = "VK_KHR_maintenance1", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = "VK_KHR_dedicated_allocation", .Level = DeviceNeedLevel::Wanted },
        DeviceNeed{ .Name = "VK_FAKE_device_extension_not_real", .Level = DeviceNeedLevel::Wanted },
    };
    rhi::RhiSystem system;
    const test_rhi::Needs needs
    {
        .InstanceExtensions = instanceExtensions,
        .DeviceExtensions = deviceExtensions,
    };
    const rhi::RhiSystemResult started = test_rhi::StartTestRhi(system, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::BaseOnly, needs);

    ASSERT_TRUE(started.Succeeded()) << started.Subject << ": " << started.Detail;
    EXPECT_TRUE(system.GetInstance()->HasExtension("VK_EXT_debug_utils"));
    EXPECT_FALSE(system.GetInstance()->HasExtension("VK_FAKE_extension_not_real"));
    EXPECT_TRUE(system.GetDevice()->HasExtension("VK_KHR_maintenance1"));
    EXPECT_TRUE(system.GetDevice()->HasExtension("VK_KHR_dedicated_allocation"));
    EXPECT_FALSE(system.GetDevice()->HasExtension("VK_FAKE_device_extension_not_real"));
}

TEST(RhiSystemTest, PresentationInstanceExtensions)
{
#ifdef _WIN32
    constexpr std::string_view k_PlatformSurface{ "VK_KHR_win32_surface" };
#elif defined(__linux__)
    constexpr std::string_view k_PlatformSurface{ "VK_KHR_xcb_surface" };
#endif

    const std::array<DeviceNeed, 2> instanceExtensions
    {
        DeviceNeed{ .Name = "VK_KHR_surface", .Level = DeviceNeedLevel::Required },
        DeviceNeed{ .Name = k_PlatformSurface, .Level = DeviceNeedLevel::Required },
    };
    const std::array<DeviceNeed, 1> deviceExtensions
    {
        DeviceNeed{ .Name = "VK_KHR_swapchain", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;
    const test_rhi::Needs needs
    {
        .InstanceExtensions = instanceExtensions,
        .DeviceExtensions = deviceExtensions,
    };
    const rhi::RhiSystemResult started = test_rhi::StartTestRhi(system, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::BaseOnly, needs);

    ASSERT_TRUE(started.Succeeded()) << started.Subject << ": " << started.Detail;
    EXPECT_TRUE(system.GetInstance()->HasExtension("VK_KHR_surface"));
    EXPECT_TRUE(system.GetInstance()->HasExtension(k_PlatformSurface));
    EXPECT_TRUE(system.GetDevice()->HasExtension("VK_KHR_swapchain"));
}

// an extension that is core in the device's version is available without its name in the enabled list
TEST(RhiSystemTest, ACoreExtensionCountsAsPresent)
{
    const std::array<DeviceNeed, 1> deviceExtensions
    {
        DeviceNeed{ .Name = "VK_KHR_dynamic_rendering", .Level = DeviceNeedLevel::Wanted },
    };
    rhi::RhiSystem system;
    const test_rhi::Needs needs{ .DeviceExtensions = deviceExtensions };
    const rhi::RhiSystemResult started = test_rhi::StartTestRhi(system, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::BaseOnly, needs);

    ASSERT_TRUE(started.Succeeded()) << started.Subject << ": " << started.Detail;
    EXPECT_TRUE(system.GetDevice()->HasExtension("VK_KHR_dynamic_rendering"));
}

TEST(RhiSystemTest, IsFeatureEnabledAnswersFromTheEnabledSet)
{
    const std::array<DeviceNeed, 1> features
    {
        DeviceNeed{ .Name = "VkPhysicalDeviceVulkan12Features.timelineSemaphore", .Level = DeviceNeedLevel::Required },
    };
    rhi::RhiSystem system;
    const test_rhi::Needs needs{ .Features = features };
    const rhi::RhiSystemResult started = test_rhi::StartTestRhi(system, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::BaseOnly, needs);

    ASSERT_TRUE(started.Succeeded()) << started.Subject << ": " << started.Detail;
    EXPECT_TRUE(system.GetDevice()->IsFeatureEnabled("VkPhysicalDeviceVulkan12Features.timelineSemaphore"));
    // every Vulkan 1.3 device supports synchronization2, and no need asked for it
    EXPECT_FALSE(system.GetDevice()->IsFeatureEnabled("VkPhysicalDeviceVulkan13Features.synchronization2"));
}

// each device names its own objects: the name function and the device handle are not globals
TEST(RhiSystemTest, TwoDevicesNameTheirOwnObjects)
{
    rhi::RhiSystem first;
    rhi::RhiSystem second;
    const rhi::RhiSystemResult firstStarted = test_rhi::StartTestRhi(first, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::Full, test_rhi::Needs{});
    const rhi::RhiSystemResult secondStarted = test_rhi::StartTestRhi(second, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::Full, test_rhi::Needs{});

    ASSERT_TRUE(firstStarted.Succeeded()) << firstStarted.Detail;
    ASSERT_TRUE(secondStarted.Succeeded()) << secondStarted.Detail;

    const rhi::Device& firstDevice = *first.GetDevice();
    const rhi::Device& secondDevice = *second.GetDevice();

    // with a global device handle, the last device to start named every object, so naming an object of the first
    // device was a validation error
    EXPECT_EQ(firstDevice.SetObjectName(VK_OBJECT_TYPE_DEVICE, firstDevice.Handle().As<uint64_t>(), "FirstDevice"), VK_SUCCESS);
    EXPECT_EQ(secondDevice.SetObjectName(VK_OBJECT_TYPE_DEVICE, secondDevice.Handle().As<uint64_t>(), "SecondDevice"), VK_SUCCESS);
}

TEST(RhiSystemTest, NamingWithoutDebugUtilsReportsNotPresent)
{
    rhi::RhiSystem system;
    const rhi::InstanceStartInfo instanceInfo
    {
        .VkVersion = rhi::ApiVersion::Vulkan13,
        .ValidationLevel = rhi::ValidationLayers::None,
        .ApplicationName = k_ApplicationName,
        .EngineName = "DiamondDogsTestEngine",
    };

    ASSERT_TRUE(system.StartInstance(instanceInfo).Succeeded());
    ASSERT_TRUE(system.StartDevice(rhi::DeviceStartInfo{}).Succeeded());

    const rhi::Device& device = *system.GetDevice();

    EXPECT_EQ(device.SetObjectName(VK_OBJECT_TYPE_DEVICE, device.Handle().As<uint64_t>(), "Unnamed"), VK_ERROR_FEATURE_NOT_PRESENT);
}

// Destroy used to dereference the device unconditionally, so the destructor crashed after an explicit Destroy
TEST(RhiSystemTest, DestroyTwiceIsSafe)
{
    rhi::RhiSystem system;

    ASSERT_TRUE(test_rhi::StartTestRhi(system, k_ApplicationName, rhi::ApiVersion::Vulkan13, rhi::ValidationLayers::BaseOnly, test_rhi::Needs{}).Succeeded());

    system.Destroy();

    EXPECT_EQ(system.GetDevice(), nullptr);
    EXPECT_EQ(system.GetInstance(), nullptr);
    EXPECT_NO_THROW(system.Destroy());
}
