#include "PassChecks.hpp"
#include "CheckHarness.hpp"
#include "CommandBuffer.hpp"
#include "GpuBuffers.hpp"
#include "PlanetMath.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "ReferenceSet.hpp"
#include "StepState.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace planet
{
    namespace
    {
        constexpr std::array<std::string_view, 15> k_AllEntryPoints
        {
            "MapProbeCS", "OwnersCS", "CountVotesCS", "SwapToMajorityCS", "FillGapsCS", "RemoveSubductedCS",
            "CountLiveCellsCS", "OffsetLiveCellsCS", "ScatterLiveCellsCS",
            "CountFrontPointsCS", "OffsetFrontPointsCS", "ScatterFrontPointsCS", "NearestFrontPointCS",
            "SumPullCS", "UpliftCS",
        };

        constexpr std::array<NamedCheck, 9> k_AllChecks
        {
            NamedCheck{ "map", CheckMap },
            NamedCheck{ "owners", CheckOwners },
            NamedCheck{ "front", CheckFront },
            NamedCheck{ "crust", CheckCrust },
            NamedCheck{ "live", CheckLiveCells },
            NamedCheck{ "distance", CheckFrontDistance },
            NamedCheck{ "uplift", CheckUplift },
            NamedCheck{ "torque", CheckTorqueSums },
            NamedCheck{ "chain", CheckChain },
        };

        // NearestFrontPointCS and UpliftCS run this many threads to a group on the live cell list
        constexpr uint32_t k_ListGroupSize = 64u;
        // FrontPoints holds one float4 per slot: x, y, z, and the point index in w
        constexpr size_t k_FrontPointWords = 4u;
        constexpr size_t k_FrontPointIndexWord = 3u;
        constexpr int64_t k_AllowedFrontPointTies = 10;

        struct FrontPointTally
        {
            int64_t Wrong{ 0 };
            int64_t Ties{ 0 };
            int64_t DistanceBad{ 0 };
        };

        [[nodiscard]] int64_t VerifyPlateCaps(std::span<const math::Float4> centres,
                                              std::span<const float> spreads,
                                              const HostLiveList& expected,
                                              int32_t point_count,
                                              int32_t plate_count,
                                              double& largest_spread);
    }

    std::span<const std::string_view> AllCheckEntryPoints() noexcept
    {
        return k_AllEntryPoints;
    }

    std::span<const NamedCheck> AllChecks() noexcept
    {
        return k_AllChecks;
    }

    bool CheckLiveCells(CheckContext& context)
    {
        constexpr std::array<std::string_view, 3> k_EntryPoints
        {
            "CountLiveCellsCS", "OffsetLiveCellsCS", "ScatterLiveCellsCS",
        };
        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const size_t points = static_cast<size_t>(pointCount);
        const size_t plates = static_cast<size_t>(plateCount);
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);
        const std::optional<HostLiveList> expected = BuildHostLiveList(context.Reference, "p3_ref_alive");

        if (!shaders.has_value() || !expected.has_value())
        {
            return false;
        }

        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 2> inputs
        {
            ReferenceBufferSpec{ flagNames.Alive, "p3_ref_alive", ReferenceArrayForm::Flag, false },
            ReferenceBufferSpec{ flagNames.Continental, "p3_ref_continental", ReferenceArrayForm::Flag, false },
        };
        const size_t plateWords = plates * sizeof(uint32_t);
        const std::array<ZeroBufferSpec, 9> zeroed
        {
            ZeroBufferSpec{ "PlateCapCenters", plates * 4u * sizeof(float), true },
            ZeroBufferSpec{ "PlateCapSpread", plateWords, true },
            ZeroBufferSpec{ "PlateCellCount", plateWords, true },
            ZeroBufferSpec{ "PlateContinentalCount", plateWords, false },
            ZeroBufferSpec{ "PlatePositionSum", plates * 3u * sizeof(int32_t), false },
            ZeroBufferSpec{ "PlateLiveCursor", plateWords, false },
            ZeroBufferSpec{ "PlateLiveOffset", (plates + 1u) * sizeof(uint32_t), true },
            ZeroBufferSpec{ "LiveDispatch", 3u * sizeof(uint32_t), true },
            ZeroBufferSpec{ "LiveCell", plates * points * sizeof(uint32_t), true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "CountLiveCellsCS", size) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
            !AddZeroedBuffers(buffers, zeroed))
        {
            return false;
        }

        const uint32_t liveThreads = LiveGroupCount(size) * k_LiveGroupSize;
        const std::array<DispatchStage, 3> stages
        {
            DispatchStage{ "CountLiveCellsCS", liveThreads },
            DispatchStage{ "OffsetLiveCellsCS", 1u },
            DispatchStage{ "ScatterLiveCellsCS", liveThreads },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const PlanetGpuResult<std::vector<math::Float4>> centres = buffers.ReadFloat4s("PlateCapCenters");
        const PlanetGpuResult<std::vector<float>> spreads = buffers.ReadFloats("PlateCapSpread");
        const PlanetGpuResult<std::vector<uint32_t>> counts = buffers.ReadWords("PlateCellCount");
        const PlanetGpuResult<std::vector<uint32_t>> offsets = buffers.ReadWords("PlateLiveOffset");
        const PlanetGpuResult<std::vector<uint32_t>> dispatch = buffers.ReadWords("LiveDispatch");
        const PlanetGpuResult<std::vector<uint32_t>> list = buffers.ReadWords("LiveCell");

        if (!centres.has_value() || !spreads.has_value() || !counts.has_value() ||
            !offsets.has_value() || !dispatch.has_value() || !list.has_value())
        {
            return false;
        }

        double largestSpread = 0.0;
        const int64_t capsWrong = VerifyPlateCaps(*centres, *spreads, *expected, pointCount, plateCount, largestSpread);
        std::println("live: plate caps {} wrong, largest spread {:.4f} (1 - cos of the cap radius)",
                     capsWrong, largestSpread);

        const uint32_t total = static_cast<uint32_t>(expected->Cells.size());
        const std::array<uint32_t, 3> expectedDispatch{ DivRoundUp(total, k_ListGroupSize), 1u, 1u };
        int64_t countsWrong = 0;
        int64_t platesWrong = 0;

        for (int32_t plate = 0; plate < plateCount; ++plate)
        {
            const size_t plateIndex = static_cast<size_t>(plate);
            const uint32_t begin = expected->Offsets[plateIndex];
            const uint32_t end = expected->Offsets[plateIndex + 1u];
            countsWrong += (*counts)[plateIndex] != end - begin ? 1 : 0;

            if ((*offsets)[plateIndex] != begin)
            {
                ++platesWrong;
                continue;
            }

            std::vector<uint32_t> actual(list->begin() + begin, list->begin() + end);
            std::ranges::sort(actual);
            platesWrong += std::ranges::equal(actual, std::span{ expected->Cells }.subspan(begin, end - begin)) ? 0 : 1;
        }

        const bool totalRight = (*offsets)[plates] == total;
        const bool dispatchRight = std::ranges::equal(*dispatch, expectedDispatch);
        std::println("live: {} live cells on {} plates, {} counts wrong, {} plate ranges wrong, total {}, dispatch {} {} {} ({})",
                     total, plates, countsWrong, platesWrong, totalRight ? "right" : "wrong",
                     (*dispatch)[0], (*dispatch)[1], (*dispatch)[2], dispatchRight ? "right" : "wrong");
        return countsWrong == 0 && platesWrong == 0 && totalRight && dispatchRight && capsWrong == 0;
    }

    bool CheckFrontDistance(CheckContext& context)
    {
        constexpr std::array<std::string_view, 4> k_EntryPoints
        {
            "CountFrontPointsCS", "OffsetFrontPointsCS", "ScatterFrontPointsCS", "NearestFrontPointCS",
        };
        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const size_t points = static_cast<size_t>(pointCount);
        const size_t plates = static_cast<size_t>(plateCount);
        const size_t cellCount = plates * points;
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);
        const std::optional<std::vector<math::Float3x3>> rotations = context.Reference.Float3x3s("rotation");
        const std::optional<std::vector<uint32_t>> subduction = context.Reference.Words("front_subduction");
        const std::optional<std::vector<int32_t>> plateOnTop = context.Reference.Ints("p3_plate");
        const std::optional<std::vector<int32_t>> expectedFrontPoint = context.Reference.Ints("p4_cell_front_point");
        const std::optional<std::vector<float>> expectedDistance = context.Reference.Floats("p4_cell_distance");
        const std::optional<HostLiveList> live = BuildHostLiveList(context.Reference, "p3_ref_alive");

        if (!shaders.has_value() || !rotations.has_value() || !subduction.has_value() || !plateOnTop.has_value() ||
            !expectedFrontPoint.has_value() || !expectedDistance.has_value() || !live.has_value())
        {
            return false;
        }

        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 3> inputs
        {
            ReferenceBufferSpec{ "FrontSubduction", "front_subduction", ReferenceArrayForm::Word, false },
            ReferenceBufferSpec{ "Pass3Plate", "p3_plate", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ flagNames.Alive, "p3_ref_alive", ReferenceArrayForm::Flag, false },
        };
        const size_t plateWords = plates * sizeof(uint32_t);
        const std::array<ZeroBufferSpec, 6> zeroed
        {
            ZeroBufferSpec{ "PlateFrontPointCount", plateWords, true },
            ZeroBufferSpec{ "PlateFrontPointCursor", plateWords, false },
            ZeroBufferSpec{ "PlateFrontPointOffset", (plates + 1u) * sizeof(uint32_t), true },
            ZeroBufferSpec{ "FrontPoints", points * k_FrontPointWords * sizeof(uint32_t), true },
            ZeroBufferSpec{ "CellFrontPoint", cellCount * sizeof(int32_t), true },
            ZeroBufferSpec{ "CellDistance", cellCount * sizeof(float), true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "CountFrontPointsCS", size) ||
            !AddRotationsBuffer(buffers, context.Reference) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
            !AddHostLiveList(buffers, *live) ||
            !AddZeroedBuffers(buffers, zeroed))
        {
            return false;
        }

        const uint32_t liveCount = static_cast<uint32_t>(live->Cells.size());
        const std::array<DispatchStage, 4> stages
        {
            DispatchStage{ "CountFrontPointsCS", size.PointCount },
            DispatchStage{ "OffsetFrontPointsCS", 1u },
            DispatchStage{ "ScatterFrontPointsCS", size.PointCount },
            DispatchStage{ "NearestFrontPointCS", liveCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const PlanetGpuResult<std::vector<uint32_t>> counts = buffers.ReadWords("PlateFrontPointCount");
        const PlanetGpuResult<std::vector<uint32_t>> offsets = buffers.ReadWords("PlateFrontPointOffset");
        const PlanetGpuResult<std::vector<uint32_t>> frontPointWords = buffers.ReadWords("FrontPoints");
        const PlanetGpuResult<std::vector<int32_t>> cellFrontPoints = buffers.ReadInts("CellFrontPoint");
        const PlanetGpuResult<std::vector<float>> cellDistances = buffers.ReadFloats("CellDistance");

        if (!counts.has_value() || !offsets.has_value() || !frontPointWords.has_value() ||
            !cellFrontPoints.has_value() || !cellDistances.has_value())
        {
            return false;
        }

        std::vector<std::vector<int32_t>> expectedLists(plates);

        for (int32_t point = 0; point < pointCount; ++point)
        {
            const size_t pointIndex = static_cast<size_t>(point);

            if ((*subduction)[pointIndex] == 1u)
            {
                expectedLists[static_cast<size_t>((*plateOnTop)[pointIndex])].push_back(point);
            }
        }

        std::vector<int32_t> frontPoints(points);

        for (int32_t slot = 0; slot < pointCount; ++slot)
        {
            const size_t slotIndex = static_cast<size_t>(slot);
            frontPoints[slotIndex] =
                static_cast<int32_t>((*frontPointWords)[slotIndex * k_FrontPointWords + k_FrontPointIndexWord]);
        }

        int64_t listsWrong = 0;
        uint32_t expectedOffset = 0u;

        for (int32_t plate = 0; plate < plateCount; ++plate)
        {
            const size_t plateIndex = static_cast<size_t>(plate);
            const std::vector<int32_t>& expected = expectedLists[plateIndex];
            const uint32_t expectedCount = static_cast<uint32_t>(expected.size());

            if ((*counts)[plateIndex] != expectedCount || (*offsets)[plateIndex] != expectedOffset)
            {
                ++listsWrong;
                expectedOffset += expectedCount;
                continue;
            }

            std::vector<int32_t> actual(
                frontPoints.begin() + expectedOffset,
                frontPoints.begin() + expectedOffset + expectedCount);
            std::ranges::sort(actual);

            if (actual != expected)
            {
                ++listsWrong;
            }

            expectedOffset += expectedCount;
        }

        if ((*offsets)[plates] != expectedOffset)
        {
            ++listsWrong;
        }

        std::println("distance: {} front points on {} plates, {} plate lists wrong", expectedOffset, plates, listsWrong);

        const double spacingKm = PointSpacingRadians(pointCount) * k_PlanetRadiusKm;
        const int64_t totalCells = static_cast<int64_t>(cellCount);
        FrontPointTally tally;

        for (int64_t flat = 0; flat < totalCells; ++flat)
        {
            const size_t flatIndex = static_cast<size_t>(flat);

            if (live->Alive[flatIndex] == 0u)
            {
                continue;
            }

            const int32_t expected = (*expectedFrontPoint)[flatIndex];
            const int32_t actual = (*cellFrontPoints)[flatIndex];
            const float expectedKm = (*expectedDistance)[flatIndex];
            const float actualKm = (*cellDistances)[flatIndex];
            const double allowedKm = std::max(0.01 * spacingKm, 1.0e-4 * static_cast<double>(expectedKm));

            if (actual != expected && (actual < 0 || expected < 0))
            {
                ++tally.Wrong;
                continue;
            }

            if (actual != expected)
            {
                const int32_t plate = static_cast<int32_t>(flat / pointCount);
                const int32_t cell = static_cast<int32_t>(flat % pointCount);
                const Double3 world = CellInWorld(*rotations, plate, cell, pointCount);
                const double toActual = ChordKilometers(world, LatticePointDouble(actual, pointCount));
                const double toExpected = ChordKilometers(world, LatticePointDouble(expected, pointCount));
                const bool tie = std::fabs(toActual - toExpected) <= 0.01 * spacingKm;
                tally.Ties += tie ? 1 : 0;
                tally.Wrong += tie ? 0 : 1;
            }

            if (!(actualKm == expectedKm) &&
                !(std::fabs(static_cast<double>(actualKm) - static_cast<double>(expectedKm)) <= allowedKm))
            {
                ++tally.DistanceBad;
            }
        }

        std::println("distance: {} live cells, {} take another front point ({} near ties, {} wrong), {} distances outside the tolerance",
                     liveCount, tally.Ties + tally.Wrong, tally.Ties, tally.Wrong, tally.DistanceBad);
        return listsWrong == 0 && tally.Wrong == 0 && tally.Ties < k_AllowedFrontPointTies && tally.DistanceBad == 0;
    }

    bool CheckUplift(CheckContext& context)
    {
        constexpr std::array<std::string_view, 1> k_EntryPoints{ "UpliftCS" };
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);
        const std::optional<HostLiveList> live = BuildHostLiveList(context.Reference, "p3_ref_alive");

        if (!shaders.has_value() || !live.has_value())
        {
            return false;
        }

        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const StepSize size
        {
            static_cast<uint32_t>(context.Reference.PointCount()),
            static_cast<uint32_t>(context.Reference.PlateCount()),
        };
        const std::array<ReferenceBufferSpec, 8> inputs
        {
            ReferenceBufferSpec{ "CellFrontPoint", "p4_cell_front_point", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "CellDistance", "p4_cell_distance", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ "FrontStrength", "front_strength", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ flagNames.Alive, "p3_ref_alive", ReferenceArrayForm::Flag, false },
            ReferenceBufferSpec{ flagNames.Continental, "p3_ref_continental", ReferenceArrayForm::Flag, true },
            ReferenceBufferSpec{ "CrustRelief", "p3_ref_relief", ReferenceArrayForm::Float, true },
            ReferenceBufferSpec{ "CrustOrogeny", "p3_ref_orogeny", ReferenceArrayForm::Int, true },
            ReferenceBufferSpec{ "CrustOrogenyTime", "p3_ref_orogeny_time", ReferenceArrayForm::Float, true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "UpliftCS", size) ||
            !AddTimeUniform(buffers, **shaders, "UpliftCS", "UpliftConstants", context.Reference.Time()) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
            !AddHostLiveList(buffers, *live))
        {
            return false;
        }

        const std::array<DispatchStage, 1> stages
        {
            DispatchStage{ "UpliftCS", static_cast<uint32_t>(live->Cells.size()) },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        const std::array<ArrayCompareSpec, 4> comparisons
        {
            ArrayCompareSpec{ flagNames.Continental, "p5_ref_continental", ReferenceArrayForm::Flag, 0.0f, 0.0f, 1, true },
            ArrayCompareSpec{ "CrustRelief", "p5_ref_relief", ReferenceArrayForm::Float, 1.0e-5f, 1.0e-4f, 1, true },
            ArrayCompareSpec{ "CrustOrogeny", "p5_ref_orogeny", ReferenceArrayForm::Int, 0.0f, 0.0f, 1, true },
            ArrayCompareSpec{ "CrustOrogenyTime", "p5_ref_orogeny_time", ReferenceArrayForm::Float, 1.0e-5f, 1.0e-4f, 1, true },
        };
        bool passed = true;

        for (const ArrayCompareSpec& spec : comparisons)
        {
            passed = CompareArray("uplift", buffers, context.Reference, storage, spec, live->Alive) && passed;
        }

        return passed;
    }

    bool CheckTorqueSums(CheckContext& context)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "CountLiveCellsCS", "SumPullCS" };
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const size_t plates = static_cast<size_t>(plateCount);
        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 6> inputs
        {
            ReferenceBufferSpec{ "FrontSubduction", "front_subduction", ReferenceArrayForm::Word, false },
            ReferenceBufferSpec{ "SecondPlate", "front_second_plate", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "FrontSpeed", "front_speed", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ "FrontSlabAge", "front_slab_age", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ flagNames.Alive, "p3_ref_alive", ReferenceArrayForm::Flag, false },
            ReferenceBufferSpec{ flagNames.Continental, "p3_ref_continental", ReferenceArrayForm::Flag, false },
        };
        const std::array<ZeroBufferSpec, 4> zeroed
        {
            ZeroBufferSpec{ "PlateCellCount", plates * sizeof(uint32_t), true },
            ZeroBufferSpec{ "PlateContinentalCount", plates * sizeof(uint32_t), true },
            ZeroBufferSpec{ "PlatePositionSum", plates * 3u * sizeof(int32_t), true },
            ZeroBufferSpec{ "PlatePull", plates * 3u * sizeof(int32_t), true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "CountLiveCellsCS", size) ||
            !AddRotationsBuffer(buffers, context.Reference) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
            !AddZeroedBuffers(buffers, zeroed))
        {
            return false;
        }

        const uint32_t liveThreads = LiveGroupCount(size) * k_LiveGroupSize;
        const std::array<DispatchStage, 2> stages
        {
            DispatchStage{ "CountLiveCellsCS", liveThreads },
            DispatchStage{ "SumPullCS", size.PointCount },
        };

        if (!RunDispatches(context.Session, **shaders, buffers, stages))
        {
            return false;
        }

        return CompareTorqueSums("torque", buffers, context.Reference);
    }

    bool CheckChain(CheckContext& context)
    {
        const int32_t pointCount = context.Reference.PointCount();
        const int32_t plateCount = context.Reference.PlateCount();
        const PlanetGpuResult<std::unique_ptr<PlanetShaders>> shaders =
            context.Session.CreateShaders(context.CrustFlags, k_StepEntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        GpuBuffers buffers(context.Session.Resources());

        if (!AddStepBuffers(buffers, **shaders, context.Reference, StepReadback::Outputs))
        {
            return false;
        }

        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const PlanetGpuResult<std::unique_ptr<PlanetStep>> step = PlanetStep::Create(**shaders, buffers, size);

        if (!step.has_value())
        {
            return false;
        }

        rhi::CommandBuffer commandBuffer = context.Session.BeginCommands();

        // Run the full step twice with a reset before each so a missed reset buffer shows up as a mismatch
        for (int32_t run = 0; run < 2; ++run)
        {
            if ((*step)->RecordReset(commandBuffer) != PlanetGpuError::Success)
            {
                context.Session.DiscardCommands(commandBuffer);
                return false;
            }

            (*step)->RecordStep(commandBuffer);
        }

        buffers.RecordReadbackCopies(commandBuffer);

        if (!context.Session.SubmitAndWait(commandBuffer))
        {
            return false;
        }

        const std::optional<std::vector<uint8_t>> alive = context.Reference.Bytes("p3_ref_alive");

        if (!alive.has_value())
        {
            return false;
        }

        const CrustFlagStorage storage = (*shaders)->CrustFlags();
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const int64_t allowedOwners = std::max<int64_t>(pointCount / 10000, 1);
        const std::array<ArrayCompareSpec, 11> comparisons
        {
            ArrayCompareSpec{ "Pass3Plate", "p3_plate", ReferenceArrayForm::Int, 0.0f, 0.0f, allowedOwners, false },
            ArrayCompareSpec{ "Pass3Cell", "p3_cell", ReferenceArrayForm::Int, 0.0f, 0.0f, allowedOwners, false },
            ArrayCompareSpec{ "FrontSubduction", "front_subduction", ReferenceArrayForm::Int, 0.0f, 0.0f, 1, false },
            ArrayCompareSpec{ "FrontStrength", "front_strength", ReferenceArrayForm::Float, 1.0e-4f, 1.0e-6f, 1, false },
            ArrayCompareSpec{ flagNames.Alive, "p3_ref_alive", ReferenceArrayForm::Flag, 0.0f, 0.0f, 1, false },
            ArrayCompareSpec{ "CrustBirth", "p3_ref_birth", ReferenceArrayForm::Float, 0.0f, 0.0f, 1, false },
            ArrayCompareSpec{ flagNames.Continental, "p5_ref_continental", ReferenceArrayForm::Flag, 0.0f, 0.0f, 1, true },
            ArrayCompareSpec{ "CrustRelief", "p5_ref_relief", ReferenceArrayForm::Float, 1.0e-5f, 1.0e-4f, 1, true },
            ArrayCompareSpec{ "CrustOrogeny", "p5_ref_orogeny", ReferenceArrayForm::Int, 0.0f, 0.0f, 1, true },
            ArrayCompareSpec{ "CrustOrogenyTime", "p5_ref_orogeny_time", ReferenceArrayForm::Float, 1.0e-5f, 1.0e-4f, 1, true },
            ArrayCompareSpec{ "CellFrontPoint", "p4_cell_front_point", ReferenceArrayForm::Int, 0.0f, 0.0f, k_AllowedFrontPointTies, true },
        };
        bool passed = true;

        for (const ArrayCompareSpec& spec : comparisons)
        {
            passed = CompareArray("chain", buffers, context.Reference, storage, spec, *alive) && passed;
        }

        return CompareTorqueSums("chain p6", buffers, context.Reference) && passed;
    }

    namespace
    {
        int64_t VerifyPlateCaps(std::span<const math::Float4> centres,
                                std::span<const float> spreads,
                                const HostLiveList& expected,
                                int32_t point_count,
                                int32_t plate_count,
                                double& largest_spread)
        {
            // The float dot product on the GPU may round the other way, so the spread may be short by 1e-6
            constexpr double k_SpreadRounding = 1.0e-6;
            int64_t capsWrong = 0;

            for (int32_t plate = 0; plate < plate_count; ++plate)
            {
                const size_t plateIndex = static_cast<size_t>(plate);
                const math::Float4& centre = centres[plateIndex];
                const uint32_t begin = expected.Offsets[plateIndex];
                const uint32_t end = expected.Offsets[plateIndex + 1u];

                if (begin == end)
                {
                    capsWrong += centre.w == k_CapEmpty ? 0 : 1;
                    continue;
                }

                if (centre.w != k_CapValid)
                {
                    ++capsWrong;
                    continue;
                }

                double needed = 0.0;
                const math::Float3 unit = centre.xyz();

                for (uint32_t entry = begin; entry < end; ++entry)
                {
                    const int32_t cell = static_cast<int32_t>(expected.Cells[entry] % static_cast<uint32_t>(point_count));
                    const Double3 point = LatticePointDouble(cell, point_count);
                    const double cosine = Dot(point, unit);
                    needed = std::max(needed, 1.0 - cosine);
                }

                largest_spread = std::max(largest_spread, needed);
                capsWrong += needed <= static_cast<double>(spreads[plateIndex]) + k_SpreadRounding ? 0 : 1;
            }

            return capsWrong;
        }
    }
}
