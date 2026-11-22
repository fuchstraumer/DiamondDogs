#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DISPLAY_HPP
#define DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DISPLAY_HPP
#include "ImageDataFormats.hpp"
#include "PlatformTypes.hpp"

#include <cstdint>
#include <vector>

namespace bootstrap
{

class WebGpuDisplayObserver
{
public:
    WebGpuDisplayObserver() noexcept;
    ~WebGpuDisplayObserver();
    WebGpuDisplayObserver(const WebGpuDisplayObserver&) = delete;
    WebGpuDisplayObserver& operator=(const WebGpuDisplayObserver&) = delete;
    WebGpuDisplayObserver(WebGpuDisplayObserver&&) = delete;
    WebGpuDisplayObserver& operator=(WebGpuDisplayObserver&&) = delete;

    [[nodiscard]] bool ConsumeCapabilitiesChanged() noexcept;
    [[nodiscard]] DisplayColorCapabilities QueryCapabilities() const noexcept;
    [[nodiscard]] std::vector<SurfaceFormatPair> AvailableSurfacePairs(ImageFormat preferred_sdr_format) const;

private:
    uint8_t dirtyFlag{ 0u };
};

} // namespace bootstrap

#endif // !DIAMOND_DOGS_BOOTSTRAP_WEBGPU_DISPLAY_HPP
