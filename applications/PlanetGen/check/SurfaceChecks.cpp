#include "PlanetGpuErrors.hpp"
#include "StepChecks.hpp"
#include "CheckHarness.hpp"
#include "CommandBuffer.hpp"
#include "GpuBuffers.hpp"
#include "Healpix.hpp"
#include "PlanetMath.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include "RhiHandle.hpp"
#include "ShaderLibrary.host.hpp"
#include "StepState.hpp"
#include "math/MathStorage.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <print>
#include <queue>
#include <span>
#include <string_view>
#include <vector>

namespace planet
{
    bool CheckMap(CheckContext& context)
    {
        constexpr std::array<std::string_view, 1> k_EntryPoints{ "MapProbeCS" };
        const std::optional<std::vector<math::Float3>> directions = context.Reference.Float3s("map_directions");
        const std::optional<std::vector<uint32_t>> expected = context.Reference.Words("map_nearest");

        if (!directions.has_value() || !expected.has_value())
        {
            return false;
        }

        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const int32_t pointCount = context.Reference.PointCount();
        const uint32_t rowCount = static_cast<uint32_t>(expected->size());
        const std::array<UniformValue, 2> constants
        {
            UniformUint("DirectionCount", rowCount),
            UniformUint("LatticeCount", static_cast<uint32_t>(pointCount)),
        };
        GpuBuffers buffers(context.Session.Resources());

        if (buffers.AddUniformFrom(**shaders, "MapProbeCS", "MapProbeConstants", constants) != PlanetGpuError::Success ||
            buffers.AddStorage("MapDirections", std::as_bytes(std::span{ *directions }), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("MapResults", expected->size() * sizeof(uint32_t), true) != PlanetGpuError::Success)
        {
            return false;
        }

        const std::array<DispatchStage, 1> stages
        {
            DispatchStage{ "MapProbeCS", rowCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const PlanetGpuResult<std::vector<uint32_t>> actual = buffers.ReadWords("MapResults");

        if (!actual.has_value())
        {
            return false;
        }

        const double spacing = PointSpacingRadians(pointCount);
        int64_t different = 0;
        int64_t ties = 0;

        for (int64_t row = 0; row < std::ssize(*actual); ++row)
        {
            const size_t rowIndex = static_cast<size_t>(row);

            if ((*actual)[rowIndex] == (*expected)[rowIndex])
            {
                continue;
            }

            const math::Float3& direction = (*directions)[rowIndex];
            const int32_t actualIndex = static_cast<int32_t>((*actual)[rowIndex]);
            const int32_t expectedIndex = static_cast<int32_t>((*expected)[rowIndex]);
            const double gpuDistance = Distance(LatticePointDouble(actualIndex, pointCount), direction);
            const double referenceDistance = Distance(LatticePointDouble(expectedIndex, pointCount), direction);
            ++different;

            if (std::fabs(gpuDistance - referenceDistance) <= 0.05 * spacing)
            {
                ++ties;
            }
        }

        const int64_t wrong = different - ties;
        std::println("map: {} directions, {} differ, {} near ties, {} wrong", rowCount, different, ties, wrong);
        return wrong == 0 && ties < 10;
    }

    bool CheckOwners(CheckContext& context)
    {
        constexpr std::array<std::string_view, 1> k_EntryPoints{ "OwnersCS" };
        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const std::optional<std::vector<uint8_t>> alive = context.Reference.Bytes("ref_alive");

        if (!alive.has_value())
        {
            return false;
        }

        const PlateCaps caps = ComputePlateCaps(*alive, pointCount, plateCount);
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 1> inputs
        {
            ReferenceBufferSpec{ "PreviousPlate", "previous_plate", ReferenceArrayForm::Int, false },
        };
        const std::array<ZeroBufferSpec, 1> outputs
        {
            ZeroBufferSpec{ "Owners", static_cast<size_t>(pointCount) * 4u * sizeof(int32_t), true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "OwnersCS", size) ||
            !AddRotationsBuffer(buffers, context.Reference) ||
            !AddOwnersOrderBuffer(buffers, pointCount) ||
            !AddCrustFlagsBuffer(buffers, context.Reference, storage, "ref_alive", "ref_continental", false) ||
            !AddCrustStateBuffer(buffers, context.Reference, "", "ref_birth", "", "", false) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
            buffers.AddStorage("PlateCapCenters", std::as_bytes(std::span{ caps.Centres }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("PlateCapSpread", std::as_bytes(std::span{ caps.Spreads }), false) != PlanetGpuError::Success ||
            !AddZeroedBuffers(buffers, outputs))
        {
            return false;
        }

        const std::array<DispatchStage, 1> stages
        {
            DispatchStage{ "OwnersCS", size.PointCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const PlanetGpuResult<std::vector<math::Int4>> actual = buffers.ReadInt4s("Owners");
        const std::optional<std::vector<int32_t>> bestPlate = context.Reference.Ints("owners_best_plate");
        const std::optional<std::vector<int32_t>> bestCell = context.Reference.Ints("owners_best_cell");
        const std::optional<std::vector<int32_t>> secondPlate = context.Reference.Ints("owners_second_plate");
        const std::optional<std::vector<int32_t>> secondCell = context.Reference.Ints("owners_second_cell");
        const size_t count = static_cast<size_t>(pointCount);

        if (!actual.has_value() || !bestPlate.has_value() || !bestCell.has_value() ||
            !secondPlate.has_value() || !secondCell.has_value() ||
            actual->size() != count || bestPlate->size() != count || bestCell->size() != count ||
            secondPlate->size() != count || secondCell->size() != count)
        {
            return false;
        }

        int64_t different = 0;

        for (size_t pointIndex = 0u; pointIndex < count; ++pointIndex)
        {
            const math::Int4& v = (*actual)[pointIndex];

            if (v.x != (*bestPlate)[pointIndex] ||
                v.y != (*bestCell)[pointIndex] ||
                v.z != (*secondPlate)[pointIndex] ||
                v.w != (*secondCell)[pointIndex])
            {
                ++different;
            }
        }

        const int64_t allowed = std::max<int64_t>(pointCount / 10000, 1);
        std::println("owners: {} points differ (allowed fewer than {})", different, allowed);
        return different < allowed;
    }

    bool CheckFront(CheckContext& context)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "CountVotesCS", "SwapToMajorityCS" };
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const size_t voteBytes = static_cast<size_t>(plateCount) * static_cast<size_t>(plateCount) * sizeof(uint32_t);
        const std::array<ZeroBufferSpec, 2> zeroed
        {
            ZeroBufferSpec{ "PairVotes", voteBytes, false },
            ZeroBufferSpec{ "LowVotes", voteBytes, false },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "CountVotesCS", size) ||
            !AddCrustFlagsBuffer(buffers, context.Reference, storage, "ref_alive", "ref_continental", false) ||
            !AddOwnersBuffer(buffers, context.Reference, "owners_best_plate", "owners_best_cell", "owners_second_plate", "owners_second_cell", true) ||
            !AddZeroedBuffers(buffers, zeroed))
        {
            return false;
        }

        const std::array<DispatchStage, 2> stages
        {
            DispatchStage{ "CountVotesCS", size.PointCount },
            DispatchStage{ "SwapToMajorityCS", size.PointCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const PlanetGpuResult<std::vector<math::Int4>> actual = buffers.ReadInt4s("Owners");
        const std::optional<std::vector<int32_t>> bestPlate = context.Reference.Ints("front_best_plate");
        const std::optional<std::vector<int32_t>> bestCell = context.Reference.Ints("front_best_cell");
        const std::optional<std::vector<int32_t>> secondPlate = context.Reference.Ints("front_second_plate");
        const std::optional<std::vector<int32_t>> secondCell = context.Reference.Ints("front_second_cell");
        const size_t count = static_cast<size_t>(pointCount);

        if (!actual.has_value() || !bestPlate.has_value() || !bestCell.has_value() ||
            !secondPlate.has_value() || !secondCell.has_value() ||
            actual->size() != count || bestPlate->size() != count || bestCell->size() != count ||
            secondPlate->size() != count || secondCell->size() != count)
        {
            return false;
        }

        int64_t different = 0;

        for (size_t pointIndex = 0u; pointIndex < count; ++pointIndex)
        {
            const math::Int4& v = (*actual)[pointIndex];
            different += v.x != (*bestPlate)[pointIndex] ? 1 : 0;
            different += v.y != (*bestCell)[pointIndex] ? 1 : 0;
            different += v.z != (*secondPlate)[pointIndex] ? 1 : 0;
            different += v.w != (*secondCell)[pointIndex] ? 1 : 0;
        }

        std::println("front: {} values differ", different);
        return different == 0;
    }

    bool CheckCrust(CheckContext& context)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "FillGapsCS", "RemoveSubductedCS" };
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 1> inputs
        {
            ReferenceBufferSpec{ "PreviousPlate", "previous_plate", ReferenceArrayForm::Int, false },
        };
        const std::array<ZeroBufferSpec, 1> zeroed
        {
            ZeroBufferSpec{ "FrontData", static_cast<size_t>(pointCount) * 4u * sizeof(float), true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "FillGapsCS", size) ||
            !AddTimeUniform(buffers, **shaders, "FillGapsCS", "Pass3Constants", context.Reference.Time()) ||
            !AddRotationsBuffer(buffers, context.Reference) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
            !AddPoleRatesBuffer(buffers, context.Reference) ||
            !AddOwnersBuffer(buffers, context.Reference, "front_best_plate", "front_best_cell", "front_second_plate", "front_second_cell", true) ||
            !AddCrustFlagsBuffer(buffers, context.Reference, storage, "ref_alive", "ref_continental", true) ||
            !AddCrustStateBuffer(buffers, context.Reference, "ref_relief", "ref_birth", "ref_orogeny_time", "ref_orogeny", true) ||
            !AddZeroedBuffers(buffers, zeroed))
        {
            return false;
        }

        const std::array<DispatchStage, 2> stages
        {
            DispatchStage{ "FillGapsCS", size.PointCount },
            DispatchStage{ "RemoveSubductedCS", size.PointCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        bool passed = true;
        passed = CompareOwnersXY("crust", buffers, context.Reference, "p3_plate", "p3_cell", 1) && passed;
        passed = CompareCrustFlags("crust", buffers, context.Reference, storage, "p3_ref_alive", "p3_ref_continental", {}) && passed;
        passed = CompareCrustState("crust", buffers, context.Reference, "p3_ref_relief", "p3_ref_birth", "p3_ref_orogeny_time", "p3_ref_orogeny", {}, 0.0f, 0.0f) && passed;
        passed = CompareFrontData("crust", buffers, context.Reference, true) && passed;
        return passed;
    }

    namespace
    {
        [[nodiscard]] bool VerifyHostHealpix()
        {
            int64_t bijectionErrors = 0;
            int64_t neighborRangeErrors = 0;
            int64_t neighborSymmetryErrors = 0;
            int64_t cornerCountErrors = 0;

            for (uint32_t order = 1u; order <= 5u; ++order)
            {
                const uint32_t pixelCount = static_cast<uint32_t>(HealpixPixelCount(order));
                int64_t missingCorners = 0;

                for (uint32_t pixel = 0u; pixel < pixelCount; ++pixel)
                {
                    const std::array<double, 3> vec = HealpixPixToVec(pixel, order);

                    if (!std::isfinite(vec[0]) || !std::isfinite(vec[1]) || !std::isfinite(vec[2]) ||
                        HealpixNestedIndex(vec[0], vec[1], vec[2], order) != pixel)
                    {
                        ++bijectionErrors;
                    }

                    const std::array<int32_t, 8> neighbors = HealpixNeighbors8(pixel, order);

                    for (const int32_t neighbor : neighbors)
                    {
                        if (neighbor == -1)
                        {
                            ++missingCorners;
                            continue;
                        }

                        if (neighbor < 0 || static_cast<uint32_t>(neighbor) >= pixelCount ||
                            static_cast<uint32_t>(neighbor) == pixel)
                        {
                            ++neighborRangeErrors;
                            continue;
                        }

                        const std::array<int32_t, 8> reverse =
                            HealpixNeighbors8(static_cast<uint32_t>(neighbor), order);

                        if (std::ranges::find(reverse, static_cast<int32_t>(pixel)) == reverse.end())
                        {
                            ++neighborSymmetryErrors;
                        }
                    }
                }

                if (missingCorners != 24)
                {
                    ++cornerCountErrors;
                }
            }

            std::println("healpix host (orders 1..5): {} bijection errors, {} range errors, {} symmetry errors, {} corner count errors",
                         bijectionErrors, neighborRangeErrors, neighborSymmetryErrors, cornerCountErrors);
            return bijectionErrors == 0 && neighborRangeErrors == 0 &&
                   neighborSymmetryErrors == 0 && cornerCountErrors == 0;
        }
    }

    bool RunHealpixCheck(CheckContext& context)
    {
        const bool hostPassed = VerifyHostHealpix();

        if (!hostPassed)
        {
            return false;
        }

        constexpr std::array<std::string_view, 1> k_EntryPoints{ "HealpixProbeCS" };
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        constexpr uint32_t k_Order = 6u;
        constexpr uint32_t k_PixelCount = 49152u;
        const std::array<UniformValue, 2> constants
        {
            UniformUint("Order", k_Order),
            UniformUint("PixelCount", k_PixelCount),
        };
        GpuBuffers buffers(context.Session.Resources());

        if (buffers.AddUniformFrom(**shaders, "HealpixProbeCS", "HealpixProbeConstants", constants) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("HealpixProbeVecs", static_cast<size_t>(k_PixelCount) * 16u, true) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("HealpixProbeNeighbors", static_cast<size_t>(k_PixelCount) * 8u * sizeof(int32_t), true) != PlanetGpuError::Success)
        {
            return false;
        }

        const std::array<DispatchStage, 1> stages
        {
            DispatchStage{ "HealpixProbeCS", k_PixelCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const PlanetGpuResult<std::vector<math::Float4>> actualVecs = buffers.ReadFloat4s("HealpixProbeVecs");
        const PlanetGpuResult<std::vector<int32_t>> actualNeighbors = buffers.ReadInts("HealpixProbeNeighbors");

        if (!actualVecs.has_value() || !actualNeighbors.has_value() ||
            actualVecs->size() != k_PixelCount ||
            actualNeighbors->size() != static_cast<size_t>(k_PixelCount) * 8u)
        {
            return false;
        }

        constexpr double k_VecTolerance = 1.0e-5;
        int64_t vecErrors = 0;
        int64_t roundTripErrors = 0;
        int64_t neighborErrors = 0;
        double maxVecDiff = 0.0;

        for (uint32_t pixelIndex = 0u; pixelIndex < k_PixelCount; ++pixelIndex)
        {
            const math::Float4& gpuVec = (*actualVecs)[pixelIndex];
            const std::array<double, 3> expectedVec = HealpixPixToVec(pixelIndex, k_Order);
            const double dx = std::fabs(static_cast<double>(gpuVec.x) - expectedVec[0]);
            const double dy = std::fabs(static_cast<double>(gpuVec.y) - expectedVec[1]);
            const double dz = std::fabs(static_cast<double>(gpuVec.z) - expectedVec[2]);
            const double diff = std::max({ dx, dy, dz });

            if (!(diff <= k_VecTolerance))
            {
                ++vecErrors;
            }

            if (std::isfinite(diff))
            {
                maxVecDiff = std::max(maxVecDiff, diff);
            }

            if (std::bit_cast<uint32_t>(gpuVec.w) != pixelIndex)
            {
                ++roundTripErrors;
            }

            const std::array<int32_t, 8> expectedNeighbors = HealpixNeighbors8(pixelIndex, k_Order);
            const size_t base = static_cast<size_t>(pixelIndex) * 8u;

            for (size_t dir = 0u; dir < 8u; ++dir)
            {
                if ((*actualNeighbors)[base + dir] != expectedNeighbors[dir])
                {
                    ++neighborErrors;
                }
            }
        }

        std::println("healpix gpu (order {}): {} vec errors (max diff {:.3g}), {} round-trip errors, {} neighbor errors",
                     k_Order, vecErrors, maxVecDiff, roundTripErrors, neighborErrors);
        return vecErrors == 0 && roundTripErrors == 0 && neighborErrors == 0;
    }

    namespace
    {
        constexpr std::array<std::string_view, 15> k_CollideEntryPoints
        {
            "DetectCollisionsCS", "SelectCollisionPairCS", "BkeTileInitAndLocalUnionCS", "BkeStitchSeamsCS", "BkeFlattenCS",
            "MarkTerraneSeedsCS", "CountTerraneCS", "DecideTransferCS", "TransferTerraneDirectCS", "ClearSourceTerraneCS",
            "ComputeSutureCapSpreadCS", "NearestSuturePointCS", "EscapeHistogramCS", "EscapePrefixSumCS", "ApplyEscapeAndSurgeCS",
        };

        constexpr std::array<StepBufferSpec, 13> k_CollideZeroedBuffers
        {
            StepBufferSpec{ "CollisionPairCount", BufferExtent::PlatesSquared },
            StepBufferSpec{ "CollisionPairSpeedSum", BufferExtent::PlatesSquared },
            StepBufferSpec{ "CollisionPairDeepCount", BufferExtent::PlatesSquared },
            StepBufferSpec{ "CollisionJob", BufferExtent::CollideJob },
            StepBufferSpec{ "CollideDispatch", BufferExtent::CollideDispatch },
            StepBufferSpec{ "BkeParent", BufferExtent::HalfPoints },
            StepBufferSpec{ "BkeMask", BufferExtent::HalfPoints },
            StepBufferSpec{ "BkeSeedFlag", BufferExtent::HalfPoints },
            StepBufferSpec{ "CollideScratchCounters", BufferExtent::CollideCounters },
            StepBufferSpec{ "SuturePositionSum", BufferExtent::SignedVec3 },
            StepBufferSpec{ "SuturePoints", BufferExtent::PointsTimes4 },
            StepBufferSpec{ "SutureDistance", BufferExtent::Points },
            StepBufferSpec{ "EscapeHistogram", BufferExtent::EscapeHistogram },
        };

        [[nodiscard]] bool AddCollideZeroedBuffers(GpuBuffers& buffers, StepSize size)
        {
            for (const StepBufferSpec& spec : k_CollideZeroedBuffers)
            {
                const size_t byteSize = StepBufferWordCount(spec.Extent, size) * sizeof(uint32_t);

                if (buffers.AddZeroedStorage(spec.Name, byteSize, true) != PlanetGpuError::Success)
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool AddCollideCrustFlags(GpuBuffers& buffers,
                                                CrustFlagStorage storage,
                                                std::span<const uint8_t> packed_flags,
                                                bool readable)
        {
            const std::string_view name = CrustFlagBufferNames(storage);

            if (storage == CrustFlagStorage::Bytes)
            {
                return buffers.AddStorage(name, std::as_bytes(packed_flags), readable) == PlanetGpuError::Success;
            }

            std::vector<uint32_t> words(packed_flags.size());

            for (size_t i = 0u; i < packed_flags.size(); ++i)
            {
                words[i] = static_cast<uint32_t>(packed_flags[i]);
            }

            return buffers.AddStorage(name, std::as_bytes(std::span{ words }), readable) == PlanetGpuError::Success;
        }

        [[nodiscard]] std::optional<std::vector<uint8_t>> ReadCollideCrustFlags(const GpuBuffers& buffers, CrustFlagStorage storage)
        {
            const std::string_view name = CrustFlagBufferNames(storage);

            if (storage == CrustFlagStorage::Bytes)
            {
                const PlanetGpuResult<std::vector<std::byte>> bytes = buffers.Read(name);

                if (!bytes.has_value())
                {
                    return std::nullopt;
                }

                std::vector<uint8_t> flags(bytes->size());

                for (size_t i = 0u; i < bytes->size(); ++i)
                {
                    flags[i] = std::to_integer<uint8_t>((*bytes)[i]);
                }

                return flags;
            }

            const PlanetGpuResult<std::vector<uint32_t>> words = buffers.ReadWords(name);

            if (!words.has_value())
            {
                return std::nullopt;
            }

            std::vector<uint8_t> flags(words->size());

            for (size_t i = 0u; i < words->size(); ++i)
            {
                flags[i] = static_cast<uint8_t>((*words)[i] & 0xFFu);
            }

            return flags;
        }

        [[nodiscard]] bool AddCollideKinematics(GpuBuffers& buffers, uint32_t plate_count)
        {
            std::vector<math::Float4> rotations;
            rotations.reserve(static_cast<size_t>(plate_count) * 3u);

            for (uint32_t plate = 0u; plate < plate_count; ++plate)
            {
                rotations.push_back(math::Float4{ 1.0f, 0.0f, 0.0f, 0.0f });
                rotations.push_back(math::Float4{ 0.0f, 1.0f, 0.0f, 0.0f });
                rotations.push_back(math::Float4{ 0.0f, 0.0f, 1.0f, 0.0f });
            }

            const std::array<math::Float4, 4> poleRates
            {
                math::Float4{ 0.0f, 1.0f, 0.0f, 0.008f },
                math::Float4{ 0.0f, -1.0f, 0.0f, 0.008f },
                math::Float4{ 0.0f, 0.0f, 1.0f, 0.001f },
                math::Float4{ 0.0f, 0.0f, 1.0f, 0.001f },
            };

            return buffers.AddStorage("Rotations", std::as_bytes(std::span{ rotations }), false) == PlanetGpuError::Success &&
                   buffers.AddStorage("PoleRatesIn", std::as_bytes(std::span{ poleRates }), false) == PlanetGpuError::Success;
        }

        [[nodiscard]] std::vector<uint8_t> ComputeBfsTerrane(std::span<const uint8_t> continental,
                                                             std::span<const uint32_t> seeds,
                                                             uint32_t point_count,
                                                             uint32_t order,
                                                             uint32_t& out_count)
        {
            std::vector<uint8_t> inTerrane(point_count, 0u);
            std::queue<uint32_t> frontier;
            out_count = 0u;

            for (const uint32_t seed : seeds)
            {
                if (continental[seed] != 0u && inTerrane[seed] == 0u)
                {
                    inTerrane[seed] = 1u;
                    ++out_count;
                    frontier.push(seed);
                }
            }

            while (!frontier.empty())
            {
                const uint32_t current = frontier.front();
                frontier.pop();
                const std::array<int32_t, 8> neighbors = HealpixNeighbors8(current, order);

                for (const int32_t neighbor : neighbors)
                {
                    if (neighbor < 0)
                    {
                        continue;
                    }

                    const uint32_t next = static_cast<uint32_t>(neighbor);

                    if (continental[next] != 0u && inTerrane[next] == 0u)
                    {
                        inTerrane[next] = 1u;
                        ++out_count;
                        frontier.push(next);
                    }
                }
            }

            return inTerrane;
        }

        [[nodiscard]] bool RunCollideSubtestA(CheckContext& context)
        {
            constexpr uint32_t k_Order = 5u;
            constexpr uint32_t k_PointCount = 12288u;
            constexpr uint32_t k_PlateCount = 4u;
            constexpr float k_TimeMy = 100.0f;
            const StepSize size{ k_PointCount, k_PlateCount };

            const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
                context.Session.CreateShaders(context.CrustFlags, k_CollideEntryPoints);

            if (!shaders.has_value())
            {
                return false;
            }

            const CrustFlagStorage storage = (*shaders)->CrustFlags();
            const size_t totalCells = static_cast<size_t>(k_PlateCount) * static_cast<size_t>(k_PointCount);
            std::vector<uint8_t> packedFlags(totalCells, 0u);
            std::vector<math::Float4> crustState(totalCells, math::Float4{ 0.0f, 0.0f, 0.0f, 0.0f });
            std::vector<uint8_t> plate0Cont0A(k_PointCount, 0u);
            std::vector<uint8_t> plate0Island0B(k_PointCount, 0u);
            std::vector<uint8_t> plate0Cont(k_PointCount, 0u);
            std::vector<uint8_t> plate1Cont(k_PointCount, 0u);
            std::vector<math::Int4> owners(k_PointCount);
            std::vector<uint32_t> overlapSeeds;
            uint32_t initialCont1 = 0u;

            for (uint32_t p = 0u; p < k_PointCount; ++p)
            {
                const std::array<double, 3> vec = HealpixPixToVec(p, k_Order);
                const bool is0A = vec[2] > 0.85;
                const bool is0B = vec[2] < -0.94;
                const bool is0 = is0A || is0B;
                const bool is1 = (vec[2] > 0.72) && (vec[0] > -0.2);

                plate0Cont0A[p] = is0A ? 1u : 0u;
                plate0Island0B[p] = is0B ? 1u : 0u;
                plate0Cont[p] = is0 ? 1u : 0u;
                plate1Cont[p] = is1 ? 1u : 0u;

                if (is0)
                {
                    packedFlags[p] = 3u;
                    crustState[p] = math::Float4{ 0.5f, 0.0f, 0.0f, 0.0f };
                }

                if (is1)
                {
                    const size_t flat1 = static_cast<size_t>(k_PointCount) + p;
                    packedFlags[flat1] = 3u;
                    crustState[flat1] = math::Float4{ 0.5f, 0.0f, 0.0f, 0.0f };
                    ++initialCont1;
                }

                if (is0 && is1)
                {
                    owners[p] = math::Int4{ 0, static_cast<int32_t>(p), 1, static_cast<int32_t>(p) };
                    overlapSeeds.push_back(p);
                }
                else if (is0)
                {
                    owners[p] = math::Int4{ 0, static_cast<int32_t>(p), -1, -1 };
                }
                else if (is1)
                {
                    owners[p] = math::Int4{ 1, static_cast<int32_t>(p), -1, -1 };
                }
                else
                {
                    owners[p] = math::Int4{ 2, static_cast<int32_t>(p), -1, -1 };
                }
            }

            GpuBuffers buffers(context.Session.Resources());

            if (!AddParamsUniform(buffers, **shaders, "DetectCollisionsCS", size, 2.0f, k_Order) ||
                !AddTimeUniform(buffers, **shaders, "ApplyEscapeAndSurgeCS", "Pass3Constants", k_TimeMy) ||
                !AddCollideKinematics(buffers, k_PlateCount) ||
                !AddCollideCrustFlags(buffers, storage, packedFlags, true) ||
                buffers.AddStorage("CrustState", std::as_bytes(std::span{ crustState }), true) != PlanetGpuError::Success ||
                buffers.AddStorage("Owners", std::as_bytes(std::span{ owners }), false) != PlanetGpuError::Success ||
                !AddCollideZeroedBuffers(buffers, size))
            {
                return false;
            }

            const std::optional<rhi::BufferHandle> collideDispatch = buffers.Handle("CollideDispatch");
            const PlanetGpuResult<std::unique_ptr<PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

            if (!collideDispatch.has_value() || !bindings.has_value())
            {
                return false;
            }

            rhi::CommandBuffer commandBuffer = context.Session.BeginCommands();
            (*shaders)->RecordDispatch(commandBuffer, **bindings, "DetectCollisionsCS", k_PointCount);
            RecordComputeBarrier(commandBuffer);
            (*shaders)->RecordDispatch(commandBuffer, **bindings, "SelectCollisionPairCS", 1u);
            RecordIndirectBarrier(commandBuffer);

            struct IndirectStage
            {
                std::string_view EntryPoint;
                uint64_t Offset;
            };

            constexpr std::array<IndirectStage, 13> k_IndirectStages
            {
                IndirectStage{ "BkeTileInitAndLocalUnionCS", 0u },
                IndirectStage{ "BkeStitchSeamsCS", 0u },
                IndirectStage{ "BkeFlattenCS", 12u },
                IndirectStage{ "MarkTerraneSeedsCS", 24u },
                IndirectStage{ "CountTerraneCS", 12u },
                IndirectStage{ "DecideTransferCS", 36u },
                IndirectStage{ "TransferTerraneDirectCS", 24u },
                IndirectStage{ "ClearSourceTerraneCS", 24u },
                IndirectStage{ "ComputeSutureCapSpreadCS", 36u },
                IndirectStage{ "NearestSuturePointCS", 24u },
                IndirectStage{ "EscapeHistogramCS", 24u },
                IndirectStage{ "EscapePrefixSumCS", 36u },
                IndirectStage{ "ApplyEscapeAndSurgeCS", 24u },
            };

            for (size_t i = 0u; i < k_IndirectStages.size(); ++i)
            {
                if (i > 0u)
                {
                    RecordComputeBarrier(commandBuffer);
                }

                (*shaders)->RecordDispatchIndirect(commandBuffer, **bindings, k_IndirectStages[i].EntryPoint,
                                                   *collideDispatch, k_IndirectStages[i].Offset);
            }

            buffers.RecordReadbackCopies(commandBuffer);

            if (!context.Session.SubmitAndWait(commandBuffer))
            {
                return false;
            }

            const PlanetGpuResult<std::vector<uint32_t>> collisionJob = buffers.ReadWords("CollisionJob");
            const PlanetGpuResult<std::vector<uint32_t>> counters = buffers.ReadWords("CollideScratchCounters");
            const PlanetGpuResult<std::vector<uint32_t>> bkeParent = buffers.ReadWords("BkeParent");
            const PlanetGpuResult<std::vector<uint32_t>> bkeMask = buffers.ReadWords("BkeMask");
            const PlanetGpuResult<std::vector<uint32_t>> bkeSeedFlag = buffers.ReadWords("BkeSeedFlag");
            const std::optional<std::vector<uint8_t>> finalFlags = ReadCollideCrustFlags(buffers, storage);
            const PlanetGpuResult<std::vector<math::Float4>> finalState = buffers.ReadFloat4s("CrustState");

            if (!collisionJob.has_value() || collisionJob->size() < 5u ||
                !counters.has_value() || counters->size() < 4u ||
                !bkeParent.has_value() || !bkeMask.has_value() || !bkeSeedFlag.has_value() ||
                !finalFlags.has_value() || finalFlags->size() != totalCells ||
                !finalState.has_value() || finalState->size() != totalCells)
            {
                return false;
            }

            uint32_t cpuTerraneCount0 = 0u;
            uint32_t cpuTerraneCount1 = 0u;
            const std::vector<uint8_t> cpuTerrane0 =
                ComputeBfsTerrane(plate0Cont, overlapSeeds, k_PointCount, k_Order, cpuTerraneCount0);
            const std::vector<uint8_t> cpuTerrane1 =
                ComputeBfsTerrane(plate1Cont, overlapSeeds, k_PointCount, k_Order, cpuTerraneCount1);

            const uint32_t blocksPerPlate = k_PointCount >> 2u;
            int64_t bkeMismatches = 0;

            for (uint32_t slot = 0u; slot < 2u; ++slot)
            {
                const std::vector<uint8_t>& cpuTerrane = slot == 0u ? cpuTerrane0 : cpuTerrane1;

                for (uint32_t p = 0u; p < k_PointCount; ++p)
                {
                    const uint32_t block = slot * blocksPerPlate + (p >> 2u);
                    const uint32_t mask = (*bkeMask)[block];
                    const uint32_t parent = (*bkeParent)[block];
                    const bool gpuInTerrane = ((mask & (1u << (p & 3u))) != 0u) &&
                                              (parent < bkeSeedFlag->size()) &&
                                              ((*bkeSeedFlag)[parent] != 0u);

                    if (gpuInTerrane != (cpuTerrane[p] != 0u))
                    {
                        ++bkeMismatches;
                    }
                }
            }

            int64_t continent0ARemaining = 0;
            int64_t island0BLost = 0;
            uint32_t finalCont1 = 0u;
            int64_t surgeCells = 0;

            for (uint32_t p = 0u; p < k_PointCount; ++p)
            {
                const uint8_t flags0 = (*finalFlags)[p];

                if (plate0Cont0A[p] != 0u && (flags0 & 1u) != 0u)
                {
                    ++continent0ARemaining;
                }

                if (plate0Island0B[p] != 0u && (flags0 & 3u) != 3u)
                {
                    ++island0BLost;
                }

                const size_t flat1 = static_cast<size_t>(k_PointCount) + p;
                const uint8_t flags1 = (*finalFlags)[flat1];

                if ((flags1 & 3u) == 3u)
                {
                    ++finalCont1;
                    const math::Float4& state = (*finalState)[flat1];

                    if (state.x > 0.5f && std::bit_cast<int32_t>(state.w) == 2 && state.z == k_TimeMy)
                    {
                        ++surgeCells;
                    }
                }
            }

            const bool pairOk = (*collisionJob)[0] == 0u && (*collisionJob)[1] == 1u;
            const bool countsOk = (*counters)[0] == cpuTerraneCount0 && (*counters)[1] == cpuTerraneCount1;
            const bool bkeOk = bkeMismatches == 0;
            const bool sourceTargetOk = (*collisionJob)[3] == 0u && (*collisionJob)[4] == 1u;
            const bool clearedOk = continent0ARemaining == 0 && island0BLost == 0;
            const bool sutureOk = (*counters)[2] > 0u && (*counters)[3] > 0u;
            const uint32_t gained1 = finalCont1 >= initialCont1 ? (finalCont1 - initialCont1) : 0u;
            const bool growthOk = finalCont1 >= initialCont1 && (gained1 * 100u) >= (85u * cpuTerraneCount0);
            const bool surgeOk = surgeCells >= 10;

            std::println("collide A (order {}): pair=({},{}) src->dst={}->{} terrane=({}/{} vs cpu {}/{}) bke_err={} 0A_rem={} 0B_lost={} suture={} recv={} plate1_gain={}/{} surge_cells={}",
                         k_Order, (*collisionJob)[0], (*collisionJob)[1], (*collisionJob)[3], (*collisionJob)[4],
                         (*counters)[0], (*counters)[1], cpuTerraneCount0, cpuTerraneCount1, bkeMismatches,
                         continent0ARemaining, island0BLost, (*counters)[2], (*counters)[3],
                         gained1, cpuTerraneCount0, surgeCells);

            return pairOk && countsOk && bkeOk && sourceTargetOk && clearedOk && sutureOk && growthOk && surgeOk;
        }

        [[nodiscard]] bool RunCollideSubtestB(CheckContext& context)
        {
            constexpr uint32_t k_Order = 5u;
            constexpr uint32_t k_PointCount = 12288u;
            constexpr uint32_t k_PlateCount = 4u;
            constexpr std::array<std::string_view, 2> k_GateEntryPoints{ "DetectCollisionsCS", "SelectCollisionPairCS" };
            const StepSize size{ k_PointCount, k_PlateCount };

            const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
                context.Session.CreateShaders(context.CrustFlags, k_GateEntryPoints);

            if (!shaders.has_value())
            {
                return false;
            }

            const CrustFlagStorage storage = (*shaders)->CrustFlags();
            const size_t totalCells = static_cast<size_t>(k_PlateCount) * static_cast<size_t>(k_PointCount);
            const std::vector<uint8_t> packedFlags(totalCells, 0u);
            std::vector<math::Int4> owners(k_PointCount);

            for (uint32_t p = 0u; p < k_PointCount; ++p)
            {
                owners[p] = math::Int4{ 0, static_cast<int32_t>(p), -1, -1 };
            }

            GpuBuffers buffers(context.Session.Resources());

            if (!AddParamsUniform(buffers, **shaders, "DetectCollisionsCS", size, 2.0f, k_Order) ||
                !AddCollideKinematics(buffers, k_PlateCount) ||
                !AddCollideCrustFlags(buffers, storage, packedFlags, false) ||
                buffers.AddStorage("Owners", std::as_bytes(std::span{ owners }), false) != PlanetGpuError::Success ||
                !AddCollideZeroedBuffers(buffers, size))
            {
                return false;
            }

            const std::array<DispatchStage, 2> stages
            {
                DispatchStage{ "DetectCollisionsCS", k_PointCount },
                DispatchStage{ "SelectCollisionPairCS", 1u },
            };

            if (!RunDispatches(context.Session, **shaders, buffers, stages))
            {
                return false;
            }

            const PlanetGpuResult<std::vector<uint32_t>> collisionJob = buffers.ReadWords("CollisionJob");
            const PlanetGpuResult<std::vector<uint32_t>> collideDispatch = buffers.ReadWords("CollideDispatch");

            if (!collisionJob.has_value() || collisionJob->empty() ||
                !collideDispatch.has_value() || collideDispatch->size() < 12u)
            {
                return false;
            }

            const bool passed = (*collisionJob)[0] == 0xFFFFFFFFu &&
                                (*collideDispatch)[0] == 0u &&
                                (*collideDispatch)[3] == 0u &&
                                (*collideDispatch)[6] == 0u &&
                                (*collideDispatch)[9] == 0u;

            std::println("collide B (no overlap): job[0]={:#010x} dispatch=({}, {}, {}, {})",
                         (*collisionJob)[0], (*collideDispatch)[0], (*collideDispatch)[3],
                         (*collideDispatch)[6], (*collideDispatch)[9]);
            return passed;
        }
    }

    bool RunCollideCheck(CheckContext& context)
    {
        return RunCollideSubtestA(context) && RunCollideSubtestB(context);
    }

    bool RunPlumesCheck(CheckContext& context)
    {
        constexpr uint32_t k_Order = 5u;
        constexpr uint32_t k_PointCount = 12288u;
        constexpr uint32_t k_PlateCount = 1u;
        constexpr float k_Dt = 1.0f;
        constexpr uint32_t k_Steps = 45u;
        constexpr float k_RadToDeg = 57.2957795f;
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "ApplyMantlePlumesCS", "MovePlatesAndClearCountersCS" };

        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const size_t flagBytes = storage == CrustFlagStorage::Bytes ? k_PointCount : (k_PointCount * sizeof(uint32_t));
        std::vector<uint8_t> packedFlags(flagBytes, 0u);

        if (storage == CrustFlagStorage::Bytes)
        {
            std::ranges::fill(packedFlags, uint8_t{ 1u });
        }
        else
        {
            for (uint32_t i = 0u; i < k_PointCount; ++i)
            {
                packedFlags[static_cast<size_t>(i) * 4u] = 1u;
            }
        }

        std::vector<uint32_t> liveCells(k_PointCount);
        for (uint32_t i = 0u; i < k_PointCount; ++i)
        {
            liveCells[i] = i;
        }

        const std::array<uint32_t, 2> liveOffset{ 0u, k_PointCount };
        const std::array<math::Float4, 3> rotations
        {
            math::Float4{ 1.0f, 0.0f, 0.0f, 0.0f },
            math::Float4{ 0.0f, 1.0f, 0.0f, 0.0f },
            math::Float4{ 0.0f, 0.0f, 1.0f, 0.0f },
        };
        const std::array<math::Float4, 1> poleRates{ math::Float4{ 0.0f, 0.0f, 1.0f, 80.0f / 6370.0f } };
        const std::array<math::Float4, 1> plateRift{ math::Float4{ 0.0f, 0.0f, 1.0f, 0.0f } };
        const std::array<math::Int4, 1> plateCounters{ math::Int4{ 0, 0, 0, static_cast<int32_t>(k_PointCount) } };
        const std::array<UniformValue, 4> paramsValues
        {
            UniformUint("PointCount", k_PointCount),
            UniformUint("PlateCount", k_PlateCount),
            UniformFloat("TimeStepMy", k_Dt),
            UniformUint("HealpixOrder", k_Order),
        };
        const std::array<UniformValue, 1> movePlatesValues{ UniformFloat("Time", 25.0f) };

        using PlumeParamsHost = planet_gen_shaders::PlumeParams<
            planet_gen_shaders::CookProfile::Spirv,
            ::lodestone::LayoutRule::Std140>;
        PlumeParamsHost plumes{};
        plumes.PlumeCount = 1u;
        plumes.Time = 25.0f;
        plumes.GlobalStrength = 1.0f;
        plumes.OceanicErosionRate = 0.015f;
        plumes.SwellScale = 1.0f;
        plumes.AccretionThresholdKm = 0.0f;
        plumes.Flags = 1u;
        plumes.Plumes[0].PositionAndRadius = math::Float4{ 1.0f, 0.0f, 0.0f, 115.0f };
        plumes.Plumes[0].DriftAndSpeed = math::Float4{ 0.0f, 0.0f, 1.0f, 0.0f };
        plumes.Plumes[0].SwellAndFlux = math::Float4{ 400.0f, 1.2f, 5.0f, 2.5f };
        plumes.Plumes[0].TimingAndPulse = math::Float4{ 0.0f, 0.0f, 15.0f, 0.35f };

        GpuBuffers buffers(context.Session.Resources());
        const std::string_view flagBuffer = CrustFlagBufferNames(storage);

        if (buffers.AddUniformFrom(**shaders, "ApplyMantlePlumesCS", "StepParams", paramsValues) != PlanetGpuError::Success ||
            buffers.AddUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", movePlatesValues) != PlanetGpuError::Success ||
            buffers.AddUniform("PlumeConstants", std::as_bytes(std::span{ &plumes, 1u })) != PlanetGpuError::Success ||
            buffers.AddStorage(flagBuffer, std::as_bytes(std::span{ packedFlags }), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("CrustState", static_cast<size_t>(k_PointCount) * sizeof(math::Float4), true) != PlanetGpuError::Success ||
            buffers.AddStorage("LiveCell", std::as_bytes(std::span{ liveCells }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("PlateLiveOffset", std::as_bytes(std::span{ liveOffset }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("Rotations", std::as_bytes(std::span{ rotations }), true) != PlanetGpuError::Success ||
            buffers.AddStorage("PoleRatesIn", std::as_bytes(std::span{ poleRates }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("PlateRiftIn", std::as_bytes(std::span{ plateRift }), false) != PlanetGpuError::Success ||
            buffers.AddStorage("PlateCounters", std::as_bytes(std::span{ plateCounters }), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("PlateContinentalCount", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("PlatePull", 3u * sizeof(int32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("PlateLiveCursor", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("PlateFrontPointCount", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("PlateFrontPointCursor", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("PairVotes", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("LowVotes", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("CollisionPairCount", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("CollisionPairSpeedSum", sizeof(uint32_t), false) != PlanetGpuError::Success ||
            buffers.AddZeroedStorage("CollisionPairDeepCount", sizeof(uint32_t), false) != PlanetGpuError::Success)
        {
            return false;
        }

        const PlanetGpuResult<std::unique_ptr<PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());
        if (!bindings.has_value())
        {
            return false;
        }

        for (uint32_t step = 0u; step < k_Steps; ++step)
        {
            const float time = 25.0f + static_cast<float>(step + 1u) * k_Dt;
            plumes.Time = time;
            const std::array<UniformValue, 1> timeVal{ UniformFloat("Time", time) };

            if (buffers.UpdateUniform("PlumeConstants", std::as_bytes(std::span{ &plumes, 1u })) != PlanetGpuError::Success ||
                buffers.UpdateUniformFrom(**shaders, "MovePlatesAndClearCountersCS", "MovePlatesConstants", timeVal) != PlanetGpuError::Success)
            {
                return false;
            }

            rhi::CommandBuffer cmd = context.Session.BeginCommands();
            (*shaders)->RecordDispatch(cmd, **bindings, "ApplyMantlePlumesCS", k_PointCount);
            RecordComputeBarrier(cmd);
            (*shaders)->RecordDispatch(cmd, **bindings, "MovePlatesAndClearCountersCS", 1u);

            if (step + 1u == k_Steps)
            {
                buffers.RecordReadbackCopies(cmd);
            }

            if (!context.Session.SubmitAndWait(cmd))
            {
                return false;
            }
        }

        const PlanetGpuResult<std::vector<math::Float4>> statesAfterChain = buffers.ReadFloat4s("CrustState");
        const PlanetGpuResult<std::vector<math::Float4>> finalRot = buffers.ReadFloat4s("Rotations");

        if (!statesAfterChain.has_value() || !finalRot.has_value() ||
            statesAfterChain->size() != k_PointCount || finalRot->size() < 3u)
        {
            return false;
        }

        const math::Float4& r0 = (*finalRot)[0];
        const math::Float4& r1 = (*finalRot)[1];
        const math::Float4& r2 = (*finalRot)[2];

        float hotspotMaxRelief = 0.0f;
        float hotspotMaxTime = 0.0f;
        int32_t upstreamVolcanic = 0;
        std::array<int32_t, 3> binCount{ 0, 0, 0 };
        std::array<double, 3> binTimeSum{ 0.0, 0.0, 0.0 };

        for (uint32_t cell = 0u; cell < k_PointCount; ++cell)
        {
            const math::Float4& cs = (*statesAfterChain)[cell];
            const int32_t orogenyType = std::bit_cast<int32_t>(cs.w);
            if (orogenyType != 4)
            {
                continue;
            }

            const std::array<double, 3> lp = HealpixPixToVec(cell, k_Order);
            const double wx = r0.x * lp[0] + r0.y * lp[1] + r0.z * lp[2];
            const double wy = r1.x * lp[0] + r1.y * lp[1] + r1.z * lp[2];
            const double wz = r2.x * lp[0] + r2.y * lp[1] + r2.z * lp[2];
            const double lonDeg = std::atan2(wy, wx) * static_cast<double>(k_RadToDeg);
            const double latDeg = std::asin(std::clamp(wz, -1.0, 1.0)) * static_cast<double>(k_RadToDeg);

            if (std::fabs(latDeg) > 6.0)
            {
                continue;
            }

            if (lonDeg >= -3.0 && lonDeg <= 3.0)
            {
                hotspotMaxRelief = std::max(hotspotMaxRelief, cs.x);
                hotspotMaxTime = std::max(hotspotMaxTime, cs.z);
            }
            else if (lonDeg < -5.0 && lonDeg >= -25.0)
            {
                ++upstreamVolcanic;
            }
            else if (lonDeg > 3.5 && lonDeg <= 7.0)
            {
                ++binCount[0];
                binTimeSum[0] += static_cast<double>(cs.z);
            }
            else if (lonDeg > 7.0 && lonDeg <= 10.5)
            {
                ++binCount[1];
                binTimeSum[1] += static_cast<double>(cs.z);
            }
            else if (lonDeg > 10.5 && lonDeg <= 16.0)
            {
                ++binCount[2];
                binTimeSum[2] += static_cast<double>(cs.z);
            }
        }

        // Verify PlumeCount == 0 no-op invariance on an extra dispatch
        plumes.PlumeCount = 0u;
        plumes.Time = 999.0f;
        if (buffers.UpdateUniform("PlumeConstants", std::as_bytes(std::span{ &plumes, 1u })) != PlanetGpuError::Success)
        {
            return false;
        }

        {
            rhi::CommandBuffer cmd = context.Session.BeginCommands();
            (*shaders)->RecordDispatch(cmd, **bindings, "ApplyMantlePlumesCS", k_PointCount);
            buffers.RecordReadbackCopies(cmd);
            if (!context.Session.SubmitAndWait(cmd))
            {
                return false;
            }
        }

        const PlanetGpuResult<std::vector<math::Float4>> statesAfterNoOp = buffers.ReadFloat4s("CrustState");
        if (!statesAfterNoOp.has_value() || statesAfterNoOp->size() != k_PointCount)
        {
            return false;
        }

        int32_t noOpDiffs = 0;
        for (uint32_t cell = 0u; cell < k_PointCount; ++cell)
        {
            if (std::memcmp(&(*statesAfterChain)[cell], &(*statesAfterNoOp)[cell], sizeof(math::Float4)) != 0)
            {
                ++noOpDiffs;
            }
        }

        const double mean0 = binCount[0] > 0 ? (binTimeSum[0] / binCount[0]) : 0.0;
        const double mean1 = binCount[1] > 0 ? (binTimeSum[1] / binCount[1]) : 0.0;
        const double mean2 = binCount[2] > 0 ? (binTimeSum[2] / binCount[2]) : 0.0;
        const int32_t totalTrail = binCount[0] + binCount[1] + binCount[2];

        const bool passed = hotspotMaxRelief >= 2.0f &&
                            hotspotMaxTime >= 68.0f &&
                            totalTrail >= 6 &&
                            binCount[0] > 0 && binCount[1] > 0 && binCount[2] > 0 &&
                            mean0 > mean1 && mean1 > mean2 &&
                            upstreamVolcanic == 0 &&
                            noOpDiffs == 0;

        std::println("plumes (order {}): peak_relief={:.2f} km peak_time={:.1f} My trail={} bins=({}/{}/{}) ages=({:.1f}>{:.1f}>{:.1f}) upstream={} noop_diffs={}",
                     k_Order, hotspotMaxRelief, hotspotMaxTime, totalTrail,
                     binCount[0], binCount[1], binCount[2], mean0, mean1, mean2, upstreamVolcanic, noOpDiffs);
        return passed;
    }
}
