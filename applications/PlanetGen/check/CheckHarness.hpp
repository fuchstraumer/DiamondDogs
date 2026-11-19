#pragma once
#ifndef DIAMOND_DOGS_PLANET_CHECK_HARNESS_HPP
#define DIAMOND_DOGS_PLANET_CHECK_HARNESS_HPP
#include "StepState.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace planet
{
    class GpuBuffers;
    class PlanetSession;
    class ReferenceSet;

    struct CheckContext
    {
        PlanetSession& Session;
        const ReferenceSet& Reference;
        CrustFlagChoice CrustFlags{ CrustFlagChoice::Invalid };
    };

    struct DispatchStage
    {
        std::string_view EntryPoint;
        uint32_t ThreadCount{ 0u };
    };

    struct ZeroBufferSpec
    {
        std::string_view BufferName;
        size_t ByteSize{ 0u };
        bool Readable{ false };
    };

    struct HostLiveList
    {
        std::vector<uint32_t> Cells;
        std::vector<uint32_t> Offsets;
        std::vector<uint8_t> Alive;
    };

    struct ArrayCompareSpec
    {
        std::string_view BufferName;
        std::string_view ExpectedFile;
        ReferenceArrayForm Form{ ReferenceArrayForm::Invalid };
        float RelativeTolerance{ 0.0f };
        float AbsoluteTolerance{ 0.0f };
        // Non-float arrays pass when differing elements are strictly less than AllowedDifferences (1 means exact)
        int64_t AllowedDifferences{ 1 };
        bool LiveOnly{ false };
    };

    [[nodiscard]] bool AddZeroedBuffers(GpuBuffers& buffers, std::span<const ZeroBufferSpec> specs);

    [[nodiscard]] bool RunDispatches(PlanetSession& session,
                                     const PlanetShaders& shaders,
                                     const GpuBuffers& buffers,
                                     std::span<const DispatchStage> stages);

    [[nodiscard]] std::optional<HostLiveList> BuildHostLiveList(const ReferenceSet& reference,
                                                                std::string_view alive_file);

    [[nodiscard]] bool AddHostLiveList(GpuBuffers& buffers, const HostLiveList& list);

    [[nodiscard]] int64_t CountDifferentBytes(std::span<const std::byte> actual,
                                              std::span<const std::byte> expected,
                                              size_t element_size);

    [[nodiscard]] int64_t CountDifferentLiveBytes(std::span<const std::byte> actual,
                                                  std::span<const std::byte> expected,
                                                  size_t element_size,
                                                  std::span<const uint8_t> alive);

    [[nodiscard]] bool CompareArray(std::string_view label_prefix,
                                    const GpuBuffers& buffers,
                                    const ReferenceSet& reference,
                                    CrustFlagStorage storage,
                                    const ArrayCompareSpec& spec,
                                    std::span<const uint8_t> live_mask);

    [[nodiscard]] bool CompareTorqueSums(std::string_view label_prefix,
                                         const GpuBuffers& buffers,
                                         const ReferenceSet& reference);

    [[nodiscard]] bool CompareCrustFlags(std::string_view label_prefix,
                                         const GpuBuffers& buffers,
                                         const ReferenceSet& reference,
                                         CrustFlagStorage storage,
                                         std::string_view alive_file,
                                         std::string_view continental_file,
                                         std::span<const uint8_t> live_mask);

    [[nodiscard]] bool CompareCrustState(std::string_view label_prefix,
                                         const GpuBuffers& buffers,
                                         const ReferenceSet& reference,
                                         std::string_view relief_file,
                                         std::string_view birth_file,
                                         std::string_view orogeny_time_file,
                                         std::string_view orogeny_file,
                                         std::span<const uint8_t> live_mask,
                                         float relative_tolerance,
                                         float absolute_tolerance);

    [[nodiscard]] bool CompareFrontData(std::string_view label_prefix,
                                        const GpuBuffers& buffers,
                                        const ReferenceSet& reference,
                                        bool check_speed_and_age);

    [[nodiscard]] bool CompareOwnersXY(std::string_view label_prefix,
                                       const GpuBuffers& buffers,
                                       const ReferenceSet& reference,
                                       std::string_view plate_file,
                                       std::string_view cell_file,
                                       int64_t allowed_differences);
}

#endif // !DIAMOND_DOGS_PLANET_CHECK_HARNESS_HPP
