#include "CheckHarness.hpp"
#include "CommandBuffer.hpp"
#include "GpuBuffers.hpp"
#include "PlanetGpuErrors.hpp"
#include "PlanetMath.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "ReferenceSet.hpp"
#include "StepState.hpp"
#include "math/MathStorage.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace planet
{
    namespace
    {
        // Fixed-point scales from the pass 6 contract so atomic adds stay order-independent
        constexpr double k_PositionSumScale = 16384.0;
        constexpr double k_PullScale = 4096.0;
        constexpr double k_RelativeTolerance = 1.0e-4;

        [[nodiscard]] bool CompareFloatArray(std::string_view label_prefix,
                                             const GpuBuffers& buffers,
                                             const ReferenceSet& reference,
                                             const ArrayCompareSpec& spec,
                                             std::span<const uint8_t> live_mask);
        [[nodiscard]] int64_t CountDifferentInts(std::span<const int32_t> actual, std::span<const int32_t> expected) noexcept;
        [[nodiscard]] double LargestRowDifference(std::span<const int32_t> actual,
                                                  double scale,
                                                  std::span<const math::Float3> expected,
                                                  int32_t plate) noexcept;
        [[nodiscard]] bool CompareCrustFlagBit(std::string_view label_prefix,
                                               std::span<const uint8_t> actual_flags,
                                               uint8_t bit_mask,
                                               const ReferenceSet& reference,
                                               std::string_view expected_file,
                                               std::span<const uint8_t> live_mask);
        [[nodiscard]] bool CompareFloat4Component(std::string_view label_prefix,
                                                  std::span<const math::Float4> actual_values,
                                                  size_t component,
                                                  const ReferenceSet& reference,
                                                  std::string_view expected_file,
                                                  std::span<const uint8_t> live_mask,
                                                  float relative_tolerance,
                                                  float absolute_tolerance);
    }

    bool AddZeroedBuffers(GpuBuffers& buffers, std::span<const ZeroBufferSpec> specs)
    {
        for (const ZeroBufferSpec& spec : specs)
        {
            if (buffers.AddZeroedStorage(spec.BufferName, spec.ByteSize, spec.Readable) != PlanetGpuError::Success)
            {
                return false;
            }
        }

        return true;
    }

    bool RunDispatches(PlanetSession& session,
                       const PlanetShaders& shaders,
                       const GpuBuffers& buffers,
                       std::span<const DispatchStage> stages)
    {
        const PlanetGpuResult<std::unique_ptr<PassBindings>> bindings = shaders.Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        rhi::CommandBuffer commandBuffer = session.BeginCommands();

        for (int64_t index = 0; index < std::ssize(stages); ++index)
        {
            if (index > 0)
            {
                RecordComputeBarrier(commandBuffer);
            }

            const DispatchStage& stage = stages[static_cast<size_t>(index)];
            shaders.RecordDispatch(commandBuffer, **bindings, stage.EntryPoint, stage.ThreadCount);
        }

        buffers.RecordReadbackCopies(commandBuffer);
        return session.SubmitAndWait(commandBuffer);
    }

    std::optional<HostLiveList> BuildHostLiveList(const ReferenceSet& reference, std::string_view alive_file)
    {
        std::optional<std::vector<uint8_t>> alive = reference.Bytes(alive_file);

        if (!alive.has_value())
        {
            return std::nullopt;
        }

        const int32_t points = reference.PointCount();
        const int32_t plates = reference.PlateCount();
        HostLiveList list;
        list.Offsets.reserve(static_cast<size_t>(plates) + 1u);

        for (int32_t plate = 0; plate < plates; ++plate)
        {
            list.Offsets.push_back(static_cast<uint32_t>(list.Cells.size()));

            for (int32_t cell = 0; cell < points; ++cell)
            {
                const int64_t flat = static_cast<int64_t>(plate) * static_cast<int64_t>(points) + cell;

                if ((*alive)[static_cast<size_t>(flat)] != 0u)
                {
                    list.Cells.push_back(static_cast<uint32_t>(flat));
                }
            }
        }

        list.Offsets.push_back(static_cast<uint32_t>(list.Cells.size()));
        list.Alive = std::move(*alive);
        return list;
    }

    bool AddHostLiveList(GpuBuffers& buffers, const HostLiveList& list)
    {
        return buffers.AddStorage("LiveCell", std::as_bytes(std::span{ list.Cells }), false) == PlanetGpuError::Success &&
               buffers.AddStorage("PlateLiveOffset", std::as_bytes(std::span{ list.Offsets }), false) == PlanetGpuError::Success;
    }

    int64_t CountDifferentBytes(std::span<const std::byte> actual,
                                std::span<const std::byte> expected,
                                size_t element_size)
    {
        const int64_t elementCount = std::ssize(expected) / static_cast<int64_t>(element_size);
        int64_t different = 0;

        for (int64_t index = 0; index < elementCount; ++index)
        {
            const size_t offset = static_cast<size_t>(index) * element_size;

            if (std::memcmp(actual.data() + offset, expected.data() + offset, element_size) != 0)
            {
                ++different;
            }
        }

        return different;
    }

    int64_t CountDifferentLiveBytes(std::span<const std::byte> actual,
                                    std::span<const std::byte> expected,
                                    size_t element_size,
                                    std::span<const uint8_t> alive)
    {
        int64_t different = 0;

        for (int64_t flat = 0; flat < std::ssize(alive); ++flat)
        {
            const size_t cell = static_cast<size_t>(flat);
            const size_t offset = cell * element_size;

            if (alive[cell] != 0u && std::memcmp(actual.data() + offset, expected.data() + offset, element_size) != 0)
            {
                ++different;
            }
        }

        return different;
    }

    bool CompareArray(std::string_view label_prefix,
                      const GpuBuffers& buffers,
                      const ReferenceSet& reference,
                      CrustFlagStorage storage,
                      const ArrayCompareSpec& spec,
                      std::span<const uint8_t> live_mask)
    {
        if (spec.Form == ReferenceArrayForm::Float)
        {
            return CompareFloatArray(label_prefix, buffers, reference, spec, live_mask);
        }

        const PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(spec.BufferName);
        const std::optional<std::vector<std::byte>> expected =
            ReadReferenceBytes(reference, spec.ExpectedFile, spec.Form, storage);

        if (!resultBytes.has_value() || !expected.has_value())
        {
            return false;
        }

        const size_t elementSize = ReferenceElementSize(spec.Form, storage);
        const int64_t different = spec.LiveOnly
            ? CountDifferentLiveBytes(*resultBytes, *expected, elementSize, live_mask)
            : CountDifferentBytes(*resultBytes, *expected, elementSize);

        if (spec.AllowedDifferences > 1)
        {
            std::println("{} {}: {} differ (allowed fewer than {})",
                         label_prefix, spec.ExpectedFile, different, spec.AllowedDifferences);
        }
        else
        {
            std::println("{} {}: {} differ", label_prefix, spec.ExpectedFile, different);
        }

        return different < spec.AllowedDifferences;
    }

    bool CompareTorqueSums(std::string_view label_prefix,
                           const GpuBuffers& buffers,
                           const ReferenceSet& reference)
    {
        const PlanetGpuResult<std::vector<math::Int4>> plateCellCounters = buffers.ReadInt4s("PlateCounters");
        const PlanetGpuResult<std::vector<int32_t>> continental = buffers.ReadInts("PlateContinentalCount");
        const PlanetGpuResult<std::vector<int32_t>> pull = buffers.ReadInts("PlatePull");
        const std::optional<std::vector<int32_t>> expectedCells = reference.Ints("p6_cell_count");
        const std::optional<std::vector<int32_t>> expectedContinental = reference.Ints("p6_continental_count");
        const std::optional<std::vector<math::Float3>> expectedPositions = reference.Float3s("p6_position_sum");
        const std::optional<std::vector<math::Float3>> expectedPull = reference.Float3s("p6_pull");

        if (!plateCellCounters.has_value() ||
            !continental.has_value() ||
            !pull.has_value() ||
            !expectedCells.has_value() ||
            !expectedContinental.has_value() ||
            !expectedPositions.has_value() ||
            !expectedPull.has_value())
        {
            return false;
        }

        const int32_t plateCount = reference.PlateCount();
        const size_t counterCount = plateCellCounters->size();
        // need to extract the .w component from each Int4 to get the actual cell count
        std::vector<int32_t> cells(counterCount);
        for (size_t i = 0; i < plateCellCounters->size(); ++i)
        {
            cells[i] = (*plateCellCounters)[i].w;
        }

        const int64_t cellsWrong = CountDifferentInts(cells, *expectedCells);
        const int64_t continentalWrong = CountDifferentInts(*continental, *expectedContinental);
        int64_t positionsWrong = 0;
        int64_t pullWrong = 0;
        double largestPosition = 0.0;
        double largestPull = 0.0;

        const size_t storageSize = counterCount * 3u;
        std::vector<int32_t> positionsExtracted(storageSize, 0);
        for (size_t i = 0; i < counterCount; ++i)
        {
            positionsExtracted[(i * 3u) + 0u] = (*plateCellCounters)[i].x;
            positionsExtracted[(i * 3u) + 1u] = (*plateCellCounters)[i].y;
            positionsExtracted[(i * 3u) + 2u] = (*plateCellCounters)[i].z;
        }

        for (int32_t plate = 0; plate < plateCount; ++plate)
        {
            const double positionDifference = LargestRowDifference(positionsExtracted, k_PositionSumScale, *expectedPositions, plate);
            const double pullDifference = LargestRowDifference(*pull, k_PullScale, *expectedPull, plate);
            largestPosition = std::max(largestPosition, positionDifference);
            largestPull = std::max(largestPull, pullDifference);
            positionsWrong += positionDifference > k_RelativeTolerance ? 1 : 0;
            pullWrong += pullDifference > k_RelativeTolerance ? 1 : 0;
        }

        std::println("{}: cell counts {} wrong, continental counts {} wrong (of {} plates)",
                     label_prefix, cellsWrong, continentalWrong, plateCount);
        std::println("{}: position sums {} plates outside 1e-4 (largest {:.3g}), pulls {} plates outside 1e-4 (largest {:.3g})",
                     label_prefix, positionsWrong, largestPosition, pullWrong, largestPull);
        return cellsWrong == 0 && continentalWrong == 0 && positionsWrong == 0 && pullWrong == 0;
    }

    bool CompareCrustFlags(std::string_view label_prefix,
                           const GpuBuffers& buffers,
                           const ReferenceSet& reference,
                           CrustFlagStorage storage,
                           std::string_view alive_file,
                           std::string_view continental_file,
                           std::span<const uint8_t> live_mask)
    {
        const std::string_view bufferName = CrustFlagBufferNames(storage);
        std::vector<uint8_t> actualFlags;

        if (storage == CrustFlagStorage::Bytes)
        {
            const PlanetGpuResult<std::vector<std::byte>> actualBytes = buffers.Read(bufferName);

            if (!actualBytes.has_value())
            {
                return false;
            }

            actualFlags.resize(actualBytes->size());

            for (size_t i = 0u; i < actualBytes->size(); ++i)
            {
                actualFlags[i] = std::to_integer<uint8_t>((*actualBytes)[i]);
            }
        }
        else
        {
            const PlanetGpuResult<std::vector<uint32_t>> actualWords = buffers.ReadWords(bufferName);

            if (!actualWords.has_value())
            {
                return false;
            }

            actualFlags.resize(actualWords->size());

            for (size_t i = 0u; i < actualWords->size(); ++i)
            {
                actualFlags[i] = static_cast<uint8_t>((*actualWords)[i] & 0xFFu);
            }
        }

        bool passed = true;

        if (!alive_file.empty())
        {
            passed = CompareCrustFlagBit(label_prefix, actualFlags, 1u, reference, alive_file, live_mask) && passed;
        }

        if (!continental_file.empty())
        {
            passed = CompareCrustFlagBit(label_prefix, actualFlags, 2u, reference, continental_file, live_mask) && passed;
        }

        return passed;
    }

    bool CompareCrustState(std::string_view label_prefix,
                           const GpuBuffers& buffers,
                           const ReferenceSet& reference,
                           std::string_view relief_file,
                           std::string_view birth_file,
                           std::string_view orogeny_time_file,
                           std::string_view orogeny_file,
                           std::span<const uint8_t> live_mask,
                           float relative_tolerance,
                           float absolute_tolerance)
    {
        const PlanetGpuResult<std::vector<math::Float4>> actual = buffers.ReadFloat4s("CrustState");

        if (!actual.has_value())
        {
            return false;
        }

        bool passed = true;

        if (!relief_file.empty())
        {
            passed = CompareFloat4Component(label_prefix, *actual, 0u, reference, relief_file, live_mask,
                                            relative_tolerance, absolute_tolerance) && passed;
        }

        if (!birth_file.empty())
        {
            passed = CompareFloat4Component(label_prefix, *actual, 1u, reference, birth_file, live_mask,
                                            relative_tolerance, absolute_tolerance) && passed;
        }

        if (!orogeny_time_file.empty())
        {
            passed = CompareFloat4Component(label_prefix, *actual, 2u, reference, orogeny_time_file, live_mask,
                                            relative_tolerance, absolute_tolerance) && passed;
        }

        if (!orogeny_file.empty())
        {
            const std::optional<std::vector<int32_t>> expected = reference.Ints(orogeny_file);

            if (!expected.has_value() || actual->size() != expected->size() ||
                (!live_mask.empty() && live_mask.size() != expected->size()))
            {
                return false;
            }

            int64_t different = 0;

            for (size_t i = 0u; i < expected->size(); ++i)
            {
                if (!live_mask.empty() && live_mask[i] == 0u)
                {
                    continue;
                }

                if (std::bit_cast<int32_t>((*actual)[i].w) != (*expected)[i])
                {
                    ++different;
                }
            }

            std::println("{} {}: {} differ", label_prefix, orogeny_file, different);
            passed = (different == 0) && passed;
        }

        return passed;
    }

    bool CompareFrontData(std::string_view label_prefix,
                          const GpuBuffers& buffers,
                          const ReferenceSet& reference,
                          bool check_speed_and_age)
    {
        constexpr float k_FrontRelTol = 1.0e-4f;
        constexpr float k_FrontAbsTol = 1.0e-6f;
        const PlanetGpuResult<std::vector<math::Float4>> actual = buffers.ReadFloat4s("FrontData");
        const std::optional<std::vector<uint32_t>> expectedSubduction = reference.Words("front_subduction");

        if (!actual.has_value() || !expectedSubduction.has_value() || actual->size() != expectedSubduction->size())
        {
            return false;
        }

        int64_t subductionDiff = 0;

        for (size_t i = 0u; i < expectedSubduction->size(); ++i)
        {
            if (std::bit_cast<uint32_t>((*actual)[i].w) != (*expectedSubduction)[i])
            {
                ++subductionDiff;
            }
        }

        std::println("{} front_subduction: {} differ", label_prefix, subductionDiff);
        bool passed = subductionDiff == 0;
        passed = CompareFloat4Component(label_prefix, *actual, 0u, reference, "front_strength", {}, k_FrontRelTol, k_FrontAbsTol) && passed;

        if (check_speed_and_age)
        {
            passed = CompareFloat4Component(label_prefix, *actual, 1u, reference, "front_speed", {}, k_FrontRelTol, k_FrontAbsTol) && passed;
            passed = CompareFloat4Component(label_prefix, *actual, 2u, reference, "front_slab_age", {}, k_FrontRelTol, k_FrontAbsTol) && passed;
        }

        return passed;
    }

    bool CompareOwnersXY(std::string_view label_prefix,
                         const GpuBuffers& buffers,
                         const ReferenceSet& reference,
                         std::string_view plate_file,
                         std::string_view cell_file,
                         int64_t allowed_differences)
    {
        const PlanetGpuResult<std::vector<math::Int4>> actual = buffers.ReadInt4s("Owners");
        const std::optional<std::vector<int32_t>> expectedPlate = reference.Ints(plate_file);
        const std::optional<std::vector<int32_t>> expectedCell = reference.Ints(cell_file);

        if (!actual.has_value() || !expectedPlate.has_value() || !expectedCell.has_value() ||
            actual->size() != expectedPlate->size() || actual->size() != expectedCell->size())
        {
            return false;
        }

        int64_t plateDiff = 0;
        int64_t cellDiff = 0;

        for (size_t i = 0u; i < actual->size(); ++i)
        {
            if ((*actual)[i].x != (*expectedPlate)[i])
            {
                ++plateDiff;
            }

            if ((*actual)[i].y != (*expectedCell)[i])
            {
                ++cellDiff;
            }
        }

        if (allowed_differences > 1)
        {
            std::println("{} {}: {} differ (allowed fewer than {})", label_prefix, plate_file, plateDiff, allowed_differences);
            std::println("{} {}: {} differ (allowed fewer than {})", label_prefix, cell_file, cellDiff, allowed_differences);
        }
        else
        {
            std::println("{} {}: {} differ", label_prefix, plate_file, plateDiff);
            std::println("{} {}: {} differ", label_prefix, cell_file, cellDiff);
        }

        return plateDiff < allowed_differences && cellDiff < allowed_differences;
    }

    namespace
    {
        bool CompareFloatArray(std::string_view label_prefix,
                               const GpuBuffers& buffers,
                               const ReferenceSet& reference,
                               const ArrayCompareSpec& spec,
                               std::span<const uint8_t> live_mask)
        {
            const PlanetGpuResult<std::vector<float>> actualValues = buffers.ReadFloats(spec.BufferName);
            const std::optional<std::vector<float>> expectedValues = reference.Floats(spec.ExpectedFile);

            if (!actualValues.has_value() || !expectedValues.has_value())
            {
                return false;
            }

            int64_t different = 0;
            double largest = 0.0;

            for (int64_t index = 0; index < std::ssize(*expectedValues); ++index)
            {
                const size_t element = static_cast<size_t>(index);

                if (spec.LiveOnly && live_mask[element] == 0u)
                {
                    continue;
                }

                const float actual = (*actualValues)[element];
                const float expected = (*expectedValues)[element];
                largest = std::max(largest, std::fabs(static_cast<double>(actual) - static_cast<double>(expected)));

                if (!IsClose(actual, expected, spec.RelativeTolerance, spec.AbsoluteTolerance))
                {
                    ++different;
                }
            }

            std::println("{} {}: {} differ, largest difference {:.3g}",
                         label_prefix, spec.ExpectedFile, different, largest);
            return different == 0;
        }

        int64_t CountDifferentInts(std::span<const int32_t> actual, std::span<const int32_t> expected) noexcept
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

        double LargestRowDifference(std::span<const int32_t> actual,
                                    double scale,
                                    std::span<const math::Float3> expected,
                                    int32_t plate) noexcept
        {
            const size_t base = static_cast<size_t>(plate) * 3u;
            const math::Float3& expectedRow = expected[static_cast<size_t>(plate)];
            double largestComponent = 0.0;
            double largestDifference = 0.0;

            for (int32_t axis = 0; axis < 3; ++axis)
            {
                const size_t axisIndex = static_cast<size_t>(axis);
                const double actualValue = static_cast<double>(actual[base + axisIndex]) / scale;
                const double expectedValue = static_cast<double>(expectedRow[axisIndex]);
                largestComponent = std::max(largestComponent, std::fabs(expectedValue));
                largestDifference = std::max(largestDifference, std::fabs(actualValue - expectedValue));
            }

            if (largestComponent == 0.0)
            {
                return largestDifference == 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
            }

            return largestDifference / largestComponent;
        }

        bool CompareCrustFlagBit(std::string_view label_prefix,
                                 std::span<const uint8_t> actual_flags,
                                 uint8_t bit_mask,
                                 const ReferenceSet& reference,
                                 std::string_view expected_file,
                                 std::span<const uint8_t> live_mask)
        {
            const std::optional<std::vector<uint8_t>> expected = reference.Bytes(expected_file);

            if (!expected.has_value() || actual_flags.size() != expected->size() ||
                (!live_mask.empty() && live_mask.size() != expected->size()))
            {
                return false;
            }

            int64_t different = 0;

            for (size_t i = 0u; i < expected->size(); ++i)
            {
                if (!live_mask.empty() && live_mask[i] == 0u)
                {
                    continue;
                }

                const bool actualBit = (actual_flags[i] & bit_mask) != 0u;
                const bool expectedBit = (*expected)[i] != 0u;

                if (actualBit != expectedBit)
                {
                    ++different;
                }
            }

            std::println("{} {}: {} differ", label_prefix, expected_file, different);
            return different == 0;
        }

        bool CompareFloat4Component(std::string_view label_prefix,
                                    std::span<const math::Float4> actual_values,
                                    size_t component,
                                    const ReferenceSet& reference,
                                    std::string_view expected_file,
                                    std::span<const uint8_t> live_mask,
                                    float relative_tolerance,
                                    float absolute_tolerance)
        {
            const std::optional<std::vector<float>> expected = reference.Floats(expected_file);

            if (!expected.has_value() || actual_values.size() != expected->size() ||
                (!live_mask.empty() && live_mask.size() != expected->size()))
            {
                return false;
            }

            int64_t different = 0;
            double largest = 0.0;

            for (size_t i = 0u; i < expected->size(); ++i)
            {
                if (!live_mask.empty() && live_mask[i] == 0u)
                {
                    continue;
                }

                const float actual = actual_values[i][component];
                const float target = (*expected)[i];
                largest = std::max(largest, std::fabs(static_cast<double>(actual) - static_cast<double>(target)));

                if (!IsClose(actual, target, relative_tolerance, absolute_tolerance))
                {
                    ++different;
                }
            }

            std::println("{} {}: {} differ, largest difference {:.3g}", label_prefix, expected_file, different, largest);
            return different == 0;
        }
    }
}
