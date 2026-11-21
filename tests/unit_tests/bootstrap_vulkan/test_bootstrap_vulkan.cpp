#include "BootResult.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "DeviceStage.hpp"
#include "GpuStage.hpp"
#include "Instance.hpp"
#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"
#include "ResourceContext.hpp"
#include "RhiResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiSystem.hpp"
#include "SpineNeeds.hpp"
#include "Swapchain.hpp"
#include "WindowStage.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using bootstrap::BootResult;
using bootstrap::BootResultCode;
using bootstrap::ContentStage;
using bootstrap::CoreStage;
using bootstrap::DeviceConfig;
using bootstrap::DeviceRequirements;
using bootstrap::DeviceStage;
using bootstrap::GpuStage;
using bootstrap::LaunchStage;
using bootstrap::ModuleDescriptor;
using bootstrap::Need;
using bootstrap::Stage;

namespace
{
    constexpr uint64_t k_TimeoutNs{ 5'000'000'000u };
    constexpr std::string_view k_TimelineFeature{ "VkPhysicalDeviceVulkan12Features.timelineSemaphore" };
    constexpr std::string_view k_Sync2Feature{ "VkPhysicalDeviceVulkan13Features.synchronization2" };
    constexpr std::string_view k_MemoryBudget{ "VK_EXT_memory_budget" };

    struct CoreObject
    {
        int32_t Value{ 11 };
    };

    struct GpuObject
    {
        CoreObject* Dependency{ nullptr };
        ResourceContext* Resources{ nullptr };
    };

    const bootstrap::ModuleKey<CoreObject> k_CoreObjectKey{ "core_object" };
    const bootstrap::ModuleKey<GpuObject> k_GpuObjectKey{ "gpu_object" };
    constexpr std::string_view k_GpuObjectDependencies[]{ "core_object" };
    constexpr std::string_view k_MissingExtension[]{ "VK_DD_not_an_extension" };

    [[nodiscard]] BootResult StartCoreObject(const bootstrap::CoreStartContext& context, bootstrap::ModuleInstance& instance);
    [[nodiscard]] BootResult StartGpuObject(const bootstrap::GpuStartContext& context, bootstrap::ModuleInstance& instance);

    const ModuleDescriptor k_CoreObjectModule
    {
        .Name = "core_object",
        .StartCore = &StartCoreObject,
    };

    const ModuleDescriptor k_GpuObjectModule
    {
        .Name = "gpu_object",
        .DependsOn = k_GpuObjectDependencies,
        .StartGpu = &StartGpuObject,
    };

    // a module whose Required extension no device has: the failure must name the module
    const ModuleDescriptor k_MissingExtensionModule
    {
        .Name = "missing_extension",
        .Needs = bootstrap::ModuleNeeds{ .RequiredDeviceExtensions = k_MissingExtension },
        .StartGpu = &StartGpuObject,
    };

    /** An argv for LaunchStage::Start. */
    class TestArguments
    {
    public:
        explicit TestArguments(std::initializer_list<std::string_view> values);
        TestArguments(const TestArguments&) = delete;
        TestArguments& operator=(const TestArguments&) = delete;

        [[nodiscard]] std::span<char* const> Span() const noexcept;

    private:
        std::vector<std::string> storage;
        std::vector<char*> pointers;
    };

    /** The launch, core and content stages, with no bundles. */
    class BootedContent
    {
    public:
        BootedContent(std::span<const ModuleDescriptor> linked, std::initializer_list<std::string_view> arguments);

        TestArguments arguments;
        LaunchStage launch;
        CoreStage core;
        ContentStage content;
    };

    [[nodiscard]] DeviceConfig ValidatedConfig();
    [[nodiscard]] std::filesystem::path WriteConfig(std::string_view file_name, std::string_view text);
    [[nodiscard]] bool Contains(std::span<const std::string> names, std::string_view name);
    // a device-local upload goes through the transfer queue and its timeline: the spine needs at work
    void ExpectUploadCompletes(ResourceContext& resources);
}

TEST(DeviceConfig, ReadsEveryKey)
{
    const std::filesystem::path path = WriteConfig("bootstrap_vulkan_device.json", R"({
        "Device": { "ApiVersion": "1.4", "Validation": "Full", "ApplicationName": "Test", "Declined": [ "A", "B" ] }
    })");
    const std::string configArgument = "--config=" + path.string();
    const BootedContent booted(std::span<const ModuleDescriptor>{}, { configArgument });
    DeviceConfig config;
    const BootResult read = bootstrap::ReadDeviceConfig(booted.launch, config);

    ASSERT_TRUE(read.Succeeded()) << bootstrap::FormatFailure(read);
    EXPECT_EQ(config.ApiVersion, rhi::ApiVersion::Vulkan14);
    EXPECT_EQ(config.Validation, rhi::ValidationLayers::Full);
    EXPECT_EQ(config.ApplicationName, "Test");
    EXPECT_EQ(config.Declined, (std::vector<std::string>{ "A", "B" }));
}

TEST(DeviceConfig, NoSectionKeepsTheValues)
{
    const std::filesystem::path path = WriteConfig("bootstrap_vulkan_empty.json", R"({ "Profiles": {} })");
    const std::string configArgument = "--config=" + path.string();
    const BootedContent booted(std::span<const ModuleDescriptor>{}, { configArgument });
    DeviceConfig config;

    config.Validation = rhi::ValidationLayers::BaseOnly;

    const BootResult read = bootstrap::ReadDeviceConfig(booted.launch, config);

    ASSERT_TRUE(read.Succeeded()) << bootstrap::FormatFailure(read);
    EXPECT_EQ(config.Validation, rhi::ValidationLayers::BaseOnly);
    EXPECT_EQ(config.ApiVersion, rhi::ApiVersion::Vulkan13);
}

TEST(DeviceConfig, RefusesAnUnknownKeyAndABadVersion)
{
    const std::filesystem::path unknownPath = WriteConfig("bootstrap_vulkan_unknown.json", R"({ "Device": { "Validaton": "Full" } })");
    const std::string unknownArgument = "--config=" + unknownPath.string();
    const BootedContent unknownBoot(std::span<const ModuleDescriptor>{}, { unknownArgument });
    DeviceConfig config;
    const BootResult unknown = bootstrap::ReadDeviceConfig(unknownBoot.launch, config);

    EXPECT_EQ(unknown.Code, BootResultCode::ConfigInvalid);
    EXPECT_EQ(unknown.Subject, "Validaton");

    const std::filesystem::path versionPath = WriteConfig("bootstrap_vulkan_version.json", R"({ "Device": { "ApiVersion": "1.2.0" } })");
    const std::string versionArgument = "--config=" + versionPath.string();
    const BootedContent versionBoot(std::span<const ModuleDescriptor>{}, { versionArgument });
    const BootResult version = bootstrap::ReadDeviceConfig(versionBoot.launch, config);

    EXPECT_EQ(version.Code, BootResultCode::ConfigInvalid);
    EXPECT_EQ(version.Subject, "ApiVersion");
}

TEST(BootstrapVulkan, HeadlessBootEnablesOnlyTheSpineAndUploads)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceStage device;
    const BootResult deviceStarted = device.StartHeadless(booted.content, requirements, ValidatedConfig());

    ASSERT_TRUE(deviceStarted.Succeeded()) << bootstrap::FormatFailure(deviceStarted);

    EXPECT_TRUE(device.Rhi().GetDevice()->IsFeatureEnabled(k_TimelineFeature));
    EXPECT_TRUE(device.Rhi().GetDevice()->IsFeatureEnabled(k_Sync2Feature));
    // no need names it: the device stage asks for it, so validation reaches our messenger and objects get names
    EXPECT_TRUE(device.Rhi().GetInstance()->HasExtension("VK_EXT_debug_utils"));

    GpuStage gpu;
    const BootResult gpuStarted = gpu.StartHeadless(device);

    ASSERT_TRUE(gpuStarted.Succeeded()) << bootstrap::FormatFailure(gpuStarted);
    EXPECT_FALSE(gpu.HasWindow());
    ExpectUploadCompletes(gpu.Resources());
}

TEST(BootstrapVulkan, ADeclinedWantedExtensionIsLeftOff)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceStage wanted;
    const BootResult wantedStarted = wanted.StartHeadless(booted.content, requirements, ValidatedConfig());

    ASSERT_TRUE(wantedStarted.Succeeded()) << bootstrap::FormatFailure(wantedStarted);

    if (!wanted.Rhi().GetDevice()->HasExtension(k_MemoryBudget))
    {
        GTEST_SKIP() << "this device has no VK_EXT_memory_budget, so declining it proves nothing";
    }

    wanted.Stop();

    DeviceConfig config = ValidatedConfig();

    config.Declined.emplace_back(k_MemoryBudget);

    DeviceStage declined;
    const BootResult declinedStarted = declined.StartHeadless(booted.content, requirements, config);

    ASSERT_TRUE(declinedStarted.Succeeded()) << bootstrap::FormatFailure(declinedStarted);
    EXPECT_FALSE(declined.Rhi().GetDevice()->HasExtension(k_MemoryBudget));
}

TEST(BootstrapVulkan, DecliningDebugUtilsLeavesItOff)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceConfig config = ValidatedConfig();

    config.Declined.emplace_back("VK_EXT_debug_utils");

    DeviceStage device;
    const BootResult deviceStarted = device.StartHeadless(booted.content, requirements, config);

    ASSERT_TRUE(deviceStarted.Succeeded()) << bootstrap::FormatFailure(deviceStarted);
    EXPECT_FALSE(device.Rhi().GetInstance()->HasExtension("VK_EXT_debug_utils"));
}

TEST(BootstrapVulkan, ARequiredLimitAboveEveryDeviceFails)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);

    requirements.RequiredLimits.MaxPushConstantBytes = 1u << 30u;

    DeviceStage device;
    const BootResult started = device.StartHeadless(booted.content, requirements, ValidatedConfig());

    EXPECT_EQ(started.Code, BootResultCode::NoDeviceMeetsRequirements);
    EXPECT_EQ(started.FailedStage, Stage::Device);
    EXPECT_EQ(started.Subject, "maxPushConstantsSize");
    EXPECT_FALSE(started.Detail.empty());
    EXPECT_FALSE(device.Started());
}

TEST(BootstrapVulkan, AnUnmetModuleNeedNamesTheModule)
{
    const ModuleDescriptor linked[]{ k_MissingExtensionModule };
    const BootedContent booted(linked, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceStage device;
    const BootResult started = device.StartHeadless(booted.content, requirements, ValidatedConfig());

    EXPECT_EQ(started.Code, BootResultCode::NoDeviceMeetsRequirements);
    EXPECT_EQ(started.Subject, "VK_DD_not_an_extension");
    EXPECT_EQ(started.Source, "missing_extension");
}

TEST(BootstrapVulkan, AGpuModuleStartsAndRequiresACoreModule)
{
    const ModuleDescriptor linked[]{ k_CoreObjectModule, k_GpuObjectModule };
    const BootedContent booted(linked, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceStage device;
    const BootResult deviceStarted = device.StartHeadless(booted.content, requirements, ValidatedConfig());

    ASSERT_TRUE(deviceStarted.Succeeded()) << bootstrap::FormatFailure(deviceStarted);

    GpuStage gpu;
    const BootResult gpuStarted = gpu.StartHeadless(device);

    ASSERT_TRUE(gpuStarted.Succeeded()) << bootstrap::FormatFailure(gpuStarted);
    ASSERT_EQ(gpu.Modules().size(), 1u);

    const GpuObject* object = static_cast<const GpuObject*>(gpu.FindModuleObject("gpu_object"));

    ASSERT_NE(object, nullptr);
    ASSERT_NE(object->Dependency, nullptr);
    EXPECT_EQ(object->Dependency->Value, 11);
    EXPECT_EQ(object->Resources, &gpu.Resources());
}

TEST(BootstrapVulkan, StopThenStartAgainGivesAWorkingDevice)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const std::vector<Need> spine = bootstrap::HeadlessSpineNeeds();
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    const DeviceConfig config = ValidatedConfig();
    DeviceStage device;
    GpuStage gpu;

    ASSERT_TRUE(device.StartHeadless(booted.content, requirements, config).Succeeded());
    ASSERT_TRUE(gpu.StartHeadless(device).Succeeded());

    // the recovery path: stop the GPU and device stages, then start them from the same requirements
    gpu.Stop();
    device.Stop();

    const BootResult deviceRestarted = device.StartHeadless(booted.content, requirements, config);

    ASSERT_TRUE(deviceRestarted.Succeeded()) << bootstrap::FormatFailure(deviceRestarted);

    const BootResult gpuRestarted = gpu.StartHeadless(device);

    ASSERT_TRUE(gpuRestarted.Succeeded()) << bootstrap::FormatFailure(gpuRestarted);
    ExpectUploadCompletes(gpu.Resources());
}

TEST(BootstrapVulkan, WindowedBootCreatesTheSurfaceBeforeTheDevice)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const PlatformWindowCreateInfo windowInfo
    {
        .WindowName = "BootstrapVulkanTest",
        .DisplayToUse = nullptr,
        .InitialWidth = 320u,
        .InitialHeight = 240u,
    };
    bootstrap::WindowStage window;
    const BootResult windowStarted = window.Start(booted.content, windowInfo);

    ASSERT_TRUE(windowStarted.Succeeded()) << bootstrap::FormatFailure(windowStarted);
    EXPECT_FALSE(window.Needs().empty());

    const std::vector<Need> spine = bootstrap::SpineNeeds(window);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceStage device;
    const BootResult deviceStarted = device.Start(booted.content, requirements, ValidatedConfig(), window);

    ASSERT_TRUE(deviceStarted.Succeeded()) << bootstrap::FormatFailure(deviceStarted);
    EXPECT_NE(window.Platform().GetSurfaceHandle(), 0u);
    EXPECT_TRUE(device.Rhi().GetDevice()->HasExtension("VK_KHR_swapchain"));

    GpuStage gpu;
    const BootResult gpuStarted = gpu.Start(device, window);

    ASSERT_TRUE(gpuStarted.Succeeded()) << bootstrap::FormatFailure(gpuStarted);
    EXPECT_TRUE(gpu.HasWindow());
    EXPECT_NE(window.Platform().GetActiveSwapchain(), nullptr);

    // the stages stop in reverse order: swapchain, surface, device, window
    gpu.Stop();
    device.Stop();
    EXPECT_EQ(window.Platform().GetSurfaceHandle(), 0u);
}

// an HDR color space needs VK_EXT_swapchain_colorspace. Declined, the swapchain must be SDR even when the system has
// HDR on, or swapchain creation is a validation error
TEST(BootstrapVulkan, WithoutTheColorSpaceExtensionTheSwapchainIsSdr)
{
    const BootedContent booted(std::span<const ModuleDescriptor>{}, {});
    const PlatformWindowCreateInfo windowInfo
    {
        .WindowName = "BootstrapVulkanTest",
        .DisplayToUse = nullptr,
        .InitialWidth = 320u,
        .InitialHeight = 240u,
    };
    bootstrap::WindowStage window;
    const BootResult windowStarted = window.Start(booted.content, windowInfo);

    ASSERT_TRUE(windowStarted.Succeeded()) << bootstrap::FormatFailure(windowStarted);

    const std::vector<Need> spine = bootstrap::SpineNeeds(window);
    const DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(booted.content, spine);
    DeviceConfig config = ValidatedConfig();

    config.Declined.emplace_back("VK_EXT_swapchain_colorspace");

    DeviceStage device;
    const BootResult deviceStarted = device.Start(booted.content, requirements, config, window);

    ASSERT_TRUE(deviceStarted.Succeeded()) << bootstrap::FormatFailure(deviceStarted);
    EXPECT_FALSE(device.Rhi().GetInstance()->HasExtension("VK_EXT_swapchain_colorspace"));

    GpuStage gpu;
    const BootResult gpuStarted = gpu.Start(device, window);

    ASSERT_TRUE(gpuStarted.Succeeded()) << bootstrap::FormatFailure(gpuStarted);
    ASSERT_NE(window.Platform().GetActiveSwapchain(), nullptr);
    EXPECT_EQ(window.Platform().GetActiveSwapchain()->ColorSpace(), ColorSpace::sRGB_Nonlinear);
}

namespace
{
    BootResult StartCoreObject(const bootstrap::CoreStartContext&, bootstrap::ModuleInstance& instance)
    {
        instance.Own(k_CoreObjectKey, std::make_unique<CoreObject>());
        return bootstrap::SuccessIn(Stage::Core);
    }

    BootResult StartGpuObject(const bootstrap::GpuStartContext& context, bootstrap::ModuleInstance& instance)
    {
        BootResult result = bootstrap::SuccessIn(Stage::Gpu);
        CoreObject* dependency = context.Require(k_CoreObjectKey, result);

        if (dependency == nullptr)
        {
            return result;
        }

        std::unique_ptr<GpuObject> object = std::make_unique<GpuObject>();

        object->Dependency = dependency;
        object->Resources = &context.Gpu().Resources();
        instance.Own(k_GpuObjectKey, std::move(object));
        return result;
    }

    TestArguments::TestArguments(std::initializer_list<std::string_view> values)
    {
        storage.emplace_back("bootstrap_vulkan_tests.exe");

        for (const std::string_view value : values)
        {
            storage.emplace_back(value);
        }

        for (std::string& argument : storage)
        {
            pointers.push_back(argument.data());
        }
    }

    std::span<char* const> TestArguments::Span() const noexcept
    {
        return pointers;
    }

    BootedContent::BootedContent(std::span<const ModuleDescriptor> linked, std::initializer_list<std::string_view> _arguments) :
        arguments(_arguments)
    {
        const BootResult launched = launch.Start(arguments.Span(), linked, Stage::Gpu);

        EXPECT_TRUE(launched.Succeeded()) << bootstrap::FormatFailure(launched);

        const BootResult coreStarted = core.Start(launch);

        EXPECT_TRUE(coreStarted.Succeeded()) << bootstrap::FormatFailure(coreStarted);

        const BootResult contentStarted = content.Start(core, std::span<const bootstrap::BundleRequest>{});

        EXPECT_TRUE(contentStarted.Succeeded()) << bootstrap::FormatFailure(contentStarted);
    }

    DeviceConfig ValidatedConfig()
    {
        DeviceConfig config;

        config.Validation = rhi::ValidationLayers::Full;
        config.ApplicationName = "bootstrap_vulkan_tests";
        return config;
    }

    std::filesystem::path WriteConfig(std::string_view file_name, std::string_view text)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return path;
    }

    bool Contains(std::span<const std::string> names, std::string_view name)
    {
        return std::ranges::contains(names, name);
    }

    void ExpectUploadCompletes(ResourceContext& resources)
    {
        std::vector<uint32_t> words(1024u);

        for (size_t index = 0u; index < words.size(); ++index)
        {
            words[index] = static_cast<uint32_t>(index) * 2654435761u;
        }

        const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
        BufferCreateInfo info{};

        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferDestination;
        info.Size = data.Data.size();

        rhi::ResourceFuture<GraphicsResource> reply = resources.CreateBuffer(info, std::span{ &data, 1u });

        ASSERT_EQ(resources.Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);

        const GraphicsResource buffer = reply.Get();

        ASSERT_TRUE(static_cast<bool>(buffer));

        rhi::OperationFuture destroyed = resources.DestroyResource(buffer);

        EXPECT_EQ(resources.Wait(destroyed, k_TimeoutNs), rhi::FutureState::Complete);
    }
}
