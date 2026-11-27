#include "WebGpuDeviceStage.hpp"
#include "BootResult.hpp"
#include "DeviceCapability.hpp"
#include "DeviceConfig.hpp"
#include "DeviceFilter.hpp"
#include "DeviceRequirements.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"
#include "RhiFlags.hpp"
#include "ShaderManifestDevice.hpp"
#include "WebGpuCapabilities.hpp"
#include "webgpu/webgpu.h"
#include "webgpu/webgpu_cpp.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{

// the features to request, and what the device has once it exists
struct WebGpuPlan
{
    std::vector<wgpu::FeatureName> Features;
    std::shared_ptr<bootstrap::WebGpuEnabledSet> Enabled;
};

wgpu::PowerPreference ToWgpuPowerPreference(bootstrap::PowerPreference preference) noexcept;
std::string CopyStringView(wgpu::StringView view);
wgpu::TextureFormat SelectSurfaceFormat(const wgpu::SurfaceCapabilities& capabilities,
                                          wgpu::TextureFormat preferred) noexcept;
// fills `plan` from the requirements and the adapter. Returns the first need or limit that the adapter cannot meet
[[nodiscard]] std::optional<bootstrap::BootResult> PlanDevice(const wgpu::Adapter& adapter,
                                                              const bootstrap::DeviceRequirements& requirements,
                                                              const bootstrap::DeviceConfig& config,
                                                              WebGpuPlan& plan);
[[nodiscard]] std::optional<bootstrap::BootResult> CheckLimits(const lodestone::manifest::DeviceLimits& required,
                                                               const wgpu::Limits& adapter_limits,
                                                               std::string_view adapter_name);
[[nodiscard]] bool SupportsWgslName(void* user, std::string_view capability_name) noexcept;
[[nodiscard]] std::string AdapterName(const wgpu::Adapter& adapter);
[[nodiscard]] bootstrap::BootResult DeviceFailure(bootstrap::BootResultCode code, std::string subject, std::string source, std::string detail);
[[nodiscard]] bootstrap::BootResult MissingNeed(const bootstrap::Need& need, std::string detail);
void BeginDeviceRequest(bootstrap::WebGpuBootState& state,
                        const bootstrap::WebGpuStageRequestingAdapter& current);
void FinalizeSurfaceAndContext(bootstrap::WebGpuBootState& state,
                               const bootstrap::WebGpuStageRequestingDevice& current);

} // namespace

namespace bootstrap
{

struct AdapterRequestState
{
    bool Done{ false };
    bool Succeeded{ false };
    wgpu::Adapter Result;
    std::string Message;
};

struct DeviceRequestState
{
    bool Done{ false };
    bool Succeeded{ false };
    wgpu::Device Result;
    std::string Message;
};

WebGpuDeviceStage::WebGpuDeviceStage() noexcept = default;

WebGpuDeviceStage::~WebGpuDeviceStage() = default;

WebGpuDeviceStage::WebGpuDeviceStage(WebGpuDeviceStage&& other) noexcept = default;

WebGpuDeviceStage& WebGpuDeviceStage::operator=(WebGpuDeviceStage&& other) noexcept = default;

void WebGpuDeviceStage::Start(const DeviceRequirements& requirements, const DeviceConfig& config, const WebGpuSurfaceConfig& surface_config)
{
    start(requirements, config, surface_config);
}

void WebGpuDeviceStage::StartHeadless(const DeviceRequirements& requirements, const DeviceConfig& config)
{
    start(requirements, config, std::nullopt);
}

void WebGpuDeviceStage::start(const DeviceRequirements& requirements, const DeviceConfig& config, std::optional<WebGpuSurfaceConfig> surface_config)
{
    Stop();

    wgpu::Instance instance = wgpu::CreateInstance(nullptr);

    if (!instance)
    {
        state = WebGpuStageFailed{
            DeviceFailure(BootResultCode::InstanceCreateFailed, "", "", "wgpu::CreateInstance returned null")
        };
        return;
    }

    std::shared_ptr<AdapterRequestState> request = std::make_shared<AdapterRequestState>();
    wgpu::RequestAdapterOptions options{};
    options.powerPreference = ToWgpuPowerPreference(config.Power);

    state = WebGpuStageRequestingAdapter{
        instance,
        config,
        requirements,
        std::move(surface_config),
        request
    };

    instance.RequestAdapter(
        &options,
        wgpu::CallbackMode::AllowSpontaneous,
        [request](wgpu::RequestAdapterStatus status,
                  wgpu::Adapter adapter,
                  wgpu::StringView message)
        {
            request->Succeeded = (status == wgpu::RequestAdapterStatus::Success) && static_cast<bool>(adapter);
            request->Result = std::move(adapter);
            request->Message = CopyStringView(message);
            request->Done = true;
        });
}

void WebGpuDeviceStage::Step()
{
    if (const WebGpuStageRequestingAdapter* requestingAdapter = std::get_if<WebGpuStageRequestingAdapter>(&state))
    {
        const WebGpuStageRequestingAdapter current = *requestingAdapter;
        current.Instance.ProcessEvents();

        if (!current.Request->Done)
        {
            return;
        }

        if (!current.Request->Succeeded)
        {
            state = WebGpuStageFailed{
                DeviceFailure(BootResultCode::AdapterRequestFailed, "", "", current.Request->Message)
            };
            return;
        }

        BeginDeviceRequest(state, current);
        return;
    }

    if (const WebGpuStageRequestingDevice* requestingDevice = std::get_if<WebGpuStageRequestingDevice>(&state))
    {
        const WebGpuStageRequestingDevice current = *requestingDevice;
        current.Instance.ProcessEvents();

        if (!current.Request->Done)
        {
            return;
        }

        if (!current.Request->Succeeded)
        {
            state = WebGpuStageFailed{
                DeviceFailure(BootResultCode::DeviceCreateFailed, "", "", current.Request->Message)
            };
            return;
        }

        FinalizeSurfaceAndContext(state, current);
    }
}

void WebGpuDeviceStage::Stop() noexcept
{
    state = WebGpuStageUnstarted{};
}

bool WebGpuDeviceStage::IsReady() const noexcept
{
    return std::holds_alternative<WebGpuStageReady>(state);
}

bool WebGpuDeviceStage::HasFailed() const noexcept
{
    return std::holds_alternative<WebGpuStageFailed>(state);
}

const WebGpuBootState& WebGpuDeviceStage::State() const noexcept
{
    return state;
}

const rhi::RhiContext& WebGpuDeviceStage::Context() const noexcept
{
    if (const WebGpuStageReady* ready = std::get_if<WebGpuStageReady>(&state))
    {
        return ready->Context;
    }

    static const rhi::RhiContext emptyContext{};
    return emptyContext;
}

const wgpu::Surface& WebGpuDeviceStage::Surface() const noexcept
{
    if (const WebGpuStageReady* ready = std::get_if<WebGpuStageReady>(&state))
    {
        return ready->Surface;
    }

    static const wgpu::Surface emptySurface{};
    return emptySurface;
}

wgpu::TextureFormat WebGpuDeviceStage::SurfaceFormat() const noexcept
{
    if (const WebGpuStageReady* ready = std::get_if<WebGpuStageReady>(&state))
    {
        return ready->SurfaceFormat;
    }

    return wgpu::TextureFormat::Undefined;
}

DeviceFilter WebGpuDeviceStage::Filter() const noexcept
{
    const WebGpuStageReady* ready = std::get_if<WebGpuStageReady>(&state);

    if (ready == nullptr)
    {
        return DeviceFilter{};
    }

    // the set is const, and the supports function only reads it
    return DeviceFilter
    {
        .Supports = &SupportsWgslName,
        .User = const_cast<WebGpuEnabledSet*>(ready->Enabled.get()),
        .Limits = ready->Enabled->Limits,
        .Capabilities = ready->Enabled->Capabilities,
    };
}

const BootResult& WebGpuDeviceStage::Failure() const noexcept
{
    if (const WebGpuStageFailed* failed = std::get_if<WebGpuStageFailed>(&state))
    {
        return failed->Result;
    }

    static const BootResult success = SuccessIn(Stage::Device);
    return success;
}

} // namespace bootstrap

namespace
{

wgpu::PowerPreference ToWgpuPowerPreference(bootstrap::PowerPreference preference) noexcept
{
    switch (preference)
    {
    case bootstrap::PowerPreference::LowPower:
        return wgpu::PowerPreference::LowPower;
    default:
        // the device gets the adapter's own limits, so the default must be the adapter with the high limits
        return wgpu::PowerPreference::HighPerformance;
    }
}

std::string CopyStringView(wgpu::StringView view)
{
    if (view.data == nullptr || view.length == 0u)
    {
        return std::string{};
    }

    if (view.length == WGPU_STRLEN)
    {
        return std::string{ view.data };
    }

    return std::string{ view.data, view.length };
}

wgpu::TextureFormat SelectSurfaceFormat(const wgpu::SurfaceCapabilities& capabilities,
                                          wgpu::TextureFormat preferred) noexcept
{
    for (std::size_t index = 0u; index < capabilities.formatCount; ++index)
    {
        if (capabilities.formats[index] == preferred)
        {
            return preferred;
        }
    }

    return capabilities.formats[0];
}

std::optional<bootstrap::BootResult> PlanDevice(const wgpu::Adapter& adapter,
                                                const bootstrap::DeviceRequirements& requirements,
                                                const bootstrap::DeviceConfig& config,
                                                WebGpuPlan& plan)
{
    const std::string adapterName = AdapterName(adapter);
    std::optional<bootstrap::BootResult> failure;

    plan.Enabled = std::make_shared<bootstrap::WebGpuEnabledSet>();

    // a Wanted need that the config declines is left off. Declined has no effect on a Required need
    const auto declined = [&config](const bootstrap::Need& need)
    {
        return need.Level == bootstrap::NeedLevel::Wanted && std::ranges::contains(config.Declined, need.Name);
    };
    // true when the adapter has every feature; a Required need that it lacks sets the failure
    const auto request = [&](const bootstrap::Need& need, std::span<const wgpu::FeatureName> features)
    {
        for (const wgpu::FeatureName feature : features)
        {
            if (!adapter.HasFeature(feature))
            {
                if (need.Level == bootstrap::NeedLevel::Required && !failure.has_value())
                {
                    failure = MissingNeed(need, std::format("{}: no WebGPU feature {}", adapterName, bootstrap::NameOf(feature)));
                }

                return false;
            }
        }

        for (const wgpu::FeatureName feature : features)
        {
            if (!std::ranges::contains(plan.Features, feature))
            {
                plan.Features.push_back(feature);
            }
        }

        return true;
    };
    // a need that WebGPU has no name for: a Required one fails, a Wanted one is skipped
    const auto unknown = [&](const bootstrap::Need& need, std::string_view reason)
    {
        if (need.Level == bootstrap::NeedLevel::Required && !failure.has_value())
        {
            failure = MissingNeed(need, std::string{ reason });
        }
    };

    for (const std::span<const bootstrap::Need> extensions : { std::span<const bootstrap::Need>{ requirements.InstanceExtensions },
                                                               std::span<const bootstrap::Need>{ requirements.DeviceExtensions } })
    {
        for (const bootstrap::Need& need : extensions)
        {
            unknown(need, "a Vulkan extension: WebGPU has no extensions");
        }
    }

    for (const bootstrap::Need& need : requirements.DeviceFeatures)
    {
        const std::optional<wgpu::FeatureName> feature = bootstrap::WebGpuFeatureNamed(need.Name);

        if (!feature.has_value())
        {
            unknown(need, "not a WebGPU feature name");
            continue;
        }

        if (!declined(need))
        {
            (void)request(need, std::span{ &*feature, 1u });
        }
    }

    for (const bootstrap::Need& need : requirements.Capabilities)
    {
        const std::optional<wgpu::FeatureName> feature = bootstrap::WebGpuFeatureOfWgslEnable(need.Name);

        if (!feature.has_value())
        {
            unknown(need, "no WebGPU feature enables this WGSL name");
            continue;
        }

        if (!declined(need) && request(need, std::span{ &*feature, 1u }))
        {
            plan.Enabled->CapabilityNames.emplace_back(need.Name);
        }
    }

    for (const bootstrap::Need& need : requirements.NeutralCapabilities)
    {
        const std::span<const wgpu::FeatureName> features = bootstrap::WebGpuFeaturesOf(need.Neutral);

        if (features.empty())
        {
            unknown(need, "WebGPU cannot provide this capability");
            continue;
        }

        if (!declined(need) && request(need, features))
        {
            plan.Enabled->Capabilities.push_back(need.Neutral);
        }
    }

    if (failure.has_value())
    {
        return failure;
    }

    wgpu::Limits adapterLimits{};

    if (adapter.GetLimits(&adapterLimits) != wgpu::Status::Success)
    {
        return DeviceFailure(bootstrap::BootResultCode::DeviceCreateFailed, "", "", std::format("{}: GetLimits failed", adapterName));
    }

    std::optional<bootstrap::BootResult> belowLimits = CheckLimits(requirements.RequiredLimits, adapterLimits, adapterName);

    if (belowLimits.has_value())
    {
        return belowLimits;
    }

    std::ranges::sort(plan.Enabled->CapabilityNames);
    std::ranges::sort(plan.Enabled->Capabilities);

    const auto [first, last] = std::ranges::unique(plan.Enabled->Capabilities);

    plan.Enabled->Capabilities.erase(first, last);
    return std::nullopt;
}

std::optional<bootstrap::BootResult> CheckLimits(const lodestone::manifest::DeviceLimits& required,
                                                 const wgpu::Limits& adapter_limits,
                                                 std::string_view adapter_name)
{
    struct LimitCheck
    {
        std::string_view Name;
        uint32_t Required;
        uint32_t Adapter;
    };

    // WGSL has no push constants: Lodestone delivers those values in a uniform buffer, so MaxPushConstantBytes has no
    // WebGPU limit
    const LimitCheck checks[]
    {
        { "maxComputeWorkgroupStorageSize", required.MaxWorkgroupMemoryBytes, adapter_limits.maxComputeWorkgroupStorageSize },
        { "maxComputeInvocationsPerWorkgroup", required.MaxInvocationsPerWorkgroup, adapter_limits.maxComputeInvocationsPerWorkgroup },
        { "maxComputeWorkgroupSizeX", required.MaxWorkgroupSizeX, adapter_limits.maxComputeWorkgroupSizeX },
        { "maxComputeWorkgroupSizeY", required.MaxWorkgroupSizeY, adapter_limits.maxComputeWorkgroupSizeY },
        { "maxComputeWorkgroupSizeZ", required.MaxWorkgroupSizeZ, adapter_limits.maxComputeWorkgroupSizeZ },
    };

    for (const LimitCheck& check : checks)
    {
        if (check.Required > check.Adapter)
        {
            return DeviceFailure(bootstrap::BootResultCode::NoDeviceMeetsRequirements,
                                 std::string{ check.Name },
                                 "",
                                 std::format("{}: {} is {}, and an entry point needs {}", adapter_name, check.Name, check.Adapter, check.Required));
        }
    }

    return std::nullopt;
}

bool SupportsWgslName(void* user, std::string_view capability_name) noexcept
{
    const bootstrap::WebGpuEnabledSet& enabled = *static_cast<const bootstrap::WebGpuEnabledSet*>(user);

    return std::ranges::binary_search(enabled.CapabilityNames, capability_name, std::less<>{});
}

std::string AdapterName(const wgpu::Adapter& adapter)
{
    wgpu::AdapterInfo info{};

    if (adapter.GetInfo(&info) != wgpu::Status::Success)
    {
        return "the adapter";
    }

    std::string name = CopyStringView(info.device);

    if (name.empty())
    {
        name = CopyStringView(info.description);
    }

    return name.empty() ? std::string{ "the adapter" } : name;
}

bootstrap::BootResult DeviceFailure(bootstrap::BootResultCode code, std::string subject, std::string source, std::string detail)
{
    return bootstrap::BootResult
    {
        .Code = code,
        .FailedStage = bootstrap::Stage::Device,
        .Subject = std::move(subject),
        .Source = std::move(source),
        .Detail = std::move(detail),
    };
}

bootstrap::BootResult MissingNeed(const bootstrap::Need& need, std::string detail)
{
    return DeviceFailure(bootstrap::BootResultCode::NoDeviceMeetsRequirements,
                         std::string{ need.Name },
                         bootstrap::FormatNeedSource(need.Source),
                         std::move(detail));
}

void BeginDeviceRequest(bootstrap::WebGpuBootState& state,
                        const bootstrap::WebGpuStageRequestingAdapter& current)
{
    wgpu::Adapter adapter = current.Request->Result;
    WebGpuPlan plan;
    std::optional<bootstrap::BootResult> unmet = PlanDevice(adapter, current.Requirements, current.DeviceCfg, plan);

    if (unmet.has_value())
    {
        state = bootstrap::WebGpuStageFailed{ std::move(*unmet) };
        return;
    }

    // the adapter's own limits: Loom's Compile and the variant filter then check against what the hardware has, not
    // against WebGPU's defaults
    wgpu::Limits requiredLimits{};

    (void)adapter.GetLimits(&requiredLimits);

    std::shared_ptr<bootstrap::DeviceRequestState> request = std::make_shared<bootstrap::DeviceRequestState>();

    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.requiredFeatureCount = plan.Features.size();
    deviceDesc.requiredFeatures = plan.Features.data();
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message)
        {
            const std::string text = CopyStringView(message);
            std::println(stderr,
                         "[webgpu] uncaptured device error ({}): {}",
                         static_cast<uint32_t>(type),
                         text);
        });
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message)
        {
            // Stop destroys the device on purpose
            if (reason == wgpu::DeviceLostReason::Destroyed)
            {
                return;
            }

            const std::string text = CopyStringView(message);
            std::println(stderr,
                         "[webgpu] device lost ({}): {}",
                         static_cast<uint32_t>(reason),
                         text);
        });

    if (!current.DeviceCfg.ApplicationName.empty())
    {
        deviceDesc.label = wgpu::StringView{ current.DeviceCfg.ApplicationName };
    }

    state = bootstrap::WebGpuStageRequestingDevice{
        current.Instance,
        adapter,
        current.DeviceCfg,
        current.SurfaceCfg,
        request,
        plan.Enabled
    };

    // RequestDevice copies the descriptor, so the plan's feature list may go when this function returns
    adapter.RequestDevice(
        &deviceDesc,
        wgpu::CallbackMode::AllowSpontaneous,
        [request](wgpu::RequestDeviceStatus status,
                  wgpu::Device device,
                  wgpu::StringView message)
        {
            request->Succeeded = (status == wgpu::RequestDeviceStatus::Success) && static_cast<bool>(device);
            request->Result = std::move(device);
            request->Message = CopyStringView(message);
            request->Done = true;
        });
}

void FinalizeSurfaceAndContext(bootstrap::WebGpuBootState& state,
                               const bootstrap::WebGpuStageRequestingDevice& current)
{
    wgpu::Device device = current.Request->Result;
    wgpu::Surface surface{};
    wgpu::TextureFormat selectedFormat = wgpu::TextureFormat::Undefined;
    uint32_t width = 0u;
    uint32_t height = 0u;

    if (current.SurfaceCfg.has_value())
    {
        const bootstrap::WebGpuSurfaceConfig& surfaceCfg = *current.SurfaceCfg;
        wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvasSource{};
        canvasSource.selector = surfaceCfg.CanvasSelector.c_str();

        wgpu::SurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &canvasSource;

        surface = current.Instance.CreateSurface(&surfaceDesc);

        if (!surface)
        {
            state = bootstrap::WebGpuStageFailed{
                DeviceFailure(bootstrap::BootResultCode::SurfaceCreateFailed, "", "", "wgpu::Instance::CreateSurface returned null")
            };
            return;
        }

        wgpu::SurfaceCapabilities capabilities{};
        surface.GetCapabilities(current.Adapter, &capabilities);

        if (capabilities.formatCount == 0u)
        {
            state = bootstrap::WebGpuStageFailed{
                DeviceFailure(bootstrap::BootResultCode::SurfaceCreateFailed, "", "", "wgpu::Surface::GetCapabilities returned zero formats")
            };
            return;
        }

        selectedFormat = SelectSurfaceFormat(capabilities, surfaceCfg.PreferredFormat);
        width = surfaceCfg.Width;
        height = surfaceCfg.Height;

        wgpu::SurfaceColorManagement colorManagement{};
        colorManagement.colorSpace = wgpu::PredefinedColorSpace::SRGB;
        colorManagement.toneMappingMode = wgpu::ToneMappingMode::Standard;

        wgpu::SurfaceConfiguration surfaceConfig{};
        surfaceConfig.nextInChain = &colorManagement;
        surfaceConfig.device = device;
        surfaceConfig.format = selectedFormat;
        surfaceConfig.usage = wgpu::TextureUsage::RenderAttachment;
        surfaceConfig.width = width;
        surfaceConfig.height = height;
        surfaceConfig.alphaMode = wgpu::CompositeAlphaMode::Auto;
        surfaceConfig.presentMode = surfaceCfg.PreferredPresentMode;

        surface.Configure(&surfaceConfig);
    }

    // the limits that the device really has, which the filter reports
    wgpu::Limits deviceLimits{};

    if (device.GetLimits(&deviceLimits) == wgpu::Status::Success)
    {
        current.Enabled->Limits = lodestone::manifest::DeviceLimits
        {
            .MaxWorkgroupMemoryBytes = deviceLimits.maxComputeWorkgroupStorageSize,
            .MaxInvocationsPerWorkgroup = deviceLimits.maxComputeInvocationsPerWorkgroup,
            .MaxWorkgroupSizeX = deviceLimits.maxComputeWorkgroupSizeX,
            .MaxWorkgroupSizeY = deviceLimits.maxComputeWorkgroupSizeY,
            .MaxWorkgroupSizeZ = deviceLimits.maxComputeWorkgroupSizeZ,
            .MaxPushConstantBytes = 0u,
        };
    }

    rhi::WebGpuRhiContextView view{};
    view.Instance = current.Instance;
    view.Adapter = current.Adapter;
    view.Device = device;
    view.Queue = device.GetQueue();
    view.ValidationEnabled = current.DeviceCfg.Validation != rhi::ValidationLayers::None;

    state = bootstrap::WebGpuStageReady{
        rhi::MakeWebGpuRhiContext(view),
        std::move(surface),
        selectedFormat,
        width,
        height,
        current.Enabled
    };
}

} // namespace
