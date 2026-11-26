#pragma once
#ifndef DIAMOND_DOGS_PLANET_STEP_STATE_HPP
#define DIAMOND_DOGS_PLANET_STEP_STATE_HPP
#include "PlanetShaders.hpp"
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

    /** Resampled HEALPix initial simulation state shared between legacy PlanetStep and Loom PlanetGenLoom. */
    struct HealpixInitialState
    {
        uint32_t PointCount{ 0u };
        uint32_t PlateCount{ 0u };
        float InitialTime{ 0.0f };
        std::vector<math::Float4> Rotations;
        std::vector<math::Float4> PoleRates;
        std::vector<std::byte> CrustFlags;
        std::vector<math::Float4> CrustState;
        PlateCaps Caps;
        std::vector<int32_t> PreviousPlate;
    };

    /** @brief Resamples the reference state onto a nested HEALPix grid of `order`. */
    [[nodiscard]] std::optional<HealpixInitialState> ResampleHealpixState(const ReferenceSet& reference,
                                                                          uint32_t order,
                                                                          CrustFlagStorage storage);

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

    /** @brief Computes tight spherical caps per plate from alive flags indexed on nested HEALPix `order`. */
    [[nodiscard]] PlateCaps ComputeHealpixPlateCaps(std::span<const uint8_t> alive, uint32_t order, int32_t plate_count);

    /** @brief Binds the default legacy `ShapingConstants` uniform buffer matching PlanetLab. */
    [[nodiscard]] bool AddDefaultShapingUniform(GpuBuffers& buffers);

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
                                        StepSize size,
                                        float time_step_my = 2.0f,
                                        uint32_t healpix_order = 0u);
    [[nodiscard]] bool AddTimeUniform(GpuBuffers& buffers,
                                      const PlanetShaders& shaders,
                                      std::string_view entry_point,
                                      std::string_view resource,
                                      float time);
    [[nodiscard]] bool AddRotationsBuffer(GpuBuffers& buffers, const ReferenceSet& reference);

    /** HEALPix pixels for each lattice point when HealpixPointOrder sorts the points. 16 keeps most pixels to one
     *  point, so the sort follows the curve closely. */
    inline constexpr uint32_t k_OrderPixelsPerPoint = 16u;

    /** @brief The lattice points sorted by their HEALPix nested index. Entry i is the point that thread i of OwnersCS
     *  takes, so the 32 points of a wave are one compact patch. Equal indices keep the lattice order. */
    [[nodiscard]] std::vector<uint32_t> HealpixPointOrder(int32_t point_count);
    /** @brief Creates OwnersOrder from HealpixPointOrder. */
    [[nodiscard]] bool AddOwnersOrderBuffer(GpuBuffers& buffers, int32_t point_count);

    /** @brief Creates every buffer that PlanetStep binds, from the reference state at the start of the step: the inputs,
     *  the crust arrays and the plate caps with their initial copies, the uniform buffers, and the outputs and counters (zero). The crust
     *  flags use the storage of `shaders`. Prints the cause and returns false when a file or a buffer fails. */
    [[nodiscard]] bool AddStepBuffers(GpuBuffers& buffers,
                                      const PlanetShaders& shaders,
                                      const ReferenceSet& reference,
                                      StepReadback readback,
                                      float time_step_my = 2.0f);

    /** @brief Resamples the reference state onto a nested HEALPix grid of `order` and creates every buffer that
     *  PlanetStep binds, including PlateCapCenters, PlateCapSpread, Params (with HealpixOrder), and optional Initial.*
     *  copies. */
    [[nodiscard]] bool AddHealpixStepBuffers(GpuBuffers& buffers,
                                             const PlanetShaders& shaders,
                                             const ReferenceSet& reference,
                                             uint32_t order,
                                             CrustFlagStorage storage,
                                             bool include_initial_copies,
                                             float time_step_my = 2.0f,
                                             bool readable = false);

    [[nodiscard]] bool AddCrustFlagsBuffer(GpuBuffers& buffers,
                                           const ReferenceSet& reference,
                                           CrustFlagStorage storage,
                                           std::string_view alive_file,
                                           std::string_view continental_file,
                                           bool readable,
                                           bool with_initial = false);

    [[nodiscard]] bool AddCrustStateBuffer(GpuBuffers& buffers,
                                           const ReferenceSet& reference,
                                           std::string_view relief_file,
                                           std::string_view birth_file,
                                           std::string_view orogeny_time_file,
                                           std::string_view orogeny_file,
                                           bool readable,
                                           bool with_initial = false);

    [[nodiscard]] bool AddPoleRatesBuffer(GpuBuffers& buffers, const ReferenceSet& reference, bool readable = false);

    [[nodiscard]] bool AddOwnersBuffer(GpuBuffers& buffers,
                                       const ReferenceSet& reference,
                                       std::string_view best_plate_file,
                                       std::string_view best_cell_file,
                                       std::string_view second_plate_file,
                                       std::string_view second_cell_file,
                                       bool readable);

    [[nodiscard]] bool AddFrontDataBuffer(GpuBuffers& buffers,
                                          const ReferenceSet& reference,
                                          std::string_view strength_file,
                                          std::string_view speed_file,
                                          std::string_view slab_age_file,
                                          std::string_view subduction_file,
                                          bool readable);

    [[nodiscard]] bool AddCellFrontsBuffer(GpuBuffers& buffers,
                                           const ReferenceSet& reference,
                                           std::string_view front_point_file,
                                           std::string_view distance_file,
                                           bool readable);

}

#endif // !DIAMOND_DOGS_PLANET_STEP_STATE_HPP
