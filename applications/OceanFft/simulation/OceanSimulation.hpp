#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SIMULATION_HPP
#define DIAMOND_DOGS_OCEAN_SIMULATION_HPP
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

class ResourceContext;

namespace rhi
{
    struct CommandBuffer;
    class Device;
    class ShaderLibrary;
}

// The OceanFft compute chain (assets/shaders/compute/Ocean), driven through rhi::ShaderLibrary. Shared by the
// OceanFft application and the headless OceanFftTest, so the test checks the code the demo runs.
//
// Once:      SpectrumInitCS -> SpectrumConjugateCS                  (h0(k), conj(h0(-k)), wave data)
// Per frame: OceanTickCS -> IFFT rows -> IFFT columns -> OceanResolveCS (Displacement and Derivatives textures)
namespace ocean
{
    /** @brief SpectrumParameters in OceanFftCommon.slang, std140. Create() checks it against the manifest. */
    struct SpectrumParameters
    {
        uint32_t NoiseSeed{ 1u };
        uint32_t DirectionalSpreadingMode{ 0u }; // 0 Mitsuyasu, 1 Hasselman, 2 Donelan-Banner
        float WindSpeed{ 10.0f };                // m/s
        float Fetch{ 100.0f };                   // km
        float Depth{ 50.0f };                    // m
        float SwellAmount{ 0.2f };
        float Alignment{ 1.0f };
        float LocalWindDirection{ 0.0f };        // radians
        float SwellDirection{ 0.0f };            // radians
        float FadeLength{ 0.01f };               // m
        float PeakingFactor{ 3.3f };
        /** @brief Multiplies every wave amplitude. 1 is the physical spectrum; GodotOceanWaves and Ocean-URP both run
         *  at about 2.8 (docs/ocean-reference-comparison.md, section 1) */
        float WaveHeightScale{ 1.0f };
        /** @brief The swell's energy relative to its physical spectrum. 0 turns the swell off. The swell's direction is
         *  SwellDirection, and it comes from distant wind: SwellWindSpeed over SwellFetch */
        float SwellScale{ 0.25f };
        float SwellWindSpeed{ 12.0f };           // m/s
        float SwellFetch{ 600.0f };              // km: about 11.6 s, 210 m waves at 12 m/s
        float SwellPeakingFactor{ 7.0f };        // a swell is narrow-banded
        float SwellSpread{ 2.0f };               // narrows the swell's spreading lobe, like SwellAmount for the wind sea
        float SwellFadeLength{ 1.0f };           // m: a swell has no short waves
    };

    /** @brief CascadeParameters in OceanFftCommon.slang, std140 */
    struct CascadeParameters
    {
        std::array<float, 4> LengthScales{};
        std::array<float, 4> CutoffsHigh{};
        std::array<float, 4> CutoffsLow{};
        std::array<uint32_t, 2> Dimensions{};
        std::array<float, 2> Pad0{};
    };
    static_assert(sizeof(CascadeParameters) == 64u);
    static_assert(sizeof(SpectrumParameters) == 72u);

    enum class IfftKernel : uint8_t
    {
        Radix2,
        Radix4,
    };

    constexpr uint32_t k_CascadeCount = 4u;

    struct OceanSimulationSettings
    {
        /** @brief One of the IFFT_SIZE axis values: 128, 256, 512, 1024 */
        uint32_t Size{ 1024u };
        IfftKernel Kernel{ IfftKernel::Radix4 };
        /** @brief 0 selects the variant without wave ops. Otherwise the IFFT_WAVE_SIZE of the wave-op variant, and
         *  the subgroup size the IFFT shaders are created with (full subgroups required). See PickWaveSize. */
        uint32_t WaveSize{ 0u };
        /** @brief The patch size of each cascade, in meters, largest first */
        std::array<float, k_CascadeCount> LengthScales{ 1024.0f, 192.0f, 32.0f, 6.0f };
        SpectrumParameters Spectrum{};
        float Choppiness{ 1.0f };
    };

    /** @brief Returns the device's subgroup size when the wave-op variants can run with it (an IFFT_WAVE_SIZE axis
     *  value, required-size control for compute, and subgroup shuffles), and 0 otherwise. 32 on NVIDIA. */
    [[nodiscard]] uint32_t PickWaveSize(const rhi::Device& device) noexcept;

    /** @brief Non-overlapping bands: each cascade covers the wave numbers that the next, smaller one samples too coarsely */
    [[nodiscard]] CascadeParameters MakeCascadeParameters(uint32_t size, const std::array<float, k_CascadeCount>& length_scales) noexcept;

    struct OceanSimulationImpl;

    class OceanSimulation
    {
    public:
        enum class Buffer : uint8_t
        {
            H0K,
            H0KConjugate,
            WaveData,
            IfftInput,
        };

        /** @brief Creates the programs, buffers and textures, and waits until every upload finished */
        [[nodiscard]] static std::expected<std::unique_ptr<OceanSimulation>, std::string> Create(
            rhi::Device& device,
            ResourceContext& resources,
            const rhi::ShaderLibrary& library,
            const OceanSimulationSettings& settings);

        explicit OceanSimulation(std::unique_ptr<OceanSimulationImpl>&& _impl) noexcept;
        ~OceanSimulation();
        OceanSimulation(const OceanSimulation&) = delete;
        OceanSimulation& operator=(const OceanSimulation&) = delete;

        /** @brief Moves the output textures to the General layout, then computes the initial spectrum. Record once,
         *  and again after SetSpectrum */
        void RecordInitialize(rhi::CommandBuffer& command_buffer) const;
        /** @brief The stages and accesses that RecordTick writes the output textures with. The resolve writes mip 0 as
         *  a storage image, and blits fill the other mips from it. A reader's barrier waits on these. The barrier
         *  before the next tick must make k_TickStages wait for that reader */
        static constexpr rhi::PipelineStage k_TickStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::Blit;
        static constexpr rhi::Access k_TickWriteAccess = rhi::Access::ShaderStorageWrite | rhi::Access::TransferWrite;

        /** @brief Tick, both IFFT passes, the resolve, and the mip chains of the output textures. Records no barrier
         *  after the last mip: the reader of the textures adds its own (from k_TickStages and k_TickWriteAccess) */
        void RecordTick(rhi::CommandBuffer& command_buffer, float time_seconds) const;
        /** @brief Only the two IFFT passes over IfftInput, in place. For tests */
        void RecordIfft(rhi::CommandBuffer& command_buffer) const;

        /** @brief Writes new spectrum parameters, which RecordInitialize then uses. The caller makes sure no
         *  submitted work still reads the uniform buffer */
        [[nodiscard]] bool SetSpectrum(const SpectrumParameters& spectrum);
        void SetChoppiness(float choppiness) noexcept;

        [[nodiscard]] const OceanSimulationSettings& Settings() const noexcept;
        [[nodiscard]] GraphicsResource GetBuffer(Buffer buffer) const noexcept;
        [[nodiscard]] uint64_t BufferBytes(Buffer buffer) const noexcept;
        /** @brief rgba16f 2D arrays, one layer per cascade, with a full mip chain (MipLevels), in the General layout
         *  after RecordInitialize. The view covers every mip; storage writes go to mip 0 */
        [[nodiscard]] GraphicsResource Displacement() const noexcept;
        [[nodiscard]] GraphicsResource Derivatives() const noexcept;
        [[nodiscard]] uint32_t MipLevels() const noexcept;

    private:
        std::unique_ptr<OceanSimulationImpl> impl;
    };

    /** @brief IEEE half precision conversions, for uploads and readbacks */
    [[nodiscard]] float HalfToFloat(uint16_t value) noexcept;
    [[nodiscard]] uint16_t FloatToHalf(float value) noexcept;
}

#endif //!DIAMOND_DOGS_OCEAN_SIMULATION_HPP
