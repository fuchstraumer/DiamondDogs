#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DEVICE_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DEVICE_STAGE_HPP
#include "DeviceConfig.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace bootstrap
{

struct AdapterRequestState;
struct DeviceRequestState;

enum class WebGpuBootErrorCode : uint8_t
{
    Invalid = 0,
    InstanceCreateFailed = 1,
    AdapterRequestFailed = 2,
    DeviceRequestFailed = 3,
    SurfaceCreateFailed = 4,
    SurfaceUnsupported = 5
};

struct WebGpuSurfaceConfig
{
    std::string CanvasSelector{ "#canvas" };
    uint32_t Width{ 800u };
    uint32_t Height{ 600u };
    wgpu::TextureFormat PreferredFormat{ wgpu::TextureFormat::BGRA8Unorm };
    wgpu::PresentMode PreferredPresentMode{ wgpu::PresentMode::Fifo };
};

struct WebGpuStageUnstarted
{
};

struct WebGpuStageRequestingAdapter
{
    wgpu::Instance Instance;
    DeviceConfig DeviceCfg;
    std::optional<WebGpuSurfaceConfig> SurfaceCfg;
    std::shared_ptr<AdapterRequestState> Request;
};

struct WebGpuStageRequestingDevice
{
    wgpu::Instance Instance;
    wgpu::Adapter Adapter;
    DeviceConfig DeviceCfg;
    std::optional<WebGpuSurfaceConfig> SurfaceCfg;
    std::shared_ptr<DeviceRequestState> Request;
};

struct WebGpuStageReady
{
    rhi::RhiContext Context;
    wgpu::Surface Surface;
    wgpu::TextureFormat SurfaceFormat{ wgpu::TextureFormat::Undefined };
    uint32_t Width{ 0u };
    uint32_t Height{ 0u };
};

struct WebGpuStageFailed
{
    WebGpuBootErrorCode Code{ WebGpuBootErrorCode::Invalid };
    std::string Message;
};

using WebGpuBootState = std::variant<
    WebGpuStageUnstarted,
    WebGpuStageRequestingAdapter,
    WebGpuStageRequestingDevice,
    WebGpuStageReady,
    WebGpuStageFailed>;

class WebGpuDeviceStage
{
public:
    WebGpuDeviceStage() noexcept;
    ~WebGpuDeviceStage();
    WebGpuDeviceStage(const WebGpuDeviceStage&) = delete;
    WebGpuDeviceStage& operator=(const WebGpuDeviceStage&) = delete;
    WebGpuDeviceStage(WebGpuDeviceStage&& other) noexcept;
    WebGpuDeviceStage& operator=(WebGpuDeviceStage&& other) noexcept;

    void Start(const DeviceConfig& config, const WebGpuSurfaceConfig& surface_config);
    void StartHeadless(const DeviceConfig& config);
    void Step();
    void Stop() noexcept;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool HasFailed() const noexcept;
    [[nodiscard]] const WebGpuBootState& State() const noexcept;
    [[nodiscard]] const rhi::RhiContext& Context() const noexcept;
    [[nodiscard]] const wgpu::Surface& Surface() const noexcept;
    [[nodiscard]] wgpu::TextureFormat SurfaceFormat() const noexcept;
    [[nodiscard]] WebGpuBootErrorCode FailureCode() const noexcept;
    [[nodiscard]] const std::string& FailureMessage() const noexcept;

private:
    void start(const DeviceConfig& config, std::optional<WebGpuSurfaceConfig> surface_config);

    WebGpuBootState state;
};

} // namespace bootstrap

#endif // !DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DEVICE_STAGE_HPP
