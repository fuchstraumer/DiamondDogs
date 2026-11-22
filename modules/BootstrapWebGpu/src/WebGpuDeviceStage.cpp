#include "WebGpuDeviceStage.hpp"
#include "DeviceConfig.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"
#include "RhiFlags.hpp"
#include "webgpu/webgpu.h"
#include "webgpu/webgpu_cpp.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <variant>

namespace
{

wgpu::PowerPreference ToWgpuPowerPreference(bootstrap::PowerPreference preference) noexcept;
std::string CopyStringView(wgpu::StringView view);
wgpu::TextureFormat SelectSurfaceFormat(const wgpu::SurfaceCapabilities& capabilities,
                                          wgpu::TextureFormat preferred) noexcept;
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

void WebGpuDeviceStage::Start(const DeviceConfig& config, const WebGpuSurfaceConfig& surface_config)
{
    start(config, surface_config);
}

void WebGpuDeviceStage::StartHeadless(const DeviceConfig& config)
{
    start(config, std::nullopt);
}

void WebGpuDeviceStage::start(const DeviceConfig& config, std::optional<WebGpuSurfaceConfig> surface_config)
{
    Stop();

    wgpu::Instance instance = wgpu::CreateInstance(nullptr);

    if (!instance)
    {
        state = WebGpuStageFailed{
            WebGpuBootErrorCode::InstanceCreateFailed,
            "wgpu::CreateInstance returned null"
        };
        return;
    }

    std::shared_ptr<AdapterRequestState> request = std::make_shared<AdapterRequestState>();
    wgpu::RequestAdapterOptions options{};
    options.powerPreference = ToWgpuPowerPreference(config.Power);

    state = WebGpuStageRequestingAdapter{
        instance,
        config,
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
                WebGpuBootErrorCode::AdapterRequestFailed,
                current.Request->Message
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
                WebGpuBootErrorCode::DeviceRequestFailed,
                current.Request->Message
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

WebGpuBootErrorCode WebGpuDeviceStage::FailureCode() const noexcept
{
    if (const WebGpuStageFailed* failed = std::get_if<WebGpuStageFailed>(&state))
    {
        return failed->Code;
    }

    return WebGpuBootErrorCode::Invalid;
}

const std::string& WebGpuDeviceStage::FailureMessage() const noexcept
{
    if (const WebGpuStageFailed* failed = std::get_if<WebGpuStageFailed>(&state))
    {
        return failed->Message;
    }

    static const std::string emptyMessage{};
    return emptyMessage;
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
    case bootstrap::PowerPreference::HighPerformance:
        return wgpu::PowerPreference::HighPerformance;
    default:
        return wgpu::PowerPreference::Undefined;
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

void BeginDeviceRequest(bootstrap::WebGpuBootState& state,
                        const bootstrap::WebGpuStageRequestingAdapter& current)
{
    wgpu::Adapter adapter = current.Request->Result;
    std::shared_ptr<bootstrap::DeviceRequestState> request = std::make_shared<bootstrap::DeviceRequestState>();

    wgpu::DeviceDescriptor deviceDesc{};
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
        request
    };

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
                bootstrap::WebGpuBootErrorCode::SurfaceCreateFailed,
                "wgpu::Instance::CreateSurface returned null"
            };
            return;
        }

        wgpu::SurfaceCapabilities capabilities{};
        surface.GetCapabilities(current.Adapter, &capabilities);

        if (capabilities.formatCount == 0u)
        {
            state = bootstrap::WebGpuStageFailed{
                bootstrap::WebGpuBootErrorCode::SurfaceUnsupported,
                "wgpu::Surface::GetCapabilities returned zero formats"
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
        height
    };
}

} // namespace
