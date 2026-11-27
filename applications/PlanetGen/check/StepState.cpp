#include "StepState.hpp"
#include "GpuBuffers.hpp"
#include "Healpix.hpp"
#include "PlanetGpuErrors.hpp"
#include "PlanetMath.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include "ShaderLibrary.host.hpp"
#include "math/MathStorage.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

        template<typename PointAtFn>
        [[nodiscard]] PlateCaps ComputePlateCapsFor(std::span<const uint8_t> alive,
                                                    int32_t point_count,
                                                    int32_t plate_count,
                                                    PointAtFn&& point_at)
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

                    const Double3 point = point_at(cell);
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

                    const Double3 point = point_at(cell);
                    const double cosine = Dot(point, unit);
                    spread = std::max(spread, 1.0 - cosine);
                }

                caps.Centres[static_cast<size_t>(plate)] = math::Float4{ unit, k_CapValid };
                const float padded = std::nextafter(static_cast<float>(spread), std::numeric_limits<float>::infinity());
                caps.Spreads[static_cast<size_t>(plate)] = std::bit_cast<uint32_t>(padded);
            }

            return caps;
        }

        [[nodiscard]] bool AddCarriedAndZeroedStepBuffers(GpuBuffers& buffers,
                                                         StepSize size,
                                                         const PlateCaps& caps,
                                                         std::span<const std::byte> previous_plate_bytes,
                                                         bool readable,
                                                         bool include_initial_copies)
        {
            const std::span<const std::string_view> carried = StepCarriedBuffers();
            const std::array<std::span<const std::byte>, 3> carriedBytes
            {
                std::as_bytes(std::span{ caps.Centres }),
                std::as_bytes(std::span{ caps.Spreads }),
                previous_plate_bytes,
            };

            if (carried.size() != carriedBytes.size())
            {
                std::println(stderr, "AddStepBuffers: the carried buffers differ from StepCarriedBuffers");
                return false;
            }

            for (size_t entry = 0u; entry < carried.size(); ++entry)
            {
                if (buffers.AddStorage(carried[entry], carriedBytes[entry], readable) != PlanetGpuError::Success ||
                    (include_initial_copies &&
                     buffers.AddStorage(InitialName(carried[entry]), carriedBytes[entry], false) != PlanetGpuError::Success))
                {
                    return false;
                }
            }

            const size_t riftBytes = static_cast<size_t>(size.PlateCount) * 4u * sizeof(float);

            if (buffers.AddZeroedStorage("PlateRiftIn", riftBytes, false) != PlanetGpuError::Success)
            {
                return false;
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
    }

    PlateCaps ComputePlateCaps(std::span<const uint8_t> alive, int32_t point_count, int32_t plate_count)
    {
        return ComputePlateCapsFor(alive, point_count, plate_count, [point_count](int32_t cell) noexcept
        {
            return LatticePointDouble(cell, point_count);
        });
    }

    PlateCaps ComputeHealpixPlateCaps(std::span<const uint8_t> alive, uint32_t order, int32_t plate_count)
    {
        const int32_t pointCount = static_cast<int32_t>(HealpixPixelCount(order));
        return ComputePlateCapsFor(alive, pointCount, plate_count, [order](int32_t cell) noexcept
        {
            const std::array<double, 3> vec = HealpixPixToVec(static_cast<uint32_t>(cell), order);
            return Double3{ vec[0], vec[1], vec[2] };
        });
    }

    bool AddDefaultShapingUniform(GpuBuffers& buffers)
    {
        using ShapingParamsHost = planet_gen_shaders::ShapingParams<
            planet_gen_shaders::CookProfile::Spirv,
            ::lodestone::LayoutRule::Std140>;

        ShapingParamsHost shaping{};
        shaping.BaseUplift = 0.6f;
        shaping.ContinentErosion = 0.06f;
        shaping.AccretionRelief = 1.5f;
        shaping.AccretionStartRelief = 0.3f;
        shaping.ContinentalReachKm = 1800.0f;
        shaping.IslandArcReachKm = 450.0f;
        shaping.ArcDistanceKm = 250.0f;
        shaping.CollisionReachKm = 900.0f;
        shaping.CollisionUpliftScale = 0.65f;
        shaping.SlabDipAgeWeight = 0.0f;
        shaping.SlabDipSpeedWeight = 0.0f;
        shaping.TrenchDepthKm = 0.0f;
        shaping.CoastalWarpAmplitude = 0.0f;
        shaping.CoastalDetailKm = 0.0f;
        shaping.AtollLagoonStrength = 0.0f;
        shaping.NoiseFrequency = 6.0f;
        shaping.Flags = 0u;
        shaping.NoiseSeed = 1337u;
        shaping.AtollLandBias = 0.0f;
        shaping.OceanErosion = 0.0f;

        for (uint32_t i = 0u; i < 4u; ++i)
        {
            shaping.ProfileLut[i] = math::Float4{ 0.0f, 0.0f, 0.0f, 0.0f };
        }

        return buffers.AddUniform("ShapingConstants", std::as_bytes(std::span{ &shaping, 1u })) == PlanetGpuError::Success;
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
                          StepSize size,
                          float time_step_my,
                          uint32_t healpix_order)
    {
        const std::array<UniformValue, 4> values
        {
            UniformUint("PointCount", size.PointCount),
            UniformUint("PlateCount", size.PlateCount),
            UniformFloat("TimeStepMy", time_step_my),
            UniformUint("HealpixOrder", healpix_order),
        };
        return buffers.AddUniformFrom(shaders, entry_point, "StepParams", values) == PlanetGpuError::Success;
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

    std::vector<uint32_t> HealpixPointOrder(int32_t point_count)
    {
        struct KeyedPoint
        {
            uint64_t Pixel{ 0u };
            uint32_t Point{ 0u };
        };

        const uint32_t order = HealpixOrderFor(static_cast<uint32_t>(point_count), k_OrderPixelsPerPoint);
        std::vector<KeyedPoint> keyed;
        keyed.reserve(static_cast<size_t>(point_count));

        for (int32_t point = 0; point < point_count; ++point)
        {
            const Double3 direction = LatticePointDouble(point, point_count);
            keyed.push_back(KeyedPoint{ HealpixNestedIndex(direction.X, direction.Y, direction.Z, order), static_cast<uint32_t>(point) });
        }

        const auto byPixelThenPoint = [](const KeyedPoint& lhs, const KeyedPoint& rhs)
        {
            return lhs.Pixel != rhs.Pixel ? lhs.Pixel < rhs.Pixel : lhs.Point < rhs.Point;
        };

        std::ranges::sort(keyed, byPixelThenPoint);

        std::vector<uint32_t> result;
        result.reserve(keyed.size());

        for (const KeyedPoint& entry : keyed)
        {
            result.push_back(entry.Point);
        }

        return result;
    }

    bool AddOwnersOrderBuffer(GpuBuffers& buffers, int32_t point_count)
    {
        const std::vector<uint32_t> order = HealpixPointOrder(point_count);
        return buffers.AddStorage("OwnersOrder", std::as_bytes(std::span{ order }), false) == PlanetGpuError::Success;
    }

    bool AddStepBuffers(GpuBuffers& buffers,
                        const PlanetShaders& shaders,
                        const ReferenceSet& reference,
                        StepReadback readback,
                        float time_step_my)
    {
        const StepSize size
        {
            static_cast<uint32_t>(reference.PointCount()),
            static_cast<uint32_t>(reference.PlateCount()),
        };
        const CrustFlagStorage storage = shaders.CrustFlags();
        const bool readable = readback == StepReadback::Outputs;

        // the uniform buffers: the sizes, and the time of the step for passes 3 and 5
        if (!AddParamsUniform(buffers, shaders, "OwnersCS", size, time_step_my) ||
            !AddTimeUniform(buffers, shaders, "FillGapsCS", "Pass3Constants", reference.Time()) ||
            !AddTimeUniform(buffers, shaders, "FrontDistanceAndUpliftCS", "UpliftConstants", reference.Time()) ||
            !AddDefaultShapingUniform(buffers))
        {
            return false;
        }

        if (!AddRotationsBuffer(buffers, reference) ||
            !AddOwnersOrderBuffer(buffers, reference.PointCount()) ||
            !AddPoleRatesBuffer(buffers, reference) ||
            !AddCrustFlagsBuffer(buffers, reference, storage, "ref_alive", "ref_continental", readable, true) ||
            !AddCrustStateBuffer(buffers, reference, "ref_relief", "ref_birth", "ref_orogeny_time", "ref_orogeny", readable, true))
        {
            return false;
        }

        // the plate caps and previous plate of the start state, with their initial copies: passes 3 and 3b write the next step's
        const std::optional<std::vector<uint8_t>> alive = reference.Bytes("ref_alive");
        const std::optional<std::vector<std::byte>> previousPlate =
            ReadReferenceBytes(reference, "previous_plate", ReferenceArrayForm::Int, storage);

        if (!alive.has_value() || !previousPlate.has_value())
        {
            return false;
        }

        const PlateCaps caps = ComputePlateCaps(*alive, reference.PointCount(), reference.PlateCount());
        return AddCarriedAndZeroedStepBuffers(buffers, size, caps, *previousPlate, readable, true);
    }

    std::optional<HealpixInitialState> ResampleHealpixState(const ReferenceSet& reference,
                                                            uint32_t order,
                                                            CrustFlagStorage storage)
    {
        if (order > 13u || reference.PointCount() <= 0 || reference.PlateCount() <= 0 ||
            (storage != CrustFlagStorage::Bytes && storage != CrustFlagStorage::Words))
        {
            return std::nullopt;
        }

        const uint32_t pointCount = static_cast<uint32_t>(HealpixPixelCount(order));
        const int32_t refPointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        const size_t refCellCount = static_cast<size_t>(plateCount) * static_cast<size_t>(refPointCount);

        const std::optional<std::vector<math::Float3x3>> rotations = reference.Float3x3s("rotation");
        const std::optional<std::vector<math::Float3>> poles = reference.Float3s("poles");
        const std::optional<std::vector<float>> rates = reference.Floats("rates");
        const std::optional<std::vector<uint8_t>> refAlive = reference.Bytes("ref_alive");
        const std::optional<std::vector<uint8_t>> refContinental = reference.Bytes("ref_continental");
        const std::optional<std::vector<float>> refRelief = reference.Floats("ref_relief");
        const std::optional<std::vector<float>> refBirth = reference.Floats("ref_birth");
        const std::optional<std::vector<float>> refOrogenyTime = reference.Floats("ref_orogeny_time");
        const std::optional<std::vector<int32_t>> refOrogeny = reference.Ints("ref_orogeny");
        const std::optional<std::vector<int32_t>> refPreviousPlate = reference.Ints("previous_plate");

        if (!rotations.has_value() || rotations->size() != static_cast<size_t>(plateCount) ||
            !poles.has_value() || poles->size() != static_cast<size_t>(plateCount) ||
            !rates.has_value() || rates->size() != static_cast<size_t>(plateCount) ||
            !refAlive.has_value() || refAlive->size() != refCellCount ||
            !refContinental.has_value() || refContinental->size() != refCellCount ||
            !refRelief.has_value() || refRelief->size() != refCellCount ||
            !refBirth.has_value() || refBirth->size() != refCellCount ||
            !refOrogenyTime.has_value() || refOrogenyTime->size() != refCellCount ||
            !refOrogeny.has_value() || refOrogeny->size() != refCellCount ||
            !refPreviousPlate.has_value() || refPreviousPlate->size() != static_cast<size_t>(refPointCount))
        {
            return std::nullopt;
        }

        HealpixInitialState state;
        state.PointCount = pointCount;
        state.PlateCount = static_cast<uint32_t>(plateCount);
        state.InitialTime = reference.Time();

        state.Rotations.reserve(rotations->size() * 3u);
        for (const math::Float3x3& matrix : *rotations)
        {
            state.Rotations.emplace_back(matrix.Row(0u), 0.0f);
            state.Rotations.emplace_back(matrix.Row(1u), 0.0f);
            state.Rotations.emplace_back(matrix.Row(2u), 0.0f);
        }

        state.PoleRates.reserve(poles->size());
        for (size_t i = 0u; i < poles->size(); ++i)
        {
            state.PoleRates.emplace_back((*poles)[i], (*rates)[i]);
        }

        std::vector<size_t> srcIndices(pointCount);
        state.PreviousPlate.resize(pointCount);

        for (uint32_t i = 0u; i < pointCount; ++i)
        {
            const std::array<double, 3> vec = HealpixPixToVec(i, order);
            const Double3 dir{ vec[0], vec[1], vec[2] };
            const size_t srcIdx = static_cast<size_t>(FibonacciNearest(dir, refPointCount));
            srcIndices[i] = srcIdx;
            state.PreviousPlate[i] = (*refPreviousPlate)[srcIdx];
        }

        const size_t totalCells = static_cast<size_t>(plateCount) * static_cast<size_t>(pointCount);
        std::vector<uint8_t> healpixAlive(totalCells);
        std::vector<uint8_t> flagBytes;
        std::vector<uint32_t> flagWords;

        if (storage == CrustFlagStorage::Bytes)
        {
            flagBytes.resize(totalCells);
        }
        else
        {
            flagWords.resize(totalCells);
        }

        state.CrustState.resize(totalCells);

        for (int32_t p = 0; p < plateCount; ++p)
        {
            const size_t srcBase = static_cast<size_t>(p) * static_cast<size_t>(refPointCount);
            const size_t dstBase = static_cast<size_t>(p) * static_cast<size_t>(pointCount);

            for (uint32_t i = 0u; i < pointCount; ++i)
            {
                const size_t srcFlat = srcBase + srcIndices[i];
                const size_t dstFlat = dstBase + i;
                const uint8_t aliveVal = (*refAlive)[srcFlat] ? 1u : 0u;
                const uint8_t contVal = (*refContinental)[srcFlat] ? 2u : 0u;
                healpixAlive[dstFlat] = aliveVal;

                if (storage == CrustFlagStorage::Bytes)
                {
                    flagBytes[dstFlat] = static_cast<uint8_t>(aliveVal | contVal);
                }
                else
                {
                    flagWords[dstFlat] = static_cast<uint32_t>(aliveVal | contVal);
                }

                state.CrustState[dstFlat] = math::Float4
                {
                    (*refRelief)[srcFlat],
                    (*refBirth)[srcFlat],
                    (*refOrogenyTime)[srcFlat],
                    std::bit_cast<float>((*refOrogeny)[srcFlat]),
                };
            }
        }

        state.CrustFlags = storage == CrustFlagStorage::Bytes
            ? ToBytes(flagBytes)
            : ToBytes(flagWords);

        state.Caps = ComputePlateCapsFor(
            healpixAlive,
            static_cast<int32_t>(pointCount),
            plateCount,
            [order](int32_t cell) noexcept
            {
                const std::array<double, 3> vec = HealpixPixToVec(static_cast<uint32_t>(cell), order);
                return Double3{ vec[0], vec[1], vec[2] };
            });

        return state;
    }

    bool AddHealpixStepBuffers(GpuBuffers& buffers,
                               const PlanetShaders& shaders,
                               const ReferenceSet& reference,
                               uint32_t order,
                               CrustFlagStorage storage,
                               bool include_initial_copies,
                               float time_step_my,
                               bool readable)
    {
        const std::optional<HealpixInitialState> initial = ResampleHealpixState(reference, order, storage);

        if (!initial.has_value())
        {
            return false;
        }

        const StepSize size{ initial->PointCount, initial->PlateCount };

        if (!AddParamsUniform(buffers, shaders, "OwnersCS", size, time_step_my, order) ||
            !AddTimeUniform(buffers, shaders, "FillGapsCS", "Pass3Constants", initial->InitialTime) ||
            !AddTimeUniform(buffers, shaders, "FrontDistanceAndUpliftCS", "UpliftConstants", initial->InitialTime) ||
            !AddDefaultShapingUniform(buffers))
        {
            return false;
        }

        if (buffers.AddStorage("Rotations", std::as_bytes(std::span{ initial->Rotations }), readable) != PlanetGpuError::Success ||
            buffers.AddStorage("PoleRatesIn", std::as_bytes(std::span{ initial->PoleRates }), readable) != PlanetGpuError::Success)
        {
            return false;
        }

        const std::string_view flagBufferName = CrustFlagBufferNames(storage);

        if (buffers.AddStorage(flagBufferName, initial->CrustFlags, readable) != PlanetGpuError::Success ||
            (include_initial_copies &&
             buffers.AddStorage(InitialName(flagBufferName), initial->CrustFlags, false) != PlanetGpuError::Success))
        {
            return false;
        }

        const std::span<const std::byte> stateSpan = std::as_bytes(std::span{ initial->CrustState });

        if (buffers.AddStorage("CrustState", stateSpan, readable) != PlanetGpuError::Success ||
            (include_initial_copies &&
             buffers.AddStorage(InitialName("CrustState"), stateSpan, false) != PlanetGpuError::Success))
        {
            return false;
        }

        return AddCarriedAndZeroedStepBuffers(
            buffers,
            size,
            initial->Caps,
            std::as_bytes(std::span{ initial->PreviousPlate }),
            readable,
            include_initial_copies);
    }

    bool AddCrustFlagsBuffer(GpuBuffers& buffers,
                             const ReferenceSet& reference,
                             CrustFlagStorage storage,
                             std::string_view alive_file,
                             std::string_view continental_file,
                             bool readable,
                             bool with_initial)
    {
        const auto alive = reference.Bytes(alive_file);
        const auto continental = reference.Bytes(continental_file);

        if (!alive.has_value() ||
            !continental.has_value() ||
            (alive->size() != continental->size()))
        {
            return false;
        }

        const std::vector<uint8_t>& aliveBytes = *alive;
        const std::vector<uint8_t>& continentalBytes = *continental;

        const std::string_view bufferName = CrustFlagBufferNames(storage);
        std::vector<std::byte> bufferBytes;
        if (storage == CrustFlagStorage::Bytes)
        {
            std::vector<uint8_t> packed(aliveBytes.size());
            for (size_t i = 0; i < aliveBytes.size(); ++i)
            {
                const uint8_t aliveVal = aliveBytes[i] ? 1u : 0u;
                const uint8_t continentalVal = continentalBytes[i] ? 2u : 0u;
                packed[i] = static_cast<uint8_t>(aliveVal | continentalVal);
            }

            bufferBytes = ToBytes(packed);
        }
        else
        {
            std::vector<uint32_t> packed(aliveBytes.size());
            for (size_t i = 0; i < aliveBytes.size(); ++i)
            {
                const uint8_t aliveVal = aliveBytes[i] ? 1u : 0u;
                const uint8_t continentalVal = continentalBytes[i] ? 2u : 0u;
                packed[i] = static_cast<uint32_t>(aliveVal | continentalVal);
            }

            bufferBytes = ToBytes(packed);
        }

        if (buffers.AddStorage(bufferName, bufferBytes, readable) != PlanetGpuError::Success)
        {
            return false;
        }

        return !with_initial || buffers.AddStorage(InitialName(bufferName), bufferBytes, false) == PlanetGpuError::Success;
    }

    bool AddCrustStateBuffer(GpuBuffers& buffers,
                             const ReferenceSet& reference,
                             std::string_view relief_file,
                             std::string_view birth_file,
                             std::string_view orogeny_time_file,
                             std::string_view orogeny_file,
                             bool readable,
                             bool with_initial)
    {
        const size_t cellCount = static_cast<size_t>(reference.PlateCount()) * static_cast<size_t>(reference.PointCount());
        const auto relief = relief_file.empty() ? std::optional<std::vector<float>>{} : reference.Floats(relief_file);
        const auto birth = birth_file.empty() ? std::optional<std::vector<float>>{} : reference.Floats(birth_file);
        const auto orogenyTime = orogeny_time_file.empty() ? std::optional<std::vector<float>>{} : reference.Floats(orogeny_time_file);
        const auto orogeny = orogeny_file.empty() ? std::optional<std::vector<int32_t>>{} : reference.Ints(orogeny_file);

        if ((!relief_file.empty() && (!relief.has_value() || relief->size() != cellCount)) ||
            (!birth_file.empty() && (!birth.has_value() || birth->size() != cellCount)) ||
            (!orogeny_time_file.empty() && (!orogenyTime.has_value() || orogenyTime->size() != cellCount)) ||
            (!orogeny_file.empty() && (!orogeny.has_value() || orogeny->size() != cellCount)))
        {
            return false;
        }

        std::vector<math::Float4> packed(cellCount);

        for (size_t i = 0u; i < cellCount; ++i)
        {
            const float reliefVal = relief.has_value() ? (*relief)[i] : 0.0f;
            const float birthVal = birth.has_value() ? (*birth)[i] : 0.0f;
            const float orogenyTimeVal = orogenyTime.has_value() ? (*orogenyTime)[i] : 0.0f;
            const int32_t orogenyVal = orogeny.has_value() ? (*orogeny)[i] : 0;
            packed[i] = math::Float4{ reliefVal, birthVal, orogenyTimeVal, std::bit_cast<float>(orogenyVal) };
        }

        const std::span<const std::byte> bytes = std::as_bytes(std::span{ packed });

        if (buffers.AddStorage("CrustState", bytes, readable) != PlanetGpuError::Success)
        {
            return false;
        }

        return !with_initial || buffers.AddStorage(InitialName("CrustState"), bytes, false) == PlanetGpuError::Success;
    }

    bool AddPoleRatesBuffer(GpuBuffers& buffers, const ReferenceSet& reference, bool readable)
    {
        const auto poles = reference.Float3s("poles");
        const auto rates = reference.Floats("rates");

        if (!poles.has_value() || !rates.has_value() || poles->size() != rates->size())
        {
            return false;
        }

        std::vector<math::Float4> packed;
        packed.reserve(poles->size());

        for (size_t i = 0u; i < poles->size(); ++i)
        {
            packed.emplace_back((*poles)[i], (*rates)[i]);
        }

        return buffers.AddStorage("PoleRatesIn", std::as_bytes(std::span{ packed }), readable) == PlanetGpuError::Success;
    }

    bool AddOwnersBuffer(GpuBuffers& buffers,
                         const ReferenceSet& reference,
                         std::string_view best_plate_file,
                         std::string_view best_cell_file,
                         std::string_view second_plate_file,
                         std::string_view second_cell_file,
                         bool readable)
    {
        const size_t pointCount = static_cast<size_t>(reference.PointCount());
        const auto bestPlate = best_plate_file.empty() ? std::optional<std::vector<int32_t>>{} : reference.Ints(best_plate_file);
        const auto bestCell = best_cell_file.empty() ? std::optional<std::vector<int32_t>>{} : reference.Ints(best_cell_file);
        const auto secondPlate = second_plate_file.empty() ? std::optional<std::vector<int32_t>>{} : reference.Ints(second_plate_file);
        const auto secondCell = second_cell_file.empty() ? std::optional<std::vector<int32_t>>{} : reference.Ints(second_cell_file);

        if ((!best_plate_file.empty() && (!bestPlate.has_value() || bestPlate->size() != pointCount)) ||
            (!best_cell_file.empty() && (!bestCell.has_value() || bestCell->size() != pointCount)) ||
            (!second_plate_file.empty() && (!secondPlate.has_value() || secondPlate->size() != pointCount)) ||
            (!second_cell_file.empty() && (!secondCell.has_value() || secondCell->size() != pointCount)))
        {
            return false;
        }

        std::vector<math::Int4> packed(pointCount);

        for (size_t i = 0u; i < pointCount; ++i)
        {
            packed[i] = math::Int4
            {
                bestPlate.has_value() ? (*bestPlate)[i] : -1,
                bestCell.has_value() ? (*bestCell)[i] : -1,
                secondPlate.has_value() ? (*secondPlate)[i] : -1,
                secondCell.has_value() ? (*secondCell)[i] : -1,
            };
        }

        return buffers.AddStorage("Owners", std::as_bytes(std::span{ packed }), readable) == PlanetGpuError::Success;
    }

    bool AddFrontDataBuffer(GpuBuffers& buffers,
                            const ReferenceSet& reference,
                            std::string_view strength_file,
                            std::string_view speed_file,
                            std::string_view slab_age_file,
                            std::string_view subduction_file,
                            bool readable)
    {
        const size_t pointCount = static_cast<size_t>(reference.PointCount());
        const auto strength = strength_file.empty() ? std::optional<std::vector<float>>{} : reference.Floats(strength_file);
        const auto speed = speed_file.empty() ? std::optional<std::vector<float>>{} : reference.Floats(speed_file);
        const auto slabAge = slab_age_file.empty() ? std::optional<std::vector<float>>{} : reference.Floats(slab_age_file);
        const auto subduction = subduction_file.empty() ? std::optional<std::vector<uint32_t>>{} : reference.Words(subduction_file);

        if ((!strength_file.empty() && (!strength.has_value() || strength->size() != pointCount)) ||
            (!speed_file.empty() && (!speed.has_value() || speed->size() != pointCount)) ||
            (!slab_age_file.empty() && (!slabAge.has_value() || slabAge->size() != pointCount)) ||
            (!subduction_file.empty() && (!subduction.has_value() || subduction->size() != pointCount)))
        {
            return false;
        }

        std::vector<math::Float4> packed(pointCount);

        for (size_t i = 0u; i < pointCount; ++i)
        {
            const float strengthVal = strength.has_value() ? (*strength)[i] : 0.0f;
            const float speedVal = speed.has_value() ? (*speed)[i] : 0.0f;
            const float slabAgeVal = slabAge.has_value() ? (*slabAge)[i] : 0.0f;
            const uint32_t subductionVal = subduction.has_value() ? (*subduction)[i] : 0u;
            packed[i] = math::Float4{ strengthVal, speedVal, slabAgeVal, std::bit_cast<float>(subductionVal) };
        }

        return buffers.AddStorage("FrontData", std::as_bytes(std::span{ packed }), readable) == PlanetGpuError::Success;
    }

    bool AddCellFrontsBuffer(GpuBuffers& buffers,
                             const ReferenceSet& reference,
                             std::string_view front_point_file,
                             std::string_view distance_file,
                             bool readable)
    {
        const auto frontPoints = reference.Ints(front_point_file);
        const auto distances = reference.Floats(distance_file);

        if (!frontPoints.has_value() || !distances.has_value() || frontPoints->size() != distances->size())
        {
            return false;
        }

        std::vector<int32_t> packed(frontPoints->size() * 2u);

        for (size_t i = 0u; i < frontPoints->size(); ++i)
        {
            packed[i * 2u + 0u] = (*frontPoints)[i];
            packed[i * 2u + 1u] = std::bit_cast<int32_t>((*distances)[i]);
        }

        return buffers.AddStorage("CellFronts", std::as_bytes(std::span{ packed }), readable) == PlanetGpuError::Success;
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
