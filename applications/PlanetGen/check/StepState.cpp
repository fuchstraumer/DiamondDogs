#include "StepState.hpp"
#include "GpuBuffers.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace planet
{
    namespace
    {
        enum class InputForm : uint8_t
        {
            Invalid = 0,
            Int,
            Float,
            Flag,
        };

        struct CrustInput
        {
            std::string_view Buffer;
            std::string_view File;
            InputForm Form{ InputForm::Invalid };
        };

        // a buffer that RecordReset fills with zero, and its element count
        struct ZeroedOutput
        {
            std::string_view Name;
            size_t Count{ 0u };
        };

        template<typename T>
        [[nodiscard]] std::vector<std::byte> ToBytes(const std::vector<T>& values)
        {
            const std::span<const std::byte> bytes = std::as_bytes(std::span{ values });
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        }

        // a crust array as the shader stores it: a flag in the variant's storage, an int8 array widened to int
        [[nodiscard]] std::optional<std::vector<std::byte>> ReadInput(const ReferenceSet& reference, std::string_view file, InputForm form, CrustFlagStorage storage)
        {
            if (form == InputForm::Flag)
            {
                std::optional<std::vector<uint8_t>> flags = reference.Bytes(file);

                if (!flags.has_value())
                {
                    return std::nullopt;
                }

                if (storage == CrustFlagStorage::Bytes)
                {
                    return ToBytes(*flags);
                }

                return ToBytes(std::vector<uint32_t>(flags->begin(), flags->end()));
            }

            if (form == InputForm::Int)
            {
                std::optional<std::vector<int32_t>> ints = reference.Ints(file);
                return ints.has_value() ? std::optional{ ToBytes(*ints) } : std::nullopt;
            }

            std::optional<std::vector<float>> floats = reference.Floats(file);
            return floats.has_value() ? std::optional{ ToBytes(*floats) } : std::nullopt;
        }

        // lattice point `index` of `count`, in double: z = 1 - (2i + 1) / N, azimuth (i + 0.5) golden angles
        [[nodiscard]] std::array<double, 3> LatticePointDouble(size_t index, size_t count)
        {
            const double goldenAngle = std::numbers::pi * (3.0 - std::sqrt(5.0));
            const double z = 1.0 - (2.0 * static_cast<double>(index) + 1.0) / static_cast<double>(count);
            const double radius = std::sqrt(std::max(1.0 - z * z, 0.0));
            const double angle = (static_cast<double>(index) + 0.5) * goldenAngle;
            return { radius * std::cos(angle), radius * std::sin(angle), z };
        }

        [[nodiscard]] bool AddUniform(GpuBuffers& buffers, const PlanetShaders& shaders, std::string_view entry_point, std::string_view name,
                                      std::span<const UniformValue> values)
        {
            PlanetGpuResult<std::vector<std::byte>> bytes = shaders.UniformBytes(entry_point, name, values);
            return bytes.has_value() && buffers.AddUniform(name, *bytes) == PlanetGpuError::Success;
        }
    }

    PlateCaps ComputePlateCaps(std::span<const uint8_t> alive, int32_t point_count, int32_t plate_count)
    {
        const size_t points = static_cast<size_t>(point_count);
        const size_t plates = static_cast<size_t>(plate_count);
        PlateCaps caps{ std::vector<float>(plates * 4u, 0.0f), std::vector<uint32_t>(plates, 0u) };

        for (size_t plate = 0; plate < plates; ++plate)
        {
            const std::span<const uint8_t> cells = alive.subspan(plate * points, points);
            std::array<double, 3> sum{ 0.0, 0.0, 0.0 };
            size_t live = 0u;

            for (size_t cell = 0; cell < points; ++cell)
            {
                if (cells[cell] == 0u)
                {
                    continue;
                }

                const std::array<double, 3> point = LatticePointDouble(cell, points);
                sum[0] += point[0];
                sum[1] += point[1];
                sum[2] += point[2];
                ++live;
            }

            float* centre = caps.Centres.data() + plate * 4u;

            if (live == 0u)
            {
                centre[3] = k_CapEmpty;
                continue;
            }

            // live cells spread evenly over the sphere have no useful centre: the plate keeps no cap
            const double length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);

            if (length < 1.0e-3 * static_cast<double>(live))
            {
                continue;
            }

            const std::array<float, 3> unit
            {
                static_cast<float>(sum[0] / length), static_cast<float>(sum[1] / length), static_cast<float>(sum[2] / length),
            };
            double spread = 0.0;

            // the spread is measured from the float centre that the shader reads
            for (size_t cell = 0; cell < points; ++cell)
            {
                if (cells[cell] == 0u)
                {
                    continue;
                }

                const std::array<double, 3> point = LatticePointDouble(cell, points);
                const double cosine = point[0] * unit[0] + point[1] * unit[1] + point[2] * unit[2];
                spread = std::max(spread, 1.0 - cosine);
            }

            centre[0] = unit[0];
            centre[1] = unit[1];
            centre[2] = unit[2];
            centre[3] = k_CapValid;
            caps.Spreads[plate] = std::bit_cast<uint32_t>(std::nextafter(static_cast<float>(spread), std::numeric_limits<float>::infinity()));
        }

        return caps;
    }

    bool AddStepBuffers(GpuBuffers& buffers, const PlanetShaders& shaders, const ReferenceSet& reference, StepReadback readback)
    {
        const size_t points = static_cast<size_t>(reference.PointCount());
        const size_t plates = static_cast<size_t>(reference.PlateCount());
        const CrustFlagStorage storage = shaders.CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const bool readable = readback == StepReadback::Outputs;

        // the uniform buffers: the sizes, and the time of the step for passes 3 and 5
        const std::array<UniformValue, 2> sizes
        {
            UniformUint("PointCount", static_cast<uint32_t>(points)),
            UniformUint("PlateCount", static_cast<uint32_t>(plates)),
        };
        const std::array<UniformValue, 1> time{ UniformFloat("Time", reference.Time()) };

        if (!AddUniform(buffers, shaders, "OwnersCS", "Params", sizes) ||
            !AddUniform(buffers, shaders, "FillGapsCS", "Pass3Constants", time) ||
            !AddUniform(buffers, shaders, "UpliftCS", "UpliftConstants", time))
        {
            return false;
        }

        // the inputs that no pass writes
        std::optional<std::vector<float>> rotation = reference.Floats("rotation");
        std::optional<std::vector<int32_t>> previous = reference.Ints("previous_plate");
        std::optional<std::vector<float>> poles = reference.Floats("poles");
        std::optional<std::vector<float>> rates = reference.Floats("rates");

        if (!rotation.has_value() || !previous.has_value() || !poles.has_value() || !rates.has_value())
        {
            return false;
        }

        if (buffers.AddStorage("Rotation", std::as_bytes(std::span{ *rotation }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("PreviousPlate", std::as_bytes(std::span{ *previous }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("Poles", std::as_bytes(std::span{ *poles }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("Rates", std::as_bytes(std::span{ *rates }), false) != PlanetGpuError::Success)
        {
            return false;
        }

        // the crust arrays, each with the initial copy that RecordReset restores it from
        const std::array<CrustInput, 6> crust
        {
            CrustInput{ flagNames.Alive, "ref_alive", InputForm::Flag },
            CrustInput{ flagNames.Continental, "ref_continental", InputForm::Flag },
            CrustInput{ "CrustBirth", "ref_birth", InputForm::Float },
            CrustInput{ "CrustRelief", "ref_relief", InputForm::Float },
            CrustInput{ "CrustOrogeny", "ref_orogeny", InputForm::Int },
            CrustInput{ "CrustOrogenyTime", "ref_orogeny_time", InputForm::Float },
        };

        for (const CrustInput& input : crust)
        {
            std::optional<std::vector<std::byte>> bytes = ReadInput(reference, input.File, input.Form, storage);

            if (!bytes.has_value() ||
                buffers.AddStorage(input.Buffer, *bytes, readable) != PlanetGpuError::Success ||
                buffers.AddStorage(InitialName(input.Buffer), *bytes, false) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        // the plate caps of the start state, with their initial copies: pass 3b of each step writes the next step's caps
        std::optional<std::vector<uint8_t>> alive = reference.Bytes("ref_alive");

        if (!alive.has_value())
        {
            return false;
        }

        const PlateCaps caps = ComputePlateCaps(*alive, reference.PointCount(), reference.PlateCount());
        const std::span<const std::string_view> carried = StepCarriedBuffers();
        const std::array<std::span<const std::byte>, 2> capBytes{ std::as_bytes(std::span{ caps.Centres }), std::as_bytes(std::span{ caps.Spreads }) };

        if (carried.size() != capBytes.size())
        {
            std::println(stderr, "AddStepBuffers: the plate caps differ from StepCarriedBuffers");
            return false;
        }

        for (size_t index = 0; index < carried.size(); ++index)
        {
            if (buffers.AddStorage(carried[index], capBytes[index], readable) != PlanetGpuError::Success ||
                buffers.AddStorage(InitialName(carried[index]), capBytes[index], false) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        // the outputs and counters, as 32-bit words. RecordReset zeroes them again before each step
        const std::array<ZeroedOutput, 25> outputs
        {
            ZeroedOutput{ "BestPlate", points },
            ZeroedOutput{ "BestCell", points },
            ZeroedOutput{ "SecondPlate", points },
            ZeroedOutput{ "SecondCell", points },
            ZeroedOutput{ "PairVotes", plates * plates },
            ZeroedOutput{ "LowVotes", plates * plates },
            ZeroedOutput{ "Pass3Plate", points },
            ZeroedOutput{ "Pass3Cell", points },
            ZeroedOutput{ "FrontSubduction", points },
            ZeroedOutput{ "FrontStrength", points },
            ZeroedOutput{ "FrontSpeed", points },
            ZeroedOutput{ "FrontSlabAge", points },
            ZeroedOutput{ "PlateLiveCursor", plates },
            ZeroedOutput{ "PlateLiveOffset", plates + 1u },
            // the three group counts of an indirect dispatch
            ZeroedOutput{ "LiveDispatch", 3u },
            ZeroedOutput{ "PlateFrontPointCount", plates },
            ZeroedOutput{ "PlateFrontPointCursor", plates },
            ZeroedOutput{ "PlateFrontPointOffset", plates + 1u },
            ZeroedOutput{ "FrontPoints", points * 4u },
            ZeroedOutput{ "CellFrontPoint", plates * points },
            ZeroedOutput{ "CellDistance", plates * points },
            ZeroedOutput{ "PlateCellCount", plates },
            ZeroedOutput{ "PlateContinentalCount", plates },
            ZeroedOutput{ "PlatePositionSum", plates * 3u },
            ZeroedOutput{ "PlatePull", plates * 3u },
        };

        const std::span<const std::string_view> zeroed = StepZeroedBuffers();

        // this list and the one that RecordReset fills must name the same buffers
        if (zeroed.size() != outputs.size() || !std::ranges::equal(zeroed, outputs, {}, {}, &ZeroedOutput::Name))
        {
            std::println(stderr, "AddStepBuffers: the outputs differ from StepZeroedBuffers");
            return false;
        }

        for (const ZeroedOutput& output : outputs)
        {
            const std::vector<uint32_t> zeros(output.Count, 0u);

            if (buffers.AddStorage(output.Name, std::as_bytes(std::span{ zeros }), readable) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        // the live cell list has room for every cell. ScatterLiveCellsCS writes the first PlateLiveOffset[plates]
        // entries, so the reset does not clear it
        const std::vector<uint32_t> liveCells(plates * points, 0u);
        return buffers.AddStorage("LiveCell", std::as_bytes(std::span{ liveCells }), readable) == PlanetGpuError::Success;
    }
}
