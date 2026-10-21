#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SIMULATION_HPP
#define DIAMOND_DOGS_OCEAN_SIMULATION_HPP
#include "ResourceTypes.hpp"
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
    static_assert(sizeof(SpectrumParameters) == 44u);

    enum class IfftKernel : uint8_t
    {
        Radix2,
        Radix4,
    };

    constexpr uint32_t k_CascadeCount = 4u;

    struct OceanSimulationSettings
    {
        /** @brief One of the IFFT_SIZE axis values: 128, 256, 512, 1024 */
        uint32_t Size{ 512u };
        IfftKernel Kernel{ IfftKernel::Radix4 };
        /** @brief 0 selects the variant without wave ops. Otherwise the IFFT_WAVE_SIZE of the wave-op variant, and
         *  the subgroup size the IFFT shaders are created with (full subgroups required). See PickWaveSize. */
        uint32_t WaveSize{ 0u };
        /** @brief The patch size of each cascade, in meters, largest first */
        std::array<float, k_CascadeCount> LengthScales{ 256.0f, 48.0f, 12.0f, 3.0f };
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
        /** @brief Tick, both IFFT passes, and the resolve. Records no barrier after the resolve: the reader of the
         *  textures adds its own (compute shader storage write to its stage and access) */
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
        /** @brief rgba16f 2D arrays, one layer per cascade, in the General layout after RecordInitialize */
        [[nodiscard]] GraphicsResource Displacement() const noexcept;
        [[nodiscard]] GraphicsResource Derivatives() const noexcept;

    private:
        std::unique_ptr<OceanSimulationImpl> impl;
    };

    /** @brief IEEE half precision conversions, for uploads and readbacks */
    [[nodiscard]] float HalfToFloat(uint16_t value) noexcept;
    [[nodiscard]] uint16_t FloatToHalf(float value) noexcept;
}

#endif //!DIAMOND_DOGS_OCEAN_SIMULATION_HPP
