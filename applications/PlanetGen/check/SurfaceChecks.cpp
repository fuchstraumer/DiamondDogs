#include "PlanetGpuErrors.hpp"
#include "StepChecks.hpp"
#include "CheckHarness.hpp"
#include "GpuBuffers.hpp"
#include "Healpix.hpp"
#include "PlanetMath.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
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
}
