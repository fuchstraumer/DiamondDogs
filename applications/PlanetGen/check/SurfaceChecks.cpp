#include "PassChecks.hpp"
#include "CheckHarness.hpp"
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
        [[nodiscard]] int64_t CountIntDifferences(std::span<const int32_t> actual,
                                                  std::span<const int32_t> expected) noexcept;
    }

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
        constexpr std::array<std::string_view, 4> k_Outputs{ "BestPlate", "BestCell", "SecondPlate", "SecondCell" };
        constexpr std::array<std::string_view, 4> k_Expected
        {
            "owners_best_plate", "owners_best_cell", "owners_second_plate", "owners_second_cell",
        };
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
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 4> inputs
        {
            ReferenceBufferSpec{ flagNames.Alive, "ref_alive", ReferenceArrayForm::Flag, false },
            ReferenceBufferSpec{ flagNames.Continental, "ref_continental", ReferenceArrayForm::Flag, false },
            ReferenceBufferSpec{ "CrustBirth", "ref_birth", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ "PreviousPlate", "previous_plate", ReferenceArrayForm::Int, false },
        };
        const size_t pointBytes = static_cast<size_t>(pointCount) * sizeof(int32_t);
        const std::array<ZeroBufferSpec, 4> outputs
        {
            ZeroBufferSpec{ "BestPlate", pointBytes, true },
            ZeroBufferSpec{ "BestCell", pointBytes, true },
            ZeroBufferSpec{ "SecondPlate", pointBytes, true },
            ZeroBufferSpec{ "SecondCell", pointBytes, true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "OwnersCS", size) ||
            !AddRotationsBuffer(buffers, context.Reference) ||
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

        std::vector<uint8_t> differs(static_cast<size_t>(pointCount), 0u);

        for (int32_t output = 0; output < std::ssize(k_Outputs); ++output)
        {
            const size_t outputIndex = static_cast<size_t>(output);
            const PlanetGpuResult<std::vector<int32_t>> actual = buffers.ReadInts(k_Outputs[outputIndex]);
            const std::optional<std::vector<int32_t>> expected = context.Reference.Ints(k_Expected[outputIndex]);

            if (!actual.has_value() || !expected.has_value())
            {
                return false;
            }

            for (int32_t point = 0; point < pointCount; ++point)
            {
                const size_t pointIndex = static_cast<size_t>(point);

                if ((*actual)[pointIndex] != (*expected)[pointIndex])
                {
                    differs[pointIndex] = 1u;
                }
            }
        }

        const int64_t different = std::ranges::count(differs, uint8_t{ 1u });
        const int64_t allowed = std::max<int64_t>(pointCount / 10000, 1);
        std::println("owners: {} points differ (allowed fewer than {})", different, allowed);
        return different < allowed;
    }

    bool CheckFront(CheckContext& context)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "CountVotesCS", "SwapToMajorityCS" };
        constexpr std::array<std::string_view, 4> k_Outputs{ "BestPlate", "BestCell", "SecondPlate", "SecondCell" };
        constexpr std::array<std::string_view, 4> k_Expected
        {
            "front_best_plate", "front_best_cell", "front_second_plate", "front_second_cell",
        };
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
        const std::array<ReferenceBufferSpec, 5> inputs
        {
            ReferenceBufferSpec{ CrustFlagBufferNames(storage).Continental, "ref_continental", ReferenceArrayForm::Flag, false },
            ReferenceBufferSpec{ "BestPlate", "owners_best_plate", ReferenceArrayForm::Int, true },
            ReferenceBufferSpec{ "BestCell", "owners_best_cell", ReferenceArrayForm::Int, true },
            ReferenceBufferSpec{ "SecondPlate", "owners_second_plate", ReferenceArrayForm::Int, true },
            ReferenceBufferSpec{ "SecondCell", "owners_second_cell", ReferenceArrayForm::Int, true },
        };
        const size_t voteBytes = static_cast<size_t>(plateCount) * static_cast<size_t>(plateCount) * sizeof(uint32_t);
        const std::array<ZeroBufferSpec, 2> zeroed
        {
            ZeroBufferSpec{ "PairVotes", voteBytes, false },
            ZeroBufferSpec{ "LowVotes", voteBytes, false },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "CountVotesCS", size) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
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

        int64_t different = 0;

        for (int32_t output = 0; output < std::ssize(k_Outputs); ++output)
        {
            const size_t outputIndex = static_cast<size_t>(output);
            const PlanetGpuResult<std::vector<int32_t>> actual = buffers.ReadInts(k_Outputs[outputIndex]);
            const std::optional<std::vector<int32_t>> expected = context.Reference.Ints(k_Expected[outputIndex]);

            if (!actual.has_value() || !expected.has_value())
            {
                return false;
            }

            different += CountIntDifferences(*actual, *expected);
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
        const CrustFlagBuffers flagNames = CrustFlagBufferNames(storage);
        const StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const std::array<ReferenceBufferSpec, 13> inputs
        {
            ReferenceBufferSpec{ "PreviousPlate", "previous_plate", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "Poles", "poles", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ "Rates", "rates", ReferenceArrayForm::Float, false },
            ReferenceBufferSpec{ "BestPlate", "front_best_plate", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "BestCell", "front_best_cell", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "SecondPlate", "front_second_plate", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ "SecondCell", "front_second_cell", ReferenceArrayForm::Int, false },
            ReferenceBufferSpec{ flagNames.Alive, "ref_alive", ReferenceArrayForm::Flag, true },
            ReferenceBufferSpec{ flagNames.Continental, "ref_continental", ReferenceArrayForm::Flag, true },
            ReferenceBufferSpec{ "CrustBirth", "ref_birth", ReferenceArrayForm::Float, true },
            ReferenceBufferSpec{ "CrustRelief", "ref_relief", ReferenceArrayForm::Float, true },
            ReferenceBufferSpec{ "CrustOrogeny", "ref_orogeny", ReferenceArrayForm::Int, true },
            ReferenceBufferSpec{ "CrustOrogenyTime", "ref_orogeny_time", ReferenceArrayForm::Float, true },
        };
        const size_t pointBytes = static_cast<size_t>(pointCount) * sizeof(uint32_t);
        const std::array<ZeroBufferSpec, 6> zeroed
        {
            ZeroBufferSpec{ "Pass3Plate", pointBytes, true },
            ZeroBufferSpec{ "Pass3Cell", pointBytes, true },
            ZeroBufferSpec{ "FrontSubduction", pointBytes, true },
            ZeroBufferSpec{ "FrontStrength", pointBytes, true },
            ZeroBufferSpec{ "FrontSpeed", pointBytes, true },
            ZeroBufferSpec{ "FrontSlabAge", pointBytes, true },
        };
        GpuBuffers buffers(context.Session.Resources());

        if (!AddParamsUniform(buffers, **shaders, "FillGapsCS", size) ||
            !AddTimeUniform(buffers, **shaders, "FillGapsCS", "Pass3Constants", context.Reference.Time()) ||
            !AddRotationsBuffer(buffers, context.Reference) ||
            !AddReferenceBuffers(buffers, context.Reference, storage, inputs) ||
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

        const std::array<ArrayCompareSpec, 12> comparisons
        {
            ArrayCompareSpec{ "Pass3Plate", "p3_plate", ReferenceArrayForm::Int },
            ArrayCompareSpec{ "Pass3Cell", "p3_cell", ReferenceArrayForm::Int },
            ArrayCompareSpec{ "FrontSubduction", "front_subduction", ReferenceArrayForm::Int },
            ArrayCompareSpec{ flagNames.Alive, "p3_ref_alive", ReferenceArrayForm::Flag },
            ArrayCompareSpec{ flagNames.Continental, "p3_ref_continental", ReferenceArrayForm::Flag },
            ArrayCompareSpec{ "CrustBirth", "p3_ref_birth", ReferenceArrayForm::Float, 0.0f, 0.0f },
            ArrayCompareSpec{ "CrustRelief", "p3_ref_relief", ReferenceArrayForm::Float, 0.0f, 0.0f },
            ArrayCompareSpec{ "CrustOrogeny", "p3_ref_orogeny", ReferenceArrayForm::Int },
            ArrayCompareSpec{ "CrustOrogenyTime", "p3_ref_orogeny_time", ReferenceArrayForm::Float, 0.0f, 0.0f },
            ArrayCompareSpec{ "FrontStrength", "front_strength", ReferenceArrayForm::Float, 1.0e-4f, 1.0e-6f },
            ArrayCompareSpec{ "FrontSpeed", "front_speed", ReferenceArrayForm::Float, 1.0e-4f, 1.0e-6f },
            ArrayCompareSpec{ "FrontSlabAge", "front_slab_age", ReferenceArrayForm::Float, 1.0e-4f, 1.0e-6f },
        };
        bool passed = true;

        for (const ArrayCompareSpec& spec : comparisons)
        {
            passed = CompareArray("crust", buffers, context.Reference, storage, spec, {}) && passed;
        }

        return passed;
    }

    namespace
    {
        int64_t CountIntDifferences(std::span<const int32_t> actual,
                                    std::span<const int32_t> expected) noexcept
        {
            int64_t different = 0;

            for (int64_t index = 0; index < std::ssize(expected); ++index)
            {
                const size_t element = static_cast<size_t>(index);

                if (actual[element] != expected[element])
                {
                    ++different;
                }
            }

            return different;
        }
    }
}
