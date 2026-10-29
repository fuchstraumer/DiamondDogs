#pragma once
#ifndef DIAMOND_DOGS_OCEAN_CDLOD_HPP
#define DIAMOND_DOGS_OCEAN_CDLOD_HPP

#include "ResourceTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

class ResourceContext;

namespace rhi
{
    struct CommandBuffer;
    class Device;
    class GpuTimerBlock;
    class ShaderLibrary;
}

namespace ocean
{
    struct OceanPatchVertex
    {
        std::array<float, 2> GridUv{};
    };
    static_assert(sizeof(OceanPatchVertex) == 8u);

    struct OceanCdlodPatch
    {
        std::array<float, 2> WorldOriginXz{};
        float PatchScale{ 0.0f };
        uint32_t LodAndEdgeMask{ 0u };
        float MorphStart{ 0.0f };
        float InvMorphRange{ 0.0f };
        std::array<float, 2> Padding{};
    };
    static_assert(sizeof(OceanCdlodPatch) == 32u);

    struct alignas(16) OceanQuadtreeParams
    {
        std::array<std::array<float, 4>, 6> FrustumPlanes{};
        std::array<float, 3> CameraPosition{};
        float WorldExtent{ 131072.0f };
        std::array<float, 2> RootCenter{};
        float MaxWaveHeight{ 20.0f };
        float MorphStartRatio{ 0.65f };
        uint32_t MaxLevel{ 13u };
        uint32_t BaseGridCount{ 64u };
        float LodDistanceScale{ 1.4f };
        uint32_t MaxNodes{ 131072u };
        uint32_t MaxSelectedPatches{ 16384u };
        std::array<uint32_t, 3> Padding{};
    };
    static_assert(sizeof(OceanQuadtreeParams) == 160u);

    struct OceanCdlodCameraState
    {
        std::array<std::array<float, 4>, 6> FrustumPlanes{};
        std::array<float, 3> CameraPosition{};
    };

    struct OceanCdlodSettings
    {
        float WorldExtent{ 131072.0f };
        float MaxWaveHeight{ 20.0f };
        float MorphStartRatio{ 0.65f };
        uint32_t MaxLevel{ 13u };
        uint32_t BaseGridCount{ 64u };
        float LodDistanceScale{ 1.4f };
        uint32_t MaxSelectedPatches{ 16384u };
        uint32_t MaxNodes{ 131072u };

        [[nodiscard]] bool operator==(const OceanCdlodSettings& other) const = default;
    };

    /**
     * @brief Computes the CDLOD maximum subdivision level (floor) from the finest active cascade
     * length and FFT resolution, ensuring the CDLOD vertex spacing matches or exceeds the finest
     * cascade texel spacing: minVertexSpacing <= finestCascadeLength / fftResolution.
     */
    [[nodiscard]] inline uint32_t ComputeCdlodMaxLevel(
        float world_extent,
        uint32_t base_grid_count,
        uint32_t fft_size,
        float min_cascade_length,
        uint32_t max_allowed_level = 13u) noexcept
    {
        if (base_grid_count == 0u || fft_size == 0u || min_cascade_length <= 0.0f || world_extent <= 0.0f)
        {
            return max_allowed_level;
        }
        const float ratio = (world_extent * static_cast<float>(fft_size)) /
                            (static_cast<float>(base_grid_count) * min_cascade_length);
        if (ratio <= 1.0f)
        {
            return 1u;
        }
        const uint32_t targetLevel = static_cast<uint32_t>(std::ceil(std::log2(ratio)));
        return std::clamp(targetLevel, 1u, max_allowed_level);
    }

    struct OceanCdlodImpl;

    class OceanCdlod
    {
    public:
        [[nodiscard]] static std::expected<std::unique_ptr<OceanCdlod>, std::string> Create(
            rhi::Device& device,
            ResourceContext& resources,
            const rhi::ShaderLibrary& library,
            uint32_t frame_count,
            const OceanCdlodSettings& settings);

        explicit OceanCdlod(std::unique_ptr<OceanCdlodImpl>&& _impl) noexcept;
        ~OceanCdlod();
        OceanCdlod(const OceanCdlod&) = delete;
        OceanCdlod& operator=(const OceanCdlod&) = delete;

        [[nodiscard]] const OceanCdlodSettings& Settings() const noexcept;
        void SetSettings(const OceanCdlodSettings& settings) noexcept;

        void RecordUpdate(
            rhi::CommandBuffer& cmd,
            uint32_t frame_index,
            const OceanCdlodCameraState& camera,
            rhi::GpuTimerBlock* timer_block);

        [[nodiscard]] const GraphicsResource& GridVertexBuffer() const noexcept;
        [[nodiscard]] const GraphicsResource& GridIndexBuffer() const noexcept;
        [[nodiscard]] const GraphicsResource& SelectedPatchesBuffer() const noexcept;
        [[nodiscard]] const GraphicsResource& IndirectDrawBuffer() const noexcept;
        [[nodiscard]] uint32_t IndexCount() const noexcept;

    private:
        std::unique_ptr<OceanCdlodImpl> impl;
    };
}

#endif //!DIAMOND_DOGS_OCEAN_CDLOD_HPP
