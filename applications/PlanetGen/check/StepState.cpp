#include "StepState.hpp"
#include "GpuBuffers.hpp"
#include "PlanetMath.hpp"
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
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace planet
{
    static_assert(sizeof(math::Float4) == 4u * sizeof(float));

    namespace
    {
        template<typename T>
        [[nodiscard]] std::vector<std::byte> ToBytes(const std::vector<T>& values);

        [[nodiscard]] std::optional<std::vector<std::byte>> ReadIntsAsBytes(const ReferenceSet& reference, std::string_view file);
        [[nodiscard]] std::optional<std::vector<std::byte>> ReadWordsAsBytes(const ReferenceSet& reference, std::string_view file);
        [[nodiscard]] std::optional<std::vector<std::byte>> ReadFloatsAsBytes(const ReferenceSet& reference, std::string_view file);
        [[nodiscard]] std::optional<std::vector<std::byte>> ReadFlagsAsBytes(const ReferenceSet& reference,
                                                                             std::string_view file,
                                                                             CrustFlagStorage storage);
    }

    PlateCaps ComputePlateCaps(std::span<const uint8_t> alive, int32_t point_count, int32_t plate_count)
    {
        PlateCaps caps
        {
            std::vector<math::Float4>(static_cast<size_t>(plate_count), math::Float4{ 0.0f, 0.0f, 0.0f, k_CapNone }),
            std::vector<uint32_t>(static_cast<size_t>(plate_count), 0u),
        };

        for (int32_t plate = 0; plate < plate_count; ++plate)
        {
            const size_t plateOffset = static_cast<size_t>(plate) * static_cast<size_t>(point_count);
            const std::span<const uint8_t> cells = alive.subspan(plateOffset, static_cast<size_t>(point_count));
            Double3 sum{};
            int32_t live = 0;

            for (int32_t cell = 0; cell < point_count; ++cell)
            {
                if (cells[static_cast<size_t>(cell)] == 0u)
                {
                    continue;
                }

                const Double3 point = LatticePointDouble(cell, point_count);
                sum.X += point.X;
                sum.Y += point.Y;
                sum.Z += point.Z;
                ++live;
            }

            if (live == 0)
            {
                caps.Centres[static_cast<size_t>(plate)].w = k_CapEmpty;
                continue;
            }

            // live cells spread evenly over the sphere have no useful centre: the plate keeps no cap
            const double length = Length(sum);

            if (length < 1.0e-3 * static_cast<double>(live))
            {
                continue;
            }

            const math::Float3 unit
            {
                static_cast<float>(sum.X / length),
                static_cast<float>(sum.Y / length),
                static_cast<float>(sum.Z / length),
            };
            double spread = 0.0;

            // the spread is measured from the float centre that the shader reads
            for (int32_t cell = 0; cell < point_count; ++cell)
            {
                if (cells[static_cast<size_t>(cell)] == 0u)
                {
                    continue;
                }

                const Double3 point = LatticePointDouble(cell, point_count);
                const double cosine = Dot(point, unit);
                spread = std::max(spread, 1.0 - cosine);
            }

            caps.Centres[static_cast<size_t>(plate)] = math::Float4{ unit, k_CapValid };
            const float padded = std::nextafter(static_cast<float>(spread), std::numeric_limits<float>::infinity());
            caps.Spreads[static_cast<size_t>(plate)] = std::bit_cast<uint32_t>(padded);
        }

        return caps;
    }

    size_t FlagElementSize(CrustFlagStorage storage) noexcept
    {
        return storage == CrustFlagStorage::Bytes ? sizeof(uint8_t) : sizeof(uint32_t);
    }

    size_t ReferenceElementSize(ReferenceArrayForm form, CrustFlagStorage storage) noexcept
    {
        switch (form)
        {
        case ReferenceArrayForm::Int:
            return sizeof(int32_t);
        case ReferenceArrayForm::Word:
            return sizeof(uint32_t);
        case ReferenceArrayForm::Float:
            return sizeof(float);
        case ReferenceArrayForm::Flag:
            return FlagElementSize(storage);
        default:
            return 0u;
        }
    }

    std::optional<std::vector<std::byte>> ReadReferenceBytes(const ReferenceSet& reference,
                                                             std::string_view file,
                                                             ReferenceArrayForm form,
                                                             CrustFlagStorage storage)
    {
        switch (form)
        {
        case ReferenceArrayForm::Int:
            return ReadIntsAsBytes(reference, file);
        case ReferenceArrayForm::Word:
            return ReadWordsAsBytes(reference, file);
        case ReferenceArrayForm::Float:
            return ReadFloatsAsBytes(reference, file);
        case ReferenceArrayForm::Flag:
            return ReadFlagsAsBytes(reference, file, storage);
        default:
            return std::nullopt;
        }
    }

    bool AddReferenceBuffers(GpuBuffers& buffers,
                             const ReferenceSet& reference,
                             CrustFlagStorage storage,
                             std::span<const ReferenceBufferSpec> specs)
    {
        for (const ReferenceBufferSpec& spec : specs)
        {
            const std::optional<std::vector<std::byte>> bytes =
                ReadReferenceBytes(reference, spec.FileName, spec.Form, storage);

            if (!bytes.has_value() ||
                buffers.AddStorage(spec.BufferName, *bytes, spec.Readable) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        return true;
    }

    bool AddParamsUniform(GpuBuffers& buffers,
                          const PlanetShaders& shaders,
                          std::string_view entry_point,
                          StepSize size)
    {
        const std::array<UniformValue, 2> values
        {
            UniformUint("PointCount", size.PointCount),
            UniformUint("PlateCount", size.PlateCount),
        };
        return buffers.AddUniformFrom(shaders, entry_point, "Params", values) == PlanetGpuError::Success;
    }

    bool AddTimeUniform(GpuBuffers& buffers,
                        const PlanetShaders& shaders,
                        std::string_view entry_point,
                        std::string_view resource,
                        float time)
    {
        const std::array<UniformValue, 1> values{ UniformFloat("Time", time) };
        return buffers.AddUniformFrom(shaders, entry_point, resource, values) == PlanetGpuError::Success;
    }

    bool AddRotationsBuffer(GpuBuffers& buffers, const ReferenceSet& reference)
    {
        const std::optional<std::vector<math::Float3x3>> rotations = reference.Float3x3s("rotation");

        if (!rotations.has_value())
        {
            return false;
        }

        std::vector<math::Float4> rows;
        rows.reserve(rotations->size() * 3u);

        for (const math::Float3x3& matrix : *rotations)
        {
            rows.emplace_back(matrix.Row(0u), 0.0f);
            rows.emplace_back(matrix.Row(1u), 0.0f);
            rows.emplace_back(matrix.Row(2u), 0.0f);
        }

        return buffers.AddStorage("Rotations", std::as_bytes(std::span{ rows }), false) == PlanetGpuError::Success;
    }

    bool AddStepBuffers(GpuBuffers& buffers, const PlanetShaders& shaders, const ReferenceSet& reference, StepReadback readback)
    {
        const StepSize size
        {
            static_cast<uint32_t>(reference.PointCount()),
            static_cast<uint32_t>(reference.PlateCount()),
        };
        const CrustFlagStorage storage = shaders.CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const bool readable = readback == StepReadback::Outputs;

        // the uniform buffers: the sizes, and the time of the step for passes 3 and 5
        if (!AddParamsUniform(buffers, shaders, "OwnersCS", size) ||
            !AddTimeUniform(buffers, shaders, "FillGapsCS", "Pass3Constants", reference.Time()) ||
            !AddTimeUniform(buffers, shaders, "UpliftCS", "UpliftConstants", reference.Time()))
        {
            return false;
        }

        // the inputs that no pass writes
        const std::array<ReferenceBufferSpec, 3> inputs
        {
            ReferenceBufferSpec{ "PreviousPlate", "previous_plate", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "Poles", "poles", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ "Rates", "rates", ReferenceArrayForm::Float, false },
        };

        if (!AddRotationsBuffer(buffers, reference) || !AddReferenceBuffers(buffers, reference, storage, inputs))
        {
            return false;
        }

        // the crust arrays, each with the initial copy that RecordReset restores it from
        const std::array<ReferenceBufferSpec, 6> crust
        {
            ReferenceBufferSpec{ flagNames.Alive, "ref_alive", ReferenceArrayForm::Flag, readable },
            ReferenceBufferSpec{ flagNames.Continental, "ref_continental", ReferenceArrayForm::Flag, readable },
            ReferenceBufferSpec{ "CrustBirth", "ref_birth", ReferenceArrayForm::Float, readable },
            ReferenceBufferSpec{ "CrustRelief", "ref_relief", ReferenceArrayForm::Float, readable },
            ReferenceBufferSpec{ "CrustOrogeny", "ref_orogeny", ReferenceArrayForm::Int, readable },
            ReferenceBufferSpec{ "CrustOrogenyTime", "ref_orogeny_time", ReferenceArrayForm::Float, readable },
        };

        for (const ReferenceBufferSpec& input : crust)
        {
            const std::optional<std::vector<std::byte>> bytes =
                ReadReferenceBytes(reference, input.FileName, input.Form, storage);

            if (!bytes.has_value() ||
                buffers.AddStorage(input.BufferName, *bytes, input.Readable) != PlanetGpuError::Success ||
                buffers.AddStorage(InitialName(input.BufferName), *bytes, false) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        // the plate caps of the start state, with their initial copies: pass 3b of each step writes the next step's caps
        const std::optional<std::vector<uint8_t>> alive = reference.Bytes("ref_alive");

        if (!alive.has_value())
        {
            return false;
        }

        const PlateCaps caps = ComputePlateCaps(*alive, reference.PointCount(), reference.PlateCount());
        const std::span<const std::string_view> carried = StepCarriedBuffers();
        const std::array<std::span<const std::byte>, 2> capBytes
        {
            std::as_bytes(std::span{ caps.Centres }),
            std::as_bytes(std::span{ caps.Spreads }),
        };

        if (carried.size() != capBytes.size())
        {
            std::println(stderr, "AddStepBuffers: the plate caps differ from StepCarriedBuffers");
            return false;
        }

        for (int32_t index = 0; index < std::ssize(carried); ++index)
        {
            const size_t entry = static_cast<size_t>(index);

            if (buffers.AddStorage(carried[entry], capBytes[entry], readable) != PlanetGpuError::Success ||
                buffers.AddStorage(InitialName(carried[entry]), capBytes[entry], false) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        // the outputs and counters, as 32-bit words. RecordReset zeroes them again before each step
        for (const StepBufferSpec& spec : StepZeroedBuffers())
        {
            const size_t byteSize = StepBufferWordCount(spec.Extent, size) * sizeof(uint32_t);

            if (buffers.AddZeroedStorage(spec.Name, byteSize, readable) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        // the live cell list has room for every cell. ScatterLiveCellsCS writes the first PlateLiveOffset[plates]
        // entries, so the reset does not clear it
        const size_t cellBytes = StepBufferWordCount(BufferExtent::Cells, size) * sizeof(uint32_t);
        return buffers.AddZeroedStorage("LiveCell", cellBytes, readable) == PlanetGpuError::Success;
    }

    namespace
    {
        template<typename T>
        std::vector<std::byte> ToBytes(const std::vector<T>& values)
        {
            const std::span<const std::byte> bytes = std::as_bytes(std::span{ values });
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        }

        std::optional<std::vector<std::byte>> ReadIntsAsBytes(const ReferenceSet& reference, std::string_view file)
        {
            const std::optional<std::vector<int32_t>> ints = reference.Ints(file);
            return ints.has_value() ? std::optional{ ToBytes(*ints) } : std::nullopt;
        }

        std::optional<std::vector<std::byte>> ReadWordsAsBytes(const ReferenceSet& reference, std::string_view file)
        {
            const std::optional<std::vector<uint32_t>> words = reference.Words(file);
            return words.has_value() ? std::optional{ ToBytes(*words) } : std::nullopt;
        }

        std::optional<std::vector<std::byte>> ReadFloatsAsBytes(const ReferenceSet& reference, std::string_view file)
        {
            const std::optional<std::vector<float>> floats = reference.Floats(file);
            return floats.has_value() ? std::optional{ ToBytes(*floats) } : std::nullopt;
        }

        std::optional<std::vector<std::byte>> ReadFlagsAsBytes(const ReferenceSet& reference,
                                                               std::string_view file,
                                                               CrustFlagStorage storage)
        {
            const std::optional<std::vector<uint8_t>> flags = reference.Bytes(file);

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
    }
}
