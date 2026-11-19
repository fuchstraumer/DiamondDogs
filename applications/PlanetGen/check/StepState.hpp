#pragma once
#ifndef DIAMOND_DOGS_PLANET_STEP_STATE_HPP
#define DIAMOND_DOGS_PLANET_STEP_STATE_HPP
#include "PlanetStep.hpp"
#include "math/MathStorage.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace planet
{
    class GpuBuffers;
    class PlanetShaders;
    class ReferenceSet;

    /** The w value of a plate cap (LiveCells.slang writes the same values): */
    /** no cap, so OwnersCS tests the plate. A zero buffer means this. */
    inline constexpr float k_CapNone = 0.0f;
    /** a cap: xyz is the unit centre in the plate frame, and PlateCapSpread holds 1 - cos(radius) as float bits. */
    inline constexpr float k_CapValid = 1.0f;
    /** the plate has no crust, so OwnersCS skips it. */
    inline constexpr float k_CapEmpty = 2.0f;

    /** The plate caps as the shaders store them: PlateCapCenters (4 floats a plate), PlateCapSpread (one word a plate). */
    struct PlateCaps
    {
        std::vector<math::Float4> Centres;
        std::vector<uint32_t> Spreads;
    };

    enum class ReferenceArrayForm : uint8_t
    {
        Invalid = 0,
        Int,
        Word,
        Float,
        Flag,
    };

    struct ReferenceBufferSpec
    {
        std::string_view BufferName;
        std::string_view FileName;
        ReferenceArrayForm Form{ ReferenceArrayForm::Invalid };
        bool Readable{ false };
    };

    /** Whether the outputs of the step get a readback copy. The check reads them; the bench does not. */
    enum class StepReadback : bool
    {
        None = false,
        Outputs = true,
    };

    /** @brief The cap of each plate around its live cells, from the alive flags ([plate][cell], 1 alive): the centre is the
     *  mean of the live lattice points, the spread the largest 1 - dot(point, centre), rounded up to a float. */
    [[nodiscard]] PlateCaps ComputePlateCaps(std::span<const uint8_t> alive, int32_t point_count, int32_t plate_count);

    [[nodiscard]] size_t FlagElementSize(CrustFlagStorage storage) noexcept;
    [[nodiscard]] size_t ReferenceElementSize(ReferenceArrayForm form, CrustFlagStorage storage) noexcept;
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadReferenceBytes(const ReferenceSet& reference,
                                                                           std::string_view file,
                                                                           ReferenceArrayForm form,
                                                                           CrustFlagStorage storage);
    [[nodiscard]] bool AddReferenceBuffers(GpuBuffers& buffers,
                                           const ReferenceSet& reference,
                                           CrustFlagStorage storage,
                                           std::span<const ReferenceBufferSpec> specs);
    [[nodiscard]] bool AddParamsUniform(GpuBuffers& buffers,
                                        const PlanetShaders& shaders,
                                        std::string_view entry_point,
                                        StepSize size);
    [[nodiscard]] bool AddTimeUniform(GpuBuffers& buffers,
                                      const PlanetShaders& shaders,
                                      std::string_view entry_point,
                                      std::string_view resource,
                                      float time);
    [[nodiscard]] bool AddRotationsBuffer(GpuBuffers& buffers, const ReferenceSet& reference);

    /** @brief Creates every buffer that PlanetStep binds, from the reference state at the start of the step: the inputs,
     *  the crust arrays and the plate caps with their initial copies, the uniform buffers, and the outputs and counters (zero). The crust
     *  flags use the storage of `shaders`. Prints the cause and returns false when a file or a buffer fails. */
    [[nodiscard]] bool AddStepBuffers(GpuBuffers& buffers, const PlanetShaders& shaders, const ReferenceSet& reference, StepReadback readback);
}

#endif // !DIAMOND_DOGS_PLANET_STEP_STATE_HPP
