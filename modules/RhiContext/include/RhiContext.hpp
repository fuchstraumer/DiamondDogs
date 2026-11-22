#pragma once
#ifndef DIAMOND_DOGS_RHI_CONTEXT_HPP
#define DIAMOND_DOGS_RHI_CONTEXT_HPP
#include <cstdint>
#include <memory>
#include <string_view>

namespace lodestone::manifest
{
    struct DeviceLimits;
}

namespace rhi
{

enum class RhiBackend : uint8_t
{
    Invalid = 0,
    Vulkan = 1,
    WebGpu = 2
};

struct RhiContextImpl;

class RhiContext
{
public:
    RhiContext() noexcept;
    explicit RhiContext(std::unique_ptr<RhiContextImpl> _impl) noexcept;
    ~RhiContext();
    RhiContext(const RhiContext&) = delete;
    RhiContext& operator=(const RhiContext&) = delete;
    RhiContext(RhiContext&& other) noexcept;
    RhiContext& operator=(RhiContext&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] RhiBackend Backend() const noexcept;
    [[nodiscard]] bool IsFeatureEnabled(std::string_view capability_name) const noexcept;
    [[nodiscard]] const lodestone::manifest::DeviceLimits& Limits() const noexcept;
    [[nodiscard]] const RhiContextImpl* Impl() const noexcept;

private:
    std::unique_ptr<RhiContextImpl> impl;
};

} // namespace rhi

#endif // !DIAMOND_DOGS_RHI_CONTEXT_HPP
