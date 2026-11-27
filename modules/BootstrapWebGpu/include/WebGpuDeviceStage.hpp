#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DEVICE_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DEVICE_STAGE_HPP
#include "BootResult.hpp"
#include "DeviceCapability.hpp"
#include "DeviceConfig.hpp"
#include "DeviceFilter.hpp"
#include "DeviceRequirements.hpp"
#include "RhiContext.hpp"
#include "RhiContextWebGpu.hpp"
#include "ShaderManifestDevice.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bootstrap
{

struct AdapterRequestState;
struct DeviceRequestState;

struct WebGpuSurfaceConfig
{
    std::string CanvasSelector{ "#canvas" };
    uint32_t Width{ 800u };
    uint32_t Height{ 600u };
    wgpu::TextureFormat PreferredFormat{ wgpu::TextureFormat::BGRA8Unorm };
    wgpu::PresentMode PreferredPresentMode{ wgpu::PresentMode::Fifo };
};

/** What the device was created with. The stage holds it on the heap, so a DeviceFilter that points into it stays
 *  valid when the stage moves. */
struct WebGpuEnabledSet
{
    // the WGSL enable names whose features the device has, sorted. Supports answers from these
    std::vector<std::string> CapabilityNames;
    // the neutral capabilities whose features the device has, sorted
    std::vector<DeviceCapability> Capabilities;
    // the device's limits. WGSL has no push constants, so MaxPushConstantBytes is 0
    lodestone::manifest::DeviceLimits Limits;
};

struct WebGpuStageUnstarted
{
};

struct WebGpuStageRequestingAdapter
{
    wgpu::Instance Instance;
    DeviceConfig DeviceCfg;
    // a copy: the names still view into the bundles and descriptors, which outlive the boot
    DeviceRequirements Requirements;
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
    // what the plan enabled; the stage reads the device's limits into it when the device exists
    std::shared_ptr<WebGpuEnabledSet> Enabled;
};

struct WebGpuStageReady
{
    rhi::RhiContext Context;
    wgpu::Surface Surface;
    wgpu::TextureFormat SurfaceFormat{ wgpu::TextureFormat::Undefined };
    uint32_t Width{ 0u };
    uint32_t Height{ 0u };
    std::shared_ptr<const WebGpuEnabledSet> Enabled;
};

struct WebGpuStageFailed
{
    // FailedStage is Stage::Device. A need that the adapter lacks names the need, where it came from, and the adapter
    BootResult Result;
};

using WebGpuBootState = std::variant<
    WebGpuStageUnstarted,
    WebGpuStageRequestingAdapter,
    WebGpuStageRequestingDevice,
    WebGpuStageReady,
    WebGpuStageFailed>;

/** The WebGPU device stage. It applies the Vulkan stage's rules: a Required need that the adapter lacks fails the
 *  boot, a Wanted need is requested when the adapter has it, and a Declined Wanted need is left off. The device gets
 *  the adapter's own limits, and the boot fails when the requirements' RequiredLimits are above them. The adapter is
 *  the high-performance one unless the config asks for low power. Step drives the asynchronous requests. */
class WebGpuDeviceStage
{
public:
    WebGpuDeviceStage() noexcept;
    ~WebGpuDeviceStage();
    WebGpuDeviceStage(const WebGpuDeviceStage&) = delete;
    WebGpuDeviceStage& operator=(const WebGpuDeviceStage&) = delete;
    WebGpuDeviceStage(WebGpuDeviceStage&& other) noexcept;
    WebGpuDeviceStage& operator=(WebGpuDeviceStage&& other) noexcept;

    /** @brief Requests the adapter. Every name in `requirements` must outlive the stage, as for the Vulkan stage. A
     *  test with no bundles passes empty requirements, and still gets the adapter's limits. */
    void Start(const DeviceRequirements& requirements, const DeviceConfig& config, const WebGpuSurfaceConfig& surface_config);
    void StartHeadless(const DeviceRequirements& requirements, const DeviceConfig& config);
    void Step();
    void Stop() noexcept;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] bool HasFailed() const noexcept;
    [[nodiscard]] const WebGpuBootState& State() const noexcept;
    [[nodiscard]] const rhi::RhiContext& Context() const noexcept;
    [[nodiscard]] const wgpu::Surface& Surface() const noexcept;
    [[nodiscard]] wgpu::TextureFormat SurfaceFormat() const noexcept;
    /** @brief What the device was created with, in the shape that ShaderLibrary and Loom's Compile take. Empty
     *  (no Supports function) until IsReady. */
    [[nodiscard]] DeviceFilter Filter() const noexcept;
    /** @brief The failure, when HasFailed. A Success result otherwise. */
    [[nodiscard]] const BootResult& Failure() const noexcept;

private:
    void start(const DeviceRequirements& requirements, const DeviceConfig& config, std::optional<WebGpuSurfaceConfig> surface_config);

    WebGpuBootState state;
};

} // namespace bootstrap

#endif // !DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DEVICE_STAGE_HPP
