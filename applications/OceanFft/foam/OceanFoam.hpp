#pragma once
#ifndef DIAMOND_DOGS_OCEAN_FOAM_HPP
#define DIAMOND_DOGS_OCEAN_FOAM_HPP
#include "ResourceTypes.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

class ResourceContext;

namespace rhi
{
    class GpuTimerBlock;
    struct CommandBuffer;
    class Device;
    class ShaderLibrary;
}

// The persistent foam (OceanFoam.slang in the OceanRender module): a clipmap of two square levels around the camera,
// updated by one compute dispatch per frame. The window follows the camera and drifts with the wind, and the CPU
// state here (the drift and the window origins) is what the shader needs to scroll it without resampling.
namespace ocean
{
    class OceanSimulation;

    /** @brief The settings of the persistent foam. Resolution and TexelSizes are fixed when the foam is created */
    struct FoamSettings
    {
        bool Enabled{ false };
        /** @brief Texels along the edge of each clipmap level */
        int32_t Resolution{ 1024 };
        /** @brief The texel size of level 0 and level 1, in meters */
        std::array<float, 2> TexelSizes{ 0.25f, 2.0f };
        /** @brief Seconds for the foam to fall to 1/e of its value */
        float DecayTime{ 3.0f };
        /** @brief The stored foam's steady state over the whitecap coverage: production rate times decay time. At 1
         *  the foam is the coverage averaged over the last decay time, so the levels agree and nothing saturates */
        float PersistenceGain{ 2.0f };
        /** @brief How far old foam raises the breaking threshold, in Jacobian units */
        float ThresholdRaise{ 0.15f };
        /** @brief The residual foam: its amount, how soft its dissolve edge is, and the meters per tile of the foam texture */
        float ResidualAmount{ 1.0f };
        float ResidualSoftness{ 0.2f };
        float TextureTileSize{ 4.0f };
        /** @brief The drift speed as a fraction of the wind speed */
        float WindDrift{ 0.03f };
        /** @brief The steps of one replay, which covers three decay times */
        int32_t ReplaySteps{ 64 };

        [[nodiscard]] bool operator==(const FoamSettings& other) const = default;
    };

    struct OceanFoamImpl;

    class OceanFoam
    {
    public:
        /** @brief Creates the clipmap image, the step program, and one descriptor set for each view buffer. The
         *  simulation supplies Displacement, Derivatives and Whitecaps. The view buffers and the sampler must outlive the foam */
        [[nodiscard]] static std::expected<std::unique_ptr<OceanFoam>, std::string> Create(
            rhi::Device& device,
            ResourceContext& resources,
            const rhi::ShaderLibrary& library,
            const OceanSimulation& simulation,
            std::span<const GraphicsResource> view_buffers,
            const GraphicsResource& sampler,
            const FoamSettings& settings);

        explicit OceanFoam(std::unique_ptr<OceanFoamImpl>&& _impl) noexcept;
        ~OceanFoam();
        OceanFoam(const OceanFoam&) = delete;
        OceanFoam& operator=(const OceanFoam&) = delete;

        [[nodiscard]] const FoamSettings& Settings() const noexcept;
        /** @brief Replaces the settings, except Resolution and TexelSizes. Call it before Advance in the frame */
        void SetSettings(const FoamSettings& settings) noexcept;

        /** @brief Moves the drift by the wind for `delta_seconds` and computes the window origins around the camera.
         *  Call it once per frame, with the step that the simulation time advanced by (0 when paused). RecordStep
         *  uses the result */
        void Advance(double delta_seconds, std::array<float, 2> camera_xz, float wind_direction_radians, float wind_speed) noexcept;
        /** @brief Writes the foam fields of the view parameters (PersistentFoamParameters, FoamDrift and
         *  FoamResidualParameters in OceanRenderCommon.slang). Call it after Advance */
        void FillView(std::array<float, 4>& persistent_foam_parameters, std::array<float, 4>& foam_drift, std::array<float, 4>& foam_residual) const noexcept;
        /** @brief Records the barriers and the dispatch that integrate one step. Records only a one-time layout
         *  change while the foam is disabled. frame_index selects the descriptor set of that frame's view buffer */
        void RecordStep(rhi::CommandBuffer& command_buffer, uint32_t frame_index, rhi::GpuTimerBlock* timer_block = nullptr);
        /** @brief Makes the next RecordStep clear the foam */
        void Invalidate() noexcept;

        /** @brief The drift-frame offset in world meters */
        [[nodiscard]] std::array<double, 2> Drift() const noexcept;
        void SetDrift(const std::array<double, 2>& drift) noexcept;
        /** @brief The drift velocity in meters per second for this wind */
        [[nodiscard]] std::array<double, 2> DriftVelocity(float wind_direction_radians, float wind_speed) const noexcept;
        /** @brief The window origins that Advance computed, in absolute texels: xy level 0, zw level 1 */
        [[nodiscard]] std::array<int32_t, 4> WindowOrigins() const noexcept;
        [[nodiscard]] GraphicsResource Clipmap() const noexcept;

    private:
        std::unique_ptr<OceanFoamImpl> impl;
    };
}

#endif //!DIAMOND_DOGS_OCEAN_FOAM_HPP
