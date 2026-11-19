#include "StepState.hpp"
#include "GpuBuffers.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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

        [[nodiscard]] bool AddUniform(GpuBuffers& buffers, const PlanetShaders& shaders, std::string_view entry_point, std::string_view name,
                                      std::span<const UniformValue> values)
        {
            PlanetGpuResult<std::vector<std::byte>> bytes = shaders.UniformBytes(entry_point, name, values);
            return bytes.has_value() && buffers.AddUniform(name, *bytes) == PlanetGpuError::Success;
        }
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
