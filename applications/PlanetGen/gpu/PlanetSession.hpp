#pragma once
#ifndef DIAMOND_DOGS_PLANET_SESSION_HPP
#define DIAMOND_DOGS_PLANET_SESSION_HPP
#include "PlanetGpuErrors.hpp"
#include "RhiFlags.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

class ResourceContext;

namespace rhi
{
    class Device;
    class ShaderLibrary;
    struct CommandBuffer;
}

namespace bootstrap
{
    struct DeviceFilter;
    struct Need;
}

namespace planet
{
    enum class CrustFlagChoice : uint8_t;
    class PlanetShaders;

    enum class ConfigFileMode : uint8_t
    {
        Invalid = 0,
        Ignore,
        Read,
    };

    struct PlanetSessionConfig
    {
        std::string_view ApplicationName;
        rhi::ValidationLayers Validation{ rhi::ValidationLayers::None };
        ConfigFileMode ConfigMode{ ConfigFileMode::Ignore };
        std::span<const bootstrap::Need> ExtraNeeds;
    };

    struct PlanetSessionImpl;

    /** Owns the headless Vulkan boot stages, shader library, and compute command pool. Keeps Vulkan and bootstrap
     *  stage headers out of application entry points. */
    class PlanetSession
    {
    public:
        [[nodiscard]] static std::unique_ptr<PlanetSession> Start(
            std::span<char* const> arguments,
            const PlanetSessionConfig& config);

        explicit PlanetSession(std::unique_ptr<PlanetSessionImpl>&& _impl) noexcept;
        ~PlanetSession();
        PlanetSession(const PlanetSession&) = delete;
        PlanetSession& operator=(const PlanetSession&) = delete;

        [[nodiscard]] rhi::Device& Device() noexcept;
        [[nodiscard]] ResourceContext& Resources() noexcept;
        [[nodiscard]] const rhi::ShaderLibrary& Library() const noexcept;
        [[nodiscard]] bootstrap::DeviceFilter Filter() const noexcept;
        [[nodiscard]] std::string_view DeviceName() const noexcept;
        [[nodiscard]] bool HasFrameBoundaries() const noexcept;

        [[nodiscard]] PlanetGpuResult<std::unique_ptr<PlanetShaders>> CreateShaders(
            CrustFlagChoice crust_flags,
            std::span<const std::string_view> entry_points) const;

        [[nodiscard]] rhi::CommandBuffer BeginCommands();
        void DiscardCommands(rhi::CommandBuffer& command_buffer);
        [[nodiscard]] bool SubmitAndWait(rhi::CommandBuffer& command_buffer);
        [[nodiscard]] bool SubmitFrameEndAndWait(rhi::CommandBuffer& command_buffer, uint64_t frame_id);

    private:
        std::unique_ptr<PlanetSessionImpl> impl;
    };

    [[nodiscard]] std::span<const bootstrap::Need> FrameBoundaryNeeds() noexcept;
}

#endif // !DIAMOND_DOGS_PLANET_SESSION_HPP
