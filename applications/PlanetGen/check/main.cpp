#include "BootResult.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "Device.hpp"
#include "DeviceConfig.hpp"
#include "DeviceRequirements.hpp"
#include "DeviceStage.hpp"
#include "Fence.hpp"
#include "GpuBuffers.hpp"
#include "GpuStage.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "Queue.hpp"
#include "ReferenceSet.hpp"
#include "StepState.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.hpp"
#include "SpineNeeds.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// PlanetGenCheck: runs the PlanetLab Slang passes through Lodestone and compares each result with the reference files
// of applications/PlanetLab (write_gpu_reference.py). The checks and their tolerances are the ones of the pass
// specification.
//
//     PlanetGenCheck --reference=<folder> [--checks=map,owners,front,crust,live,distance,uplift,torque,chain]
//
// The default is all of them. Each check runs once for each crust flag storage (bytes, words) that the device can
// run. Exit 0 only when every check passes. Validation is Full, so also search the output for validation messages.
namespace
{
    constexpr std::string_view k_ReferenceFlag{ "--reference=" };
    constexpr std::string_view k_ChecksFlag{ "--checks=" };
    constexpr std::array<std::string_view, 9> k_AllChecks{ "map", "owners", "front", "crust", "live", "distance", "uplift", "torque", "chain" };
    constexpr std::array<planet::CrustFlagChoice, 2> k_FlagStorages{ planet::CrustFlagChoice::Bytes, planet::CrustFlagChoice::Words };
    constexpr std::array<std::string_view, 15> k_AllEntryPoints
    {
        "MapProbeCS", "OwnersCS", "CountVotesCS", "SwapToMajorityCS", "FillGapsCS", "RemoveSubductedCS",
        "CountLiveCellsCS", "OffsetLiveCellsCS", "ScatterLiveCellsCS",
        "CountFrontPointsCS", "OffsetFrontPointsCS", "ScatterFrontPointsCS", "NearestFrontPointCS", "UpliftCS",
        "SumPullCS",
    };
    // NearestFrontPointCS and UpliftCS run this many threads to a group on the live cell list (LiveCells.slang)
    constexpr uint32_t k_ListGroupSize = 64u;

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = PLANET_GEN_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    struct GpuContext
    {
        rhi::Device& Device;
        ResourceContext& Resources;
        const rhi::ShaderLibrary& Library;
        rhi::CommandPool& Pool;
        const bootstrap::DeviceFilter Filter;
    };

    // prints a boot failure to stderr
    [[nodiscard]] bool Booted(const bootstrap::BootResult& result)
    {
        if (!result.Succeeded())
        {
            std::println(stderr, "{}", bootstrap::FormatFailure(result));
        }

        return result.Succeeded();
    }

    template<typename T>
    [[nodiscard]] std::span<const std::byte> Bytes(const std::vector<T>& values) noexcept
    {
        return std::as_bytes(std::span{ values });
    }

    template<typename T>
    [[nodiscard]] std::vector<T> FromBytes(const std::vector<std::byte>& bytes, size_t count)
    {
        std::vector<T> values(count);
        std::memcpy(values.data(), bytes.data(), count * sizeof(T));
        return values;
    }

    // records the passes, then the readback copies, and waits for the fence
    template<typename Record>
    [[nodiscard]] bool Submit(GpuContext& gpu, const planet::GpuBuffers& buffers, const Record& record)
    {
        RhiAssert(gpu.Pool.Reset());
        rhi::CommandBuffer commandBuffer(gpu.Pool.GetCommandBuffer(0));
        RhiAssert(commandBuffer.Begin());
        record(commandBuffer);
        buffers.RecordReadbackCopies(commandBuffer);
        RhiAssert(commandBuffer.End());

        const rhi::CommandBufferHandle handle = commandBuffer.Handle();
        rhi::Fence fence(gpu.Device.Handle());
        RhiAssert(rhi::QueueSubmit(gpu.Device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()));

        if (!fence.Wait())
        {
            std::println(stderr, "the submit did not finish");
            return false;
        }

        return true;
    }

    // the creation of shaders and buffers prints its own cause, so a check only has to stop
    [[nodiscard]] bool Failed(planet::PlanetGpuError error)
    {
        return error != planet::PlanetGpuError::Success;
    }

    // lattice point `index` of `count`, in double: the exact point, for the distance test of the map check
    [[nodiscard]] std::array<double, 3> LatticeDouble(int64_t index, int64_t count)
    {
        const double goldenAngle = std::numbers::pi * (3.0 - std::sqrt(5.0));
        const double z = 1.0 - (2.0 * static_cast<double>(index) + 1.0) / static_cast<double>(count);
        const double radius = std::sqrt(std::max(1.0 - z * z, 0.0));
        const double angle = (static_cast<double>(index) + 0.5) * goldenAngle;
        return { radius * std::cos(angle), radius * std::sin(angle), z };
    }

    [[nodiscard]] double Distance(const std::array<double, 3>& point, const float* direction)
    {
        const double dx = point[0] - static_cast<double>(direction[0]);
        const double dy = point[1] - static_cast<double>(direction[1]);
        const double dz = point[2] - static_cast<double>(direction[2]);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // numpy.isclose(actual, expected, rtol, atol), with inf equal to inf
    [[nodiscard]] bool Close(float actual, float expected, float relative, float absolute)
    {
        if (actual == expected)
        {
            return true;
        }

        return std::fabs(actual - expected) <= absolute + relative * std::fabs(expected);
    }

    template<typename T>
    [[nodiscard]] std::vector<std::byte> ToByteVector(const std::vector<T>& values)
    {
        const std::span<const std::byte> bytes = Bytes(values);
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    template<typename T>
    [[nodiscard]] int64_t CountDifferent(const std::vector<T>& actual, const std::vector<T>& expected)
    {
        int64_t different = 0;

        for (size_t index = 0; index < expected.size(); ++index)
        {
            if (std::memcmp(&actual[index], &expected[index], sizeof(T)) != 0)
            {
                ++different;
            }
        }

        return different;
    }

    // compares `expected.size() / element_size` elements bit for bit
    [[nodiscard]] int64_t CountDifferentElements(std::span<const std::byte> actual, std::span<const std::byte> expected, size_t element_size)
    {
        int64_t different = 0;

        for (size_t offset = 0; offset < expected.size(); offset += element_size)
        {
            if (std::memcmp(actual.data() + offset, expected.data() + offset, element_size) != 0)
            {
                ++different;
            }
        }

        return different;
    }

    [[nodiscard]] size_t FlagElementSize(planet::CrustFlagStorage storage) noexcept
    {
        return storage == planet::CrustFlagStorage::Bytes ? sizeof(uint8_t) : sizeof(uint32_t);
    }

    // the uint8 flag file `name` in the storage that the variant reads: one byte or one word for each cell
    [[nodiscard]] std::optional<std::vector<std::byte>> FlagArray(const planet::ReferenceSet& reference, std::string_view name, planet::CrustFlagStorage storage)
    {
        std::optional<std::vector<uint8_t>> flags = reference.Bytes(name);

        if (!flags.has_value())
        {
            return std::nullopt;
        }

        if (storage == planet::CrustFlagStorage::Bytes)
        {
            return ToByteVector(*flags);
        }

        return ToByteVector(std::vector<uint32_t>(flags->begin(), flags->end()));
    }

    [[nodiscard]] bool AddFlags(planet::GpuBuffers& buffers, const planet::ReferenceSet& reference, planet::CrustFlagStorage storage,
                                std::string_view buffer, std::string_view file, bool readable)
    {
        std::optional<std::vector<std::byte>> flags = FlagArray(reference, file, storage);
        return flags.has_value() && buffers.AddStorage(buffer, *flags, readable) == planet::PlanetGpuError::Success;
    }

    // the live cell list as the host makes it from a flag file: the flat index of each live cell, plate by plate and
    // in cell order, and the offset of each plate in it
    struct HostLiveList
    {
        std::vector<uint32_t> Cells;
        std::vector<uint32_t> Offsets;
        std::vector<uint8_t> Alive;
    };

    [[nodiscard]] std::optional<HostLiveList> BuildLiveList(const planet::ReferenceSet& reference, std::string_view alive_file)
    {
        std::optional<std::vector<uint8_t>> alive = reference.Bytes(alive_file);

        if (!alive.has_value())
        {
            return std::nullopt;
        }

        const size_t points = static_cast<size_t>(reference.PointCount());
        const size_t plates = static_cast<size_t>(reference.PlateCount());
        HostLiveList list;
        list.Offsets.reserve(plates + 1u);

        for (size_t plate = 0; plate < plates; ++plate)
        {
            list.Offsets.push_back(static_cast<uint32_t>(list.Cells.size()));

            for (size_t cell = 0; cell < points; ++cell)
            {
                const size_t flat = plate * points + cell;

                if ((*alive)[flat] != 0u)
                {
                    list.Cells.push_back(static_cast<uint32_t>(flat));
                }
            }
        }

        list.Offsets.push_back(static_cast<uint32_t>(list.Cells.size()));
        list.Alive = std::move(*alive);
        return list;
    }

    // the buffers that a pass on the list reads, for a check that runs that pass alone
    [[nodiscard]] bool AddLiveList(planet::GpuBuffers& buffers, const HostLiveList& list)
    {
        return buffers.AddStorage("LiveCell", Bytes(list.Cells), false) == planet::PlanetGpuError::Success &&
               buffers.AddStorage("PlateLiveOffset", Bytes(list.Offsets), false) == planet::PlanetGpuError::Success;
    }

    // compares the elements of live cells only: a pass on the list leaves dead cells as they were
    [[nodiscard]] int64_t CountDifferentLive(std::span<const std::byte> actual, std::span<const std::byte> expected, size_t element_size,
                                             const std::vector<uint8_t>& alive)
    {
        int64_t different = 0;

        for (size_t flat = 0; flat < alive.size(); ++flat)
        {
            const size_t offset = flat * element_size;

            if (alive[flat] != 0u && std::memcmp(actual.data() + offset, expected.data() + offset, element_size) != 0)
            {
                ++different;
            }
        }

        return different;
    }

    // creates the programs of one check in the storage that the run asked for
    [[nodiscard]] planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> CreateShaders(GpuContext& gpu,
                                                                                                planet::CrustFlagChoice crust_flags,
                                                                                                std::span<const std::string_view> entry_points)
    {
        return planet::PlanetShaders::Create(gpu.Device, gpu.Library, gpu.Filter, crust_flags, entry_points);
    }

    // Pass 0: each direction maps to the expected lattice index, or to a point at almost the same distance
    [[nodiscard]] bool CheckMap(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 1> k_EntryPoints{ "MapProbeCS" };
        std::optional<std::vector<float>> directions = reference.Floats("map_directions");
        std::optional<std::vector<uint32_t>> expected = reference.Words("map_nearest");

        if (!directions.has_value() || !expected.has_value())
        {
            return false;
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const uint32_t rowCount = static_cast<uint32_t>(expected->size());
        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("DirectionCount", rowCount),
            planet::UniformUint("LatticeCount", static_cast<uint32_t>(reference.PointCount())),
        };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("MapProbeCS", "MapProbeConstants", constants);

        if (!constantBytes.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const std::vector<uint32_t> zeros(rowCount, 0u);

        if (Failed(buffers.AddUniform("MapProbeConstants", *constantBytes)) ||
            Failed(buffers.AddStorage("MapDirections", Bytes(*directions), false)) ||
            Failed(buffers.AddStorage("MapResults", Bytes(zeros), true)))
        {
            return false;
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "MapProbeCS", rowCount);
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read("MapResults");

        if (!resultBytes.has_value())
        {
            return false;
        }

        const std::vector<uint32_t> actual = FromBytes<uint32_t>(*resultBytes, rowCount);
        const double spacing = std::sqrt(4.0 * std::numbers::pi / static_cast<double>(reference.PointCount()));
        int64_t different = 0;
        int64_t ties = 0;

        for (size_t row = 0; row < actual.size(); ++row)
        {
            if (actual[row] == (*expected)[row])
            {
                continue;
            }

            const float* direction = directions->data() + row * 3u;
            const double gpuDistance = Distance(LatticeDouble(actual[row], reference.PointCount()), direction);
            const double referenceDistance = Distance(LatticeDouble((*expected)[row], reference.PointCount()), direction);
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

    // Pass 1: the best two plates at each point. A float32 rotation can move a few points at cell borders
    [[nodiscard]] bool CheckOwners(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 1> k_EntryPoints{ "OwnersCS" };
        constexpr std::array<std::string_view, 4> k_Outputs{ "BestPlate", "BestCell", "SecondPlate", "SecondCell" };
        constexpr std::array<std::string_view, 4> k_Expected{ "owners_best_plate", "owners_best_cell", "owners_second_plate", "owners_second_cell" };
        const int32_t pointCount = reference.PointCount();
        std::optional<std::vector<float>> rotation = reference.Floats("rotation");
        std::optional<std::vector<float>> birth = reference.Floats("ref_birth");
        std::optional<std::vector<int32_t>> previous = reference.Ints("previous_plate");

        if (!rotation.has_value() || !birth.has_value() || !previous.has_value())
        {
            return false;
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(reference.PlateCount())),
        };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("OwnersCS", "Params", constants);

        if (!constantBytes.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const std::vector<int32_t> zeros(static_cast<size_t>(pointCount), 0);
        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const planet::CrustFlagBuffers flagNames = planet::CrustFlagBufferNames(storage);

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            Failed(buffers.AddStorage("Rotation", Bytes(*rotation), false)) ||
            !AddFlags(buffers, reference, storage, flagNames.Alive, "ref_alive", false) ||
            !AddFlags(buffers, reference, storage, flagNames.Continental, "ref_continental", false) ||
            Failed(buffers.AddStorage("CrustBirth", Bytes(*birth), false)) ||
            Failed(buffers.AddStorage("PreviousPlate", Bytes(*previous), false)))
        {
            return false;
        }

        for (const std::string_view output : k_Outputs)
        {
            if (Failed(buffers.AddStorage(output, Bytes(zeros), true)))
            {
                return false;
            }
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "OwnersCS", static_cast<uint32_t>(pointCount));
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        std::vector<bool> differs(static_cast<size_t>(pointCount), false);

        for (size_t output = 0; output < k_Outputs.size(); ++output)
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(k_Outputs[output]);
            std::optional<std::vector<int32_t>> expected = reference.Ints(k_Expected[output]);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            const std::vector<int32_t> actual = FromBytes<int32_t>(*resultBytes, static_cast<size_t>(pointCount));

            for (size_t point = 0; point < actual.size(); ++point)
            {
                if (actual[point] != (*expected)[point])
                {
                    differs[point] = true;
                }
            }
        }

        const int64_t different = std::ranges::count(differs, true);
        const int64_t allowed = std::max<int64_t>(pointCount / 10000, 1);
        std::println("owners: {} points differ (allowed fewer than {})", different, allowed);
        return different < allowed;
    }

    // Pass 2: one plate on top for each pair of ocean plates. Integer arithmetic only, so the result is exact
    [[nodiscard]] bool CheckFront(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "CountVotesCS", "SwapToMajorityCS" };
        constexpr std::array<std::string_view, 4> k_Outputs{ "BestPlate", "BestCell", "SecondPlate", "SecondCell" };
        constexpr std::array<std::string_view, 4> k_Inputs{ "owners_best_plate", "owners_best_cell", "owners_second_plate", "owners_second_cell" };
        constexpr std::array<std::string_view, 4> k_Expected{ "front_best_plate", "front_best_cell", "front_second_plate", "front_second_cell" };
        const int32_t pointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(plateCount)),
        };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("CountVotesCS", "Params", constants);

        if (!constantBytes.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const std::vector<uint32_t> votes(static_cast<size_t>(plateCount * plateCount), 0u);
        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            !AddFlags(buffers, reference, storage, planet::CrustFlagBufferNames(storage).Continental, "ref_continental", false) ||
            Failed(buffers.AddStorage("PairVotes", Bytes(votes), false)) ||
            Failed(buffers.AddStorage("LowVotes", Bytes(votes), false)))
        {
            return false;
        }

        for (size_t output = 0; output < k_Outputs.size(); ++output)
        {
            std::optional<std::vector<int32_t>> input = reference.Ints(k_Inputs[output]);

            if (!input.has_value() || Failed(buffers.AddStorage(k_Outputs[output], Bytes(*input), true)))
            {
                return false;
            }
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "CountVotesCS", static_cast<uint32_t>(pointCount));
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "SwapToMajorityCS", static_cast<uint32_t>(pointCount));
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        int64_t different = 0;

        for (size_t output = 0; output < k_Outputs.size(); ++output)
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(k_Outputs[output]);
            std::optional<std::vector<int32_t>> expected = reference.Ints(k_Expected[output]);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            different += CountDifferent(FromBytes<int32_t>(*resultBytes, static_cast<size_t>(pointCount)), *expected);
        }

        std::println("front: {} values differ", different);
        return different == 0;
    }

    enum class ArrayForm : uint8_t
    {
        Invalid = 0,
        Int,
        Float,
        Flag,
    };

    struct CrustArray
    {
        std::string_view Buffer;
        std::string_view Input;
        std::string_view Expected;
        ArrayForm Form{ ArrayForm::Invalid };
    };

    // a crust array as the shader stores it: a flag in the variant's storage, an int8 array widened to int
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadCrustArray(const planet::ReferenceSet& reference, std::string_view name, ArrayForm form,
                                                                       planet::CrustFlagStorage storage)
    {
        if (form == ArrayForm::Flag)
        {
            return FlagArray(reference, name, storage);
        }

        if (form == ArrayForm::Int)
        {
            std::optional<std::vector<int32_t>> ints = reference.Ints(name);
            return ints.has_value() ? std::optional{ ToByteVector(*ints) } : std::nullopt;
        }

        std::optional<std::vector<float>> floats = reference.Floats(name);
        return floats.has_value() ? std::optional{ ToByteVector(*floats) } : std::nullopt;
    }

    // Pass 3: new crust at the gaps, removal under subduction fronts, and the front values. The crust flags use the
    // storage of the variant. Integer and crust results are exact; the front values allow a relative 1e-4
    [[nodiscard]] bool CheckCrust(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "FillGapsCS", "RemoveSubductedCS" };
        constexpr std::array<std::string_view, 4> k_FrontInputs{ "BestPlate", "BestCell", "SecondPlate", "SecondCell" };
        constexpr std::array<std::string_view, 4> k_FrontFiles{ "front_best_plate", "front_best_cell", "front_second_plate", "front_second_cell" };
        constexpr std::array<std::string_view, 3> k_FrontFloats{ "FrontStrength", "FrontSpeed", "FrontSlabAge" };
        constexpr std::array<std::string_view, 3> k_FrontFloatFiles{ "front_strength", "front_speed", "front_slab_age" };
        const int32_t pointCount = reference.PointCount();
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const planet::CrustFlagBuffers flagNames = planet::CrustFlagBufferNames(storage);
        const std::array<CrustArray, 6> crust
        {
            CrustArray{ flagNames.Alive, "ref_alive", "p3_ref_alive", ArrayForm::Flag },
            CrustArray{ flagNames.Continental, "ref_continental", "p3_ref_continental", ArrayForm::Flag },
            CrustArray{ "CrustBirth", "ref_birth", "p3_ref_birth", ArrayForm::Float },
            CrustArray{ "CrustRelief", "ref_relief", "p3_ref_relief", ArrayForm::Float },
            CrustArray{ "CrustOrogeny", "ref_orogeny", "p3_ref_orogeny", ArrayForm::Int },
            CrustArray{ "CrustOrogenyTime", "ref_orogeny_time", "p3_ref_orogeny_time", ArrayForm::Float },
        };

        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(reference.PlateCount())),
        };
        const std::array<planet::UniformValue, 1> passConstants{ planet::UniformFloat("Time", reference.Time()) };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("FillGapsCS", "Params", constants);
        planet::PlanetGpuResult<std::vector<std::byte>> passBytes = (*shaders)->UniformBytes("FillGapsCS", "Pass3Constants", passConstants);
        std::optional<std::vector<float>> rotation = reference.Floats("rotation");
        std::optional<std::vector<int32_t>> previous = reference.Ints("previous_plate");
        std::optional<std::vector<float>> poles = reference.Floats("poles");
        std::optional<std::vector<float>> rates = reference.Floats("rates");

        if (!constantBytes.has_value() || !passBytes.has_value() || !rotation.has_value() || !previous.has_value() ||
            !poles.has_value() || !rates.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const std::vector<uint32_t> zeros(static_cast<size_t>(pointCount), 0u);

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            Failed(buffers.AddUniform("Pass3Constants", *passBytes)) ||
            Failed(buffers.AddStorage("Rotation", Bytes(*rotation), false)) ||
            Failed(buffers.AddStorage("PreviousPlate", Bytes(*previous), false)) ||
            Failed(buffers.AddStorage("Poles", Bytes(*poles), false)) ||
            Failed(buffers.AddStorage("Rates", Bytes(*rates), false)) ||
            Failed(buffers.AddStorage("Pass3Plate", Bytes(zeros), true)) ||
            Failed(buffers.AddStorage("Pass3Cell", Bytes(zeros), true)) ||
            Failed(buffers.AddStorage("FrontSubduction", Bytes(zeros), true)))
        {
            return false;
        }

        for (size_t input = 0; input < k_FrontInputs.size(); ++input)
        {
            std::optional<std::vector<int32_t>> values = reference.Ints(k_FrontFiles[input]);

            if (!values.has_value() || Failed(buffers.AddStorage(k_FrontInputs[input], Bytes(*values), false)))
            {
                return false;
            }
        }

        for (const std::string_view output : k_FrontFloats)
        {
            if (Failed(buffers.AddStorage(output, Bytes(zeros), true)))
            {
                return false;
            }
        }

        for (const CrustArray& array : crust)
        {
            std::optional<std::vector<std::byte>> input = ReadCrustArray(reference, array.Input, array.Form, storage);

            if (!input.has_value() || Failed(buffers.AddStorage(array.Buffer, *input, true)))
            {
                return false;
            }
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "FillGapsCS", static_cast<uint32_t>(pointCount));
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "RemoveSubductedCS", static_cast<uint32_t>(pointCount));
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        bool passed = true;

        const auto report = [&passed](std::string_view name, int64_t different)
        {
            std::println("crust {}: {} differ", name, different);
            passed = passed && different == 0;
        };

        for (const auto& [buffer, file] : { std::pair{ "Pass3Plate", "p3_plate" }, std::pair{ "Pass3Cell", "p3_cell" }, std::pair{ "FrontSubduction", "front_subduction" } })
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(buffer);
            std::optional<std::vector<int32_t>> expected = reference.Ints(file);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            report(file, CountDifferent(FromBytes<int32_t>(*resultBytes, static_cast<size_t>(pointCount)), *expected));
        }

        for (const CrustArray& array : crust)
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(array.Buffer);
            std::optional<std::vector<std::byte>> expected = ReadCrustArray(reference, array.Expected, array.Form, storage);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            const size_t elementSize = array.Form == ArrayForm::Flag ? FlagElementSize(storage) : sizeof(uint32_t);
            report(array.Expected, CountDifferentElements(*resultBytes, *expected, elementSize));
        }

        for (size_t output = 0; output < k_FrontFloats.size(); ++output)
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(k_FrontFloats[output]);
            std::optional<std::vector<float>> expected = reference.Floats(k_FrontFloatFiles[output]);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            const std::vector<float> actual = FromBytes<float>(*resultBytes, static_cast<size_t>(pointCount));
            int64_t different = 0;

            for (size_t point = 0; point < actual.size(); ++point)
            {
                if (!Close(actual[point], (*expected)[point], 1.0e-4f, 1.0e-6f))
                {
                    ++different;
                }
            }

            report(k_FrontFloatFiles[output], different);
        }

        return passed;
    }

    constexpr double k_PlanetRadiusKm = 6370.0;

    // great-circle km between two unit vectors, from the chord, as crust.ChordToKilometers does
    [[nodiscard]] double ChordKilometers(const std::array<double, 3>& a, const std::array<double, 3>& b)
    {
        const double dx = a[0] - b[0];
        const double dy = a[1] - b[1];
        const double dz = a[2] - b[2];
        const double chord = std::sqrt(dx * dx + dy * dy + dz * dz);
        return 2.0 * std::asin(std::clamp(0.5 * chord, 0.0, 1.0)) * k_PlanetRadiusKm;
    }

    // cell `cell` of plate `plate` in the world: rotation * frame point, with the float32 rotation of the file
    [[nodiscard]] std::array<double, 3> CellInWorld(const std::vector<float>& rotation, int32_t plate, int32_t cell, int32_t count)
    {
        const std::array<double, 3> frame = LatticeDouble(cell, count);
        const float* matrix = rotation.data() + static_cast<size_t>(plate) * 9u;
        std::array<double, 3> world{};

        for (size_t row = 0; row < 3u; ++row)
        {
            world[row] = static_cast<double>(matrix[row * 3u]) * frame[0] +
                         static_cast<double>(matrix[row * 3u + 1u]) * frame[1] +
                         static_cast<double>(matrix[row * 3u + 2u]) * frame[2];
        }

        return world;
    }

    struct FrontPointTally
    {
        int64_t Wrong{ 0 };
        int64_t Ties{ 0 };
        int64_t DistanceBad{ 0 };
    };

    // Pass 4: the front point lists of each plate, then the nearest front point of each plate cell, by brute force. A
    // cell's front point may differ from the reference only at a tie (1 % of the spacing); distances allow the larger
    // of 1 % of the spacing and 1e-4 of the distance
    [[nodiscard]] bool CheckFrontDistance(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 4> k_EntryPoints{ "CountFrontPointsCS", "OffsetFrontPointsCS", "ScatterFrontPointsCS", "NearestFrontPointCS" };
        const int32_t pointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        const size_t points = static_cast<size_t>(pointCount);
        const size_t plates = static_cast<size_t>(plateCount);
        const size_t cellCount = plates * points;
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(plateCount)),
        };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("CountFrontPointsCS", "Params", constants);
        std::optional<std::vector<float>> rotation = reference.Floats("rotation");
        std::optional<std::vector<uint32_t>> subduction = reference.Words("front_subduction");
        std::optional<std::vector<int32_t>> plateOnTop = reference.Ints("p3_plate");
        std::optional<std::vector<int32_t>> expectedFrontPoint = reference.Ints("p4_cell_front_point");
        std::optional<std::vector<float>> expectedDistance = reference.Floats("p4_cell_distance");
        std::optional<HostLiveList> live = BuildLiveList(reference, "p3_ref_alive");

        if (!constantBytes.has_value() || !rotation.has_value() || !subduction.has_value() || !plateOnTop.has_value() ||
            !expectedFrontPoint.has_value() || !expectedDistance.has_value() || !live.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const std::vector<uint32_t> plateZeros(plates, 0u);
        const std::vector<uint32_t> offsetZeros(plates + 1u, 0u);
        const std::vector<int32_t> pointZeros(points, 0);
        const std::vector<float> positionZeros(points * 3u, 0.0f);
        const std::vector<int32_t> cellZeros(cellCount, 0);

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            Failed(buffers.AddStorage("Rotation", Bytes(*rotation), false)) ||
            Failed(buffers.AddStorage("FrontSubduction", Bytes(*subduction), false)) ||
            Failed(buffers.AddStorage("Pass3Plate", Bytes(*plateOnTop), false)) ||
            !AddFlags(buffers, reference, storage, planet::CrustFlagBufferNames(storage).Alive, "p3_ref_alive", false) ||
            Failed(buffers.AddStorage("PlateFrontPointCount", Bytes(plateZeros), true)) ||
            Failed(buffers.AddStorage("PlateFrontPointCursor", Bytes(plateZeros), false)) ||
            Failed(buffers.AddStorage("PlateFrontPointOffset", Bytes(offsetZeros), true)) ||
            Failed(buffers.AddStorage("FrontPoint", Bytes(pointZeros), true)) ||
            Failed(buffers.AddStorage("FrontPointPosition", Bytes(positionZeros), false)) ||
            Failed(buffers.AddStorage("CellFrontPoint", Bytes(cellZeros), true)) ||
            Failed(buffers.AddStorage("CellDistance", Bytes(cellZeros), true)) ||
            !AddLiveList(buffers, *live))
        {
            return false;
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        // OffsetFrontPointsCS is one workgroup: a thread count of 1 dispatches one group, whatever its size. The search
        // runs on the live cell list from the host, one thread for each live cell
        const uint32_t liveCount = static_cast<uint32_t>(live->Cells.size());

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "CountFrontPointsCS", static_cast<uint32_t>(pointCount));
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "OffsetFrontPointsCS", 1u);
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "ScatterFrontPointsCS", static_cast<uint32_t>(pointCount));
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "NearestFrontPointCS", liveCount);
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        planet::PlanetGpuResult<std::vector<std::byte>> countBytes = buffers.Read("PlateFrontPointCount");
        planet::PlanetGpuResult<std::vector<std::byte>> offsetBytes = buffers.Read("PlateFrontPointOffset");
        planet::PlanetGpuResult<std::vector<std::byte>> frontPointBytes = buffers.Read("FrontPoint");
        planet::PlanetGpuResult<std::vector<std::byte>> cellFrontPointBytes = buffers.Read("CellFrontPoint");
        planet::PlanetGpuResult<std::vector<std::byte>> cellDistanceBytes = buffers.Read("CellDistance");

        if (!countBytes.has_value() || !offsetBytes.has_value() || !frontPointBytes.has_value() || !cellFrontPointBytes.has_value() ||
            !cellDistanceBytes.has_value())
        {
            return false;
        }

        // the front point lists: counts and offsets exact, and the front points of each plate as a set, because the
        // scatter order changes
        std::vector<std::vector<int32_t>> expectedLists(plates);

        for (size_t point = 0; point < points; ++point)
        {
            if ((*subduction)[point] == 1u)
            {
                expectedLists[static_cast<size_t>((*plateOnTop)[point])].push_back(static_cast<int32_t>(point));
            }
        }

        const std::vector<uint32_t> counts = FromBytes<uint32_t>(*countBytes, plates);
        const std::vector<uint32_t> offsets = FromBytes<uint32_t>(*offsetBytes, plates + 1u);
        const std::vector<int32_t> frontPoints = FromBytes<int32_t>(*frontPointBytes, points);
        int64_t listsWrong = 0;
        uint32_t expectedOffset = 0u;

        for (size_t plate = 0; plate < plates; ++plate)
        {
            const std::vector<int32_t>& expected = expectedLists[plate];
            const uint32_t expectedCount = static_cast<uint32_t>(expected.size());

            if (counts[plate] != expectedCount || offsets[plate] != expectedOffset)
            {
                ++listsWrong;
                expectedOffset += expectedCount;
                continue;
            }

            std::vector<int32_t> actual(frontPoints.begin() + expectedOffset, frontPoints.begin() + expectedOffset + expectedCount);
            std::ranges::sort(actual);

            if (actual != expected)
            {
                ++listsWrong;
            }

            expectedOffset += expectedCount;
        }

        if (offsets[plates] != expectedOffset)
        {
            ++listsWrong;
        }

        std::println("distance: {} front points on {} plates, {} plate lists wrong", expectedOffset, plates, listsWrong);

        // the nearest front point of each plate cell
        const std::vector<int32_t> cellFrontPoints = FromBytes<int32_t>(*cellFrontPointBytes, cellCount);
        const std::vector<float> cellDistances = FromBytes<float>(*cellDistanceBytes, cellCount);
        const double spacingKm = std::sqrt(4.0 * std::numbers::pi / static_cast<double>(pointCount)) * k_PlanetRadiusKm;
        FrontPointTally tally;

        // dead cells are not on the list, so the pass does not write them
        for (size_t flat = 0; flat < cellCount; ++flat)
        {
            if (live->Alive[flat] == 0u)
            {
                continue;
            }

            const int32_t expected = (*expectedFrontPoint)[flat];
            const int32_t actual = cellFrontPoints[flat];
            const float expectedKm = (*expectedDistance)[flat];
            const float actualKm = cellDistances[flat];
            const double allowedKm = std::max(0.01 * spacingKm, 1.0e-4 * static_cast<double>(expectedKm));

            if (actual != expected && (actual < 0 || expected < 0))
            {
                ++tally.Wrong;
                continue;
            }

            if (actual != expected)
            {
                const int32_t plate = static_cast<int32_t>(flat / points);
                const int32_t cell = static_cast<int32_t>(flat % points);
                const std::array<double, 3> world = CellInWorld(*rotation, plate, cell, pointCount);
                const double toActual = ChordKilometers(world, LatticeDouble(actual, pointCount));
                const double toExpected = ChordKilometers(world, LatticeDouble(expected, pointCount));
                const bool tie = std::fabs(toActual - toExpected) <= 0.01 * spacingKm;
                tally.Ties += tie ? 1 : 0;
                tally.Wrong += tie ? 0 : 1;
            }

            if (!(actualKm == expectedKm) && !(std::fabs(static_cast<double>(actualKm) - static_cast<double>(expectedKm)) <= allowedKm))
            {
                ++tally.DistanceBad;
            }
        }

        std::println("distance: {} live cells, {} take another front point ({} near ties, {} wrong), {} distances outside the tolerance",
                     liveCount, tally.Ties + tally.Wrong, tally.Ties, tally.Wrong, tally.DistanceBad);
        return listsWrong == 0 && tally.Wrong == 0 && tally.Ties < 10 && tally.DistanceBad == 0;
    }

    // Pass 5: uplift, erosion damping and arc accretion, from the exact pass 4 results. Floats within 1e-4 (and a
    // relative 1e-5); the other arrays exact. Orogeny and accretion flip at a threshold, but a float32 uplift differs
    // from the double one by less than 1e-6 km, and only 3 cells accrete at 100 000 points: an allowance of a few cells
    // would hide a pass with no accretion at all
    [[nodiscard]] bool CheckUplift(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 1> k_EntryPoints{ "UpliftCS" };
        const int32_t pointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        const size_t cellCount = static_cast<size_t>(plateCount) * static_cast<size_t>(pointCount);
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const planet::CrustFlagBuffers flagNames = planet::CrustFlagBufferNames(storage);
        const std::array<CrustArray, 4> crust
        {
            CrustArray{ flagNames.Continental, "p3_ref_continental", "p5_ref_continental", ArrayForm::Flag },
            CrustArray{ "CrustRelief", "p3_ref_relief", "p5_ref_relief", ArrayForm::Float },
            CrustArray{ "CrustOrogeny", "p3_ref_orogeny", "p5_ref_orogeny", ArrayForm::Int },
            CrustArray{ "CrustOrogenyTime", "p3_ref_orogeny_time", "p5_ref_orogeny_time", ArrayForm::Float },
        };

        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(plateCount)),
        };
        const std::array<planet::UniformValue, 1> passConstants{ planet::UniformFloat("Time", reference.Time()) };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("UpliftCS", "Params", constants);
        planet::PlanetGpuResult<std::vector<std::byte>> passBytes = (*shaders)->UniformBytes("UpliftCS", "UpliftConstants", passConstants);
        std::optional<std::vector<int32_t>> cellFrontPoint = reference.Ints("p4_cell_front_point");
        std::optional<std::vector<float>> cellDistance = reference.Floats("p4_cell_distance");
        std::optional<std::vector<float>> strength = reference.Floats("front_strength");
        std::optional<HostLiveList> live = BuildLiveList(reference, "p3_ref_alive");

        if (!constantBytes.has_value() || !passBytes.has_value() || !cellFrontPoint.has_value() || !cellDistance.has_value() ||
            !strength.has_value() || !live.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            Failed(buffers.AddUniform("UpliftConstants", *passBytes)) ||
            Failed(buffers.AddStorage("CellFrontPoint", Bytes(*cellFrontPoint), false)) ||
            Failed(buffers.AddStorage("CellDistance", Bytes(*cellDistance), false)) ||
            Failed(buffers.AddStorage("FrontStrength", Bytes(*strength), false)) ||
            !AddFlags(buffers, reference, storage, flagNames.Alive, "p3_ref_alive", false) ||
            !AddLiveList(buffers, *live))
        {
            return false;
        }

        for (const CrustArray& array : crust)
        {
            std::optional<std::vector<std::byte>> input = ReadCrustArray(reference, array.Input, array.Form, storage);

            if (!input.has_value() || Failed(buffers.AddStorage(array.Buffer, *input, true)))
            {
                return false;
            }
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "UpliftCS", static_cast<uint32_t>(live->Cells.size()));
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        // live cells only: the pass runs on the live cell list, and the reference also erodes dead continental cells
        bool passed = true;

        for (const CrustArray& array : crust)
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(array.Buffer);
            std::optional<std::vector<std::byte>> expected = ReadCrustArray(reference, array.Expected, array.Form, storage);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            if (array.Form != ArrayForm::Float)
            {
                const size_t elementSize = array.Form == ArrayForm::Flag ? FlagElementSize(storage) : sizeof(uint32_t);
                const int64_t different = CountDifferentLive(*resultBytes, *expected, elementSize, live->Alive);
                std::println("uplift {}: {} differ", array.Expected, different);
                passed = passed && different == 0;
                continue;
            }

            const std::vector<float> actualValues = FromBytes<float>(*resultBytes, cellCount);
            const std::vector<float> expectedValues = FromBytes<float>(*expected, cellCount);
            int64_t different = 0;
            double largest = 0.0;

            for (size_t flat = 0; flat < cellCount; ++flat)
            {
                if (live->Alive[flat] == 0u)
                {
                    continue;
                }

                largest = std::max(largest, std::fabs(static_cast<double>(actualValues[flat]) - static_cast<double>(expectedValues[flat])));

                if (!Close(actualValues[flat], expectedValues[flat], 1.0e-5f, 1.0e-4f))
                {
                    ++different;
                }
            }

            std::println("uplift {}: {} differ, largest difference {:.3g}", array.Expected, different, largest);
            passed = passed && different == 0;
        }

        return passed;
    }

    // The fixed-point scales of the pass 6 sums: the shader adds round(value * scale) as int, so the sum does not depend
    // on the order of the atomics or on the subgroup size. The pass specification states the same values
    constexpr double k_PositionSumScale = 16384.0;
    constexpr double k_PullScale = 4096.0;

    // the largest relative difference of a [plate, 3] sum, against the largest component of that plate's expected row
    [[nodiscard]] double LargestRowDifference(std::span<const int32_t> actual, double scale, std::span<const float> expected, size_t plate)
    {
        double largestComponent = 0.0;
        double largestDifference = 0.0;

        for (size_t axis = 0; axis < 3u; ++axis)
        {
            const size_t index = plate * 3u + axis;
            const double value = static_cast<double>(actual[index]) / scale;
            largestComponent = std::max(largestComponent, std::fabs(static_cast<double>(expected[index])));
            largestDifference = std::max(largestDifference, std::fabs(value - static_cast<double>(expected[index])));
        }

        if (largestComponent == 0.0)
        {
            return largestDifference == 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
        }

        return largestDifference / largestComponent;
    }

    // Pass 6: the sums of the torque balance. CountLiveCellsCS makes the counts and position sums, and SumPullCS the
    // pull. Cell counts exact; the position sums and the pulls within a relative 1e-4 of the largest component of each
    // plate's row
    [[nodiscard]] bool CheckTorqueSums(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 2> k_EntryPoints{ "CountLiveCellsCS", "SumPullCS" };
        constexpr double k_RelativeTolerance = 1.0e-4;
        const int32_t pointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        const size_t plates = static_cast<size_t>(plateCount);
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const planet::CrustFlagBuffers flagNames = planet::CrustFlagBufferNames(storage);
        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(plateCount)),
        };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("CountLiveCellsCS", "Params", constants);
        std::optional<std::vector<float>> rotation = reference.Floats("rotation");
        std::optional<std::vector<uint32_t>> subduction = reference.Words("front_subduction");
        std::optional<std::vector<int32_t>> underPlate = reference.Ints("front_second_plate");
        std::optional<std::vector<float>> speed = reference.Floats("front_speed");
        std::optional<std::vector<float>> slabAge = reference.Floats("front_slab_age");
        std::optional<std::vector<int32_t>> expectedCells = reference.Ints("p6_cell_count");
        std::optional<std::vector<int32_t>> expectedContinental = reference.Ints("p6_continental_count");
        std::optional<std::vector<float>> expectedPositions = reference.Floats("p6_position_sum");
        std::optional<std::vector<float>> expectedPull = reference.Floats("p6_pull");

        if (!constantBytes.has_value() || !rotation.has_value() || !subduction.has_value() || !underPlate.has_value() ||
            !speed.has_value() || !slabAge.has_value() || !expectedCells.has_value() || !expectedContinental.has_value() ||
            !expectedPositions.has_value() || !expectedPull.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const std::vector<uint32_t> countZeros(plates, 0u);
        const std::vector<int32_t> rowZeros(plates * 3u, 0);

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            Failed(buffers.AddStorage("Rotation", Bytes(*rotation), false)) ||
            Failed(buffers.AddStorage("FrontSubduction", Bytes(*subduction), false)) ||
            Failed(buffers.AddStorage("SecondPlate", Bytes(*underPlate), false)) ||
            Failed(buffers.AddStorage("FrontSpeed", Bytes(*speed), false)) ||
            Failed(buffers.AddStorage("FrontSlabAge", Bytes(*slabAge), false)) ||
            !AddFlags(buffers, reference, storage, flagNames.Alive, "p3_ref_alive", false) ||
            !AddFlags(buffers, reference, storage, flagNames.Continental, "p3_ref_continental", false) ||
            Failed(buffers.AddStorage("PlateCellCount", Bytes(countZeros), true)) ||
            Failed(buffers.AddStorage("PlateContinentalCount", Bytes(countZeros), true)) ||
            Failed(buffers.AddStorage("PlatePositionSum", Bytes(rowZeros), true)) ||
            Failed(buffers.AddStorage("PlatePull", Bytes(rowZeros), true)))
        {
            return false;
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        // CountLiveCellsCS gives each plate its own row of whole workgroups
        const planet::StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const uint32_t liveThreads = planet::LiveGroupCount(size) * planet::k_LiveGroupSize;

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "CountLiveCellsCS", liveThreads);
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "SumPullCS", static_cast<uint32_t>(pointCount));
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        planet::PlanetGpuResult<std::vector<std::byte>> cellBytes = buffers.Read("PlateCellCount");
        planet::PlanetGpuResult<std::vector<std::byte>> continentalBytes = buffers.Read("PlateContinentalCount");
        planet::PlanetGpuResult<std::vector<std::byte>> positionBytes = buffers.Read("PlatePositionSum");
        planet::PlanetGpuResult<std::vector<std::byte>> pullBytes = buffers.Read("PlatePull");

        if (!cellBytes.has_value() || !continentalBytes.has_value() || !positionBytes.has_value() || !pullBytes.has_value())
        {
            return false;
        }

        const std::vector<int32_t> cells = FromBytes<int32_t>(*cellBytes, plates);
        const std::vector<int32_t> continental = FromBytes<int32_t>(*continentalBytes, plates);
        const std::vector<int32_t> positions = FromBytes<int32_t>(*positionBytes, plates * 3u);
        const std::vector<int32_t> pull = FromBytes<int32_t>(*pullBytes, plates * 3u);
        const int64_t cellsWrong = CountDifferent(cells, *expectedCells);
        const int64_t continentalWrong = CountDifferent(continental, *expectedContinental);
        int64_t positionsWrong = 0;
        int64_t pullWrong = 0;
        double largestPosition = 0.0;
        double largestPull = 0.0;

        for (size_t plate = 0; plate < plates; ++plate)
        {
            const double positionDifference = LargestRowDifference(positions, k_PositionSumScale, *expectedPositions, plate);
            const double pullDifference = LargestRowDifference(pull, k_PullScale, *expectedPull, plate);
            largestPosition = std::max(largestPosition, positionDifference);
            largestPull = std::max(largestPull, pullDifference);
            positionsWrong += positionDifference > k_RelativeTolerance ? 1 : 0;
            pullWrong += pullDifference > k_RelativeTolerance ? 1 : 0;
        }

        std::println("torque: cell counts {} wrong, continental counts {} wrong (of {} plates)", cellsWrong, continentalWrong, plates);
        std::println("torque: position sums {} plates outside 1e-4 (largest {:.3g}), pulls {} plates outside 1e-4 (largest {:.3g})",
                     positionsWrong, largestPosition, pullWrong, largestPull);
        return cellsWrong == 0 && continentalWrong == 0 && positionsWrong == 0 && pullWrong == 0;
    }

    // Pass 3b: the live cell list, from the crust that pass 3 left. The offsets and the group counts of the indirect
    // dispatch exact; each plate's range of the list holds exactly its live cells, in any order
    [[nodiscard]] bool CheckLiveCells(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr std::array<std::string_view, 3> k_EntryPoints{ "CountLiveCellsCS", "OffsetLiveCellsCS", "ScatterLiveCellsCS" };
        const int32_t pointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        const size_t plates = static_cast<size_t>(plateCount);
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, k_EntryPoints);
        std::optional<HostLiveList> expected = BuildLiveList(reference, "p3_ref_alive");

        if (!shaders.has_value() || !expected.has_value())
        {
            return false;
        }

        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const planet::CrustFlagBuffers flagNames = planet::CrustFlagBufferNames(storage);
        const std::array<planet::UniformValue, 2> constants
        {
            planet::UniformUint("PointCount", static_cast<uint32_t>(pointCount)),
            planet::UniformUint("PlateCount", static_cast<uint32_t>(plateCount)),
        };
        planet::PlanetGpuResult<std::vector<std::byte>> constantBytes = (*shaders)->UniformBytes("CountLiveCellsCS", "Params", constants);

        if (!constantBytes.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);
        const std::vector<uint32_t> plateZeros(plates, 0u);
        const std::vector<uint32_t> offsetZeros(plates + 1u, 0u);
        const std::vector<int32_t> rowZeros(plates * 3u, 0);
        const std::vector<uint32_t> dispatchZeros(3u, 0u);
        const std::vector<uint32_t> listZeros(plates * static_cast<size_t>(pointCount), 0u);

        if (Failed(buffers.AddUniform("Params", *constantBytes)) ||
            !AddFlags(buffers, reference, storage, flagNames.Alive, "p3_ref_alive", false) ||
            !AddFlags(buffers, reference, storage, flagNames.Continental, "p3_ref_continental", false) ||
            Failed(buffers.AddStorage("PlateCellCount", Bytes(plateZeros), true)) ||
            Failed(buffers.AddStorage("PlateContinentalCount", Bytes(plateZeros), false)) ||
            Failed(buffers.AddStorage("PlatePositionSum", Bytes(rowZeros), false)) ||
            Failed(buffers.AddStorage("PlateLiveCursor", Bytes(plateZeros), false)) ||
            Failed(buffers.AddStorage("PlateLiveOffset", Bytes(offsetZeros), true)) ||
            Failed(buffers.AddStorage("LiveDispatch", Bytes(dispatchZeros), true)) ||
            Failed(buffers.AddStorage("LiveCell", Bytes(listZeros), true)))
        {
            return false;
        }

        planet::PlanetGpuResult<std::unique_ptr<planet::PassBindings>> bindings = (*shaders)->Bind(buffers.Bound());

        if (!bindings.has_value())
        {
            return false;
        }

        const planet::StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        const uint32_t liveThreads = planet::LiveGroupCount(size) * planet::k_LiveGroupSize;

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            (*shaders)->RecordDispatch(command_buffer, **bindings, "CountLiveCellsCS", liveThreads);
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "OffsetLiveCellsCS", 1u);
            planet::RecordComputeBarrier(command_buffer);
            (*shaders)->RecordDispatch(command_buffer, **bindings, "ScatterLiveCellsCS", liveThreads);
        };

        if (!Submit(gpu, buffers, record))
        {
            return false;
        }

        planet::PlanetGpuResult<std::vector<std::byte>> countBytes = buffers.Read("PlateCellCount");
        planet::PlanetGpuResult<std::vector<std::byte>> offsetBytes = buffers.Read("PlateLiveOffset");
        planet::PlanetGpuResult<std::vector<std::byte>> dispatchBytes = buffers.Read("LiveDispatch");
        planet::PlanetGpuResult<std::vector<std::byte>> listBytes = buffers.Read("LiveCell");

        if (!countBytes.has_value() || !offsetBytes.has_value() || !dispatchBytes.has_value() || !listBytes.has_value())
        {
            return false;
        }

        const std::vector<uint32_t> counts = FromBytes<uint32_t>(*countBytes, plates);
        const std::vector<uint32_t> offsets = FromBytes<uint32_t>(*offsetBytes, plates + 1u);
        const std::vector<uint32_t> dispatch = FromBytes<uint32_t>(*dispatchBytes, 3u);
        const std::vector<uint32_t> list = FromBytes<uint32_t>(*listBytes, expected->Cells.size());
        const uint32_t total = static_cast<uint32_t>(expected->Cells.size());
        const std::array<uint32_t, 3> expectedDispatch{ (total + k_ListGroupSize - 1u) / k_ListGroupSize, 1u, 1u };
        int64_t countsWrong = 0;
        int64_t platesWrong = 0;

        for (size_t plate = 0; plate < plates; ++plate)
        {
            const uint32_t begin = expected->Offsets[plate];
            const uint32_t end = expected->Offsets[plate + 1u];
            countsWrong += counts[plate] != end - begin ? 1 : 0;

            if (offsets[plate] != begin)
            {
                ++platesWrong;
                continue;
            }

            std::vector<uint32_t> actual(list.begin() + begin, list.begin() + end);
            std::ranges::sort(actual);
            platesWrong += std::ranges::equal(actual, std::span{ expected->Cells }.subspan(begin, end - begin)) ? 0 : 1;
        }

        const bool totalRight = offsets[plates] == total;
        const bool dispatchRight = std::ranges::equal(dispatch, expectedDispatch);
        std::println("live: {} live cells on {} plates, {} counts wrong, {} plate ranges wrong, total {}, dispatch {} {} {} ({})",
                     total, plates, countsWrong, platesWrong, totalRight ? "right" : "wrong", dispatch[0], dispatch[1], dispatch[2],
                     dispatchRight ? "right" : "wrong");
        return countsWrong == 0 && platesWrong == 0 && totalRight && dispatchRight;
    }

    struct ChainArray
    {
        std::string_view Buffer;
        std::string_view Expected;
        ArrayForm Form{ ArrayForm::Invalid };
        // floats: the tolerance of the single-pass check that writes the array
        float Relative{ 0.0f };
        float Absolute{ 0.0f };
        // the plate and cell on top come from pass 1, so they allow its float32 border points
        bool OwnersAllowance{ false };
        // written by a pass on the live cell list, which leaves dead cells as they were
        bool LiveOnly{ false };
    };

    // The whole step: PlanetStep runs passes 1 to 6 on the start state, each pass on the buffers that the passes before
    // it wrote. It runs twice in one submit, with a reset before each, so a reset that misses a buffer shows as a
    // difference. The tolerances are the ones of the single-pass checks: the plate and cell on top allow the owners
    // check's border points, and CellFrontPoint the near ties of the distance check. The arrays that passes on the live
    // cell list write are compared on live cells only
    [[nodiscard]] bool CheckChain(GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        constexpr int64_t k_AllowedFrontPointTies = 10;
        constexpr double k_RelativeTolerance = 1.0e-4;
        const int32_t pointCount = reference.PointCount();
        const int32_t plateCount = reference.PlateCount();
        const size_t plates = static_cast<size_t>(plateCount);
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders = CreateShaders(gpu, crust_flags, planet::k_StepEntryPoints);

        if (!shaders.has_value())
        {
            return false;
        }

        planet::GpuBuffers buffers(gpu.Resources);

        if (!planet::AddStepBuffers(buffers, **shaders, reference, planet::StepReadback::Outputs))
        {
            return false;
        }

        const planet::StepSize size{ static_cast<uint32_t>(pointCount), static_cast<uint32_t>(plateCount) };
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetStep>> step = planet::PlanetStep::Create(**shaders, buffers, size);

        if (!step.has_value())
        {
            return false;
        }

        bool recorded = true;

        const auto record = [&](rhi::CommandBuffer& command_buffer)
        {
            for (uint32_t run = 0; run < 2u; ++run)
            {
                recorded = recorded && (*step)->RecordReset(command_buffer) == planet::PlanetGpuError::Success;
                (*step)->RecordStep(command_buffer);
            }
        };

        if (!Submit(gpu, buffers, record) || !recorded)
        {
            return false;
        }

        const planet::CrustFlagStorage storage = (*shaders)->CrustFlags();
        const planet::CrustFlagBuffers flagNames = planet::CrustFlagBufferNames(storage);
        const std::array<ChainArray, 10> arrays
        {
            ChainArray{ "Pass3Plate", "p3_plate", ArrayForm::Int, 0.0f, 0.0f, true },
            ChainArray{ "Pass3Cell", "p3_cell", ArrayForm::Int, 0.0f, 0.0f, true },
            ChainArray{ "FrontSubduction", "front_subduction", ArrayForm::Int },
            ChainArray{ "FrontStrength", "front_strength", ArrayForm::Float, 1.0e-4f, 1.0e-6f },
            ChainArray{ flagNames.Alive, "p3_ref_alive", ArrayForm::Flag },
            // a tolerance of 0 is exact: pass 3 copies or sets the birth time and never computes it
            ChainArray{ "CrustBirth", "p3_ref_birth", ArrayForm::Float, 0.0f, 0.0f },
            ChainArray{ flagNames.Continental, "p5_ref_continental", ArrayForm::Flag, 0.0f, 0.0f, false, true },
            ChainArray{ "CrustRelief", "p5_ref_relief", ArrayForm::Float, 1.0e-5f, 1.0e-4f, false, true },
            ChainArray{ "CrustOrogeny", "p5_ref_orogeny", ArrayForm::Int, 0.0f, 0.0f, false, true },
            ChainArray{ "CrustOrogenyTime", "p5_ref_orogeny_time", ArrayForm::Float, 1.0e-5f, 1.0e-4f, false, true },
        };
        std::optional<std::vector<uint8_t>> alive = reference.Bytes("p3_ref_alive");

        if (!alive.has_value())
        {
            return false;
        }

        bool passed = true;

        for (const ChainArray& array : arrays)
        {
            planet::PlanetGpuResult<std::vector<std::byte>> resultBytes = buffers.Read(array.Buffer);
            std::optional<std::vector<std::byte>> expected = ReadCrustArray(reference, array.Expected, array.Form, storage);

            if (!resultBytes.has_value() || !expected.has_value())
            {
                return false;
            }

            if (array.Form != ArrayForm::Float)
            {
                const size_t elementSize = array.Form == ArrayForm::Flag ? FlagElementSize(storage) : sizeof(uint32_t);
                const int64_t different = array.LiveOnly ? CountDifferentLive(*resultBytes, *expected, elementSize, *alive)
                                                         : CountDifferentElements(*resultBytes, *expected, elementSize);
                // the same allowance as the owners check: fewer than 1 point in 10 000
                const int64_t allowed = array.OwnersAllowance ? std::max<int64_t>(pointCount / 10000, 1) : 1;
                std::println("chain {}: {} differ (allowed fewer than {})", array.Expected, different, allowed);
                passed = passed && different < allowed;
                continue;
            }

            const size_t count = expected->size() / sizeof(float);
            const std::vector<float> actualValues = FromBytes<float>(*resultBytes, count);
            const std::vector<float> expectedValues = FromBytes<float>(*expected, count);
            int64_t different = 0;
            double largest = 0.0;

            for (size_t index = 0; index < count; ++index)
            {
                if (array.LiveOnly && (*alive)[index] == 0u)
                {
                    continue;
                }

                largest = std::max(largest, std::fabs(static_cast<double>(actualValues[index]) - static_cast<double>(expectedValues[index])));

                if (!Close(actualValues[index], expectedValues[index], array.Relative, array.Absolute))
                {
                    ++different;
                }
            }

            std::println("chain {}: {} differ, largest difference {:.3g}", array.Expected, different, largest);
            passed = passed && different == 0;
        }

        // pass 4: the nearest front point of each cell, with the near ties of the distance check
        planet::PlanetGpuResult<std::vector<std::byte>> frontPointBytes = buffers.Read("CellFrontPoint");
        std::optional<std::vector<int32_t>> expectedFrontPoint = reference.Ints("p4_cell_front_point");

        if (!frontPointBytes.has_value() || !expectedFrontPoint.has_value())
        {
            return false;
        }

        const int64_t frontPointsDifferent = CountDifferentLive(*frontPointBytes, Bytes(*expectedFrontPoint), sizeof(int32_t), *alive);
        std::println("chain p4_cell_front_point: {} differ (allowed fewer than {})", frontPointsDifferent, k_AllowedFrontPointTies);
        passed = passed && frontPointsDifferent < k_AllowedFrontPointTies;

        // pass 6: the sums, with the tolerances of the torque check
        planet::PlanetGpuResult<std::vector<std::byte>> cellBytes = buffers.Read("PlateCellCount");
        planet::PlanetGpuResult<std::vector<std::byte>> continentalBytes = buffers.Read("PlateContinentalCount");
        planet::PlanetGpuResult<std::vector<std::byte>> positionBytes = buffers.Read("PlatePositionSum");
        planet::PlanetGpuResult<std::vector<std::byte>> pullBytes = buffers.Read("PlatePull");
        std::optional<std::vector<int32_t>> expectedCells = reference.Ints("p6_cell_count");
        std::optional<std::vector<int32_t>> expectedContinental = reference.Ints("p6_continental_count");
        std::optional<std::vector<float>> expectedPositions = reference.Floats("p6_position_sum");
        std::optional<std::vector<float>> expectedPull = reference.Floats("p6_pull");

        if (!cellBytes.has_value() || !continentalBytes.has_value() || !positionBytes.has_value() || !pullBytes.has_value() ||
            !expectedCells.has_value() || !expectedContinental.has_value() || !expectedPositions.has_value() || !expectedPull.has_value())
        {
            return false;
        }

        const int64_t cellsWrong = CountDifferent(FromBytes<int32_t>(*cellBytes, plates), *expectedCells);
        const int64_t continentalWrong = CountDifferent(FromBytes<int32_t>(*continentalBytes, plates), *expectedContinental);
        const std::vector<int32_t> positions = FromBytes<int32_t>(*positionBytes, plates * 3u);
        const std::vector<int32_t> pull = FromBytes<int32_t>(*pullBytes, plates * 3u);
        double largestPosition = 0.0;
        double largestPull = 0.0;

        for (size_t plate = 0; plate < plates; ++plate)
        {
            largestPosition = std::max(largestPosition, LargestRowDifference(positions, k_PositionSumScale, *expectedPositions, plate));
            largestPull = std::max(largestPull, LargestRowDifference(pull, k_PullScale, *expectedPull, plate));
        }

        std::println("chain p6: cell counts {} wrong, continental counts {} wrong, position sums largest {:.3g}, pulls largest {:.3g}",
                     cellsWrong, continentalWrong, largestPosition, largestPull);
        passed = passed && cellsWrong == 0 && continentalWrong == 0 && largestPosition <= k_RelativeTolerance && largestPull <= k_RelativeTolerance;
        return passed;
    }

    [[nodiscard]] bool RunCheck(std::string_view name, GpuContext& gpu, const planet::ReferenceSet& reference, planet::CrustFlagChoice crust_flags)
    {
        if (name == "chain")
        {
            return CheckChain(gpu, reference, crust_flags);
        }

        if (name == "map")
        {
            return CheckMap(gpu, reference, crust_flags);
        }

        if (name == "owners")
        {
            return CheckOwners(gpu, reference, crust_flags);
        }

        if (name == "front")
        {
            return CheckFront(gpu, reference, crust_flags);
        }

        if (name == "crust")
        {
            return CheckCrust(gpu, reference, crust_flags);
        }

        if (name == "live")
        {
            return CheckLiveCells(gpu, reference, crust_flags);
        }

        if (name == "distance")
        {
            return CheckFrontDistance(gpu, reference, crust_flags);
        }

        if (name == "uplift")
        {
            return CheckUplift(gpu, reference, crust_flags);
        }

        if (name == "torque")
        {
            return CheckTorqueSums(gpu, reference, crust_flags);
        }

        std::println(stderr, "unknown check '{}'", name);
        return false;
    }
}

int main(int argc, char* argv[])
{
    std::filesystem::path referenceFolder;
    std::vector<std::string_view> checks;

    for (const char* argument : std::span{ argv, static_cast<size_t>(argc) }.subspan(1))
    {
        const std::string_view text{ argument };

        if (text.starts_with(k_ReferenceFlag))
        {
            referenceFolder = text.substr(k_ReferenceFlag.size());
        }
        else if (text.starts_with(k_ChecksFlag))
        {
            for (const auto part : std::views::split(text.substr(k_ChecksFlag.size()), ','))
            {
                checks.emplace_back(part.begin(), part.end());
            }
        }
    }

    if (checks.empty())
    {
        checks.assign(k_AllChecks.begin(), k_AllChecks.end());
    }

    if (referenceFolder.empty())
    {
        std::println(stderr, "usage: PlanetGenCheck --reference=<folder> [--checks=map,owners,front,crust,live,distance,uplift,torque,chain]");
        return 1;
    }

    std::optional<planet::ReferenceSet> reference = planet::ReferenceSet::Open(referenceFolder);

    if (!reference.has_value())
    {
        return 1;
    }

    // headless boot: no window and no modules
    bootstrap::LaunchStage launch;

    if (!Booted(launch.Start(std::span{ argv, static_cast<size_t>(argc) }, std::span<const bootstrap::ModuleDescriptor>{}, bootstrap::Stage::Gpu)))
    {
        return 1;
    }

    bootstrap::CoreStage core;

    if (!Booted(core.Start(launch)))
    {
        return 1;
    }

    bootstrap::ContentStage content;

    if (!Booted(content.Start(core, k_Bundles)))
    {
        return 1;
    }

    std::vector<bootstrap::Need> needs = bootstrap::HeadlessSpineNeeds();
    needs.append_range(bootstrap::ShaderObjectNeeds());
    const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(content, needs);
    bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::Full,
        .ApplicationName = "PlanetGenCheck",
    };

    // --config=PATH can decline Wanted needs, for example "StorageBuffer8BitAccess" to run the word variant only
    if (!Booted(bootstrap::ReadDeviceConfig(launch, config)))
    {
        return 1;
    }

    bootstrap::DeviceStage deviceStage;

    if (!Booted(deviceStage.StartHeadless(content, requirements, config)))
    {
        return 1;
    }

    bootstrap::GpuStage gpuStage;

    if (!Booted(gpuStage.StartHeadless(deviceStage)))
    {
        return 1;
    }

    rhi::Device& device = *deviceStage.Rhi().GetDevice();
    // the content stage found the bundle, so the library opens that same file
    const std::filesystem::path& bundlePath = content.Bundles().front().Path;
    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(bundlePath, rhi::k_SpirvProfileName);

    if (!library.has_value())
    {
        std::println(stderr, "Failed to open {}: {}", bundlePath.string(), rhi::ToString(library.error()));
        return 1;
    }

    std::println("PlanetGenCheck: {} points, {} plates, time {} My", reference->PointCount(), reference->PlateCount(), reference->Time());
    bool passed = true;

    {
        rhi::CommandPool pool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
        RhiAssert(pool.AllocateCommandBuffers(1));
        GpuContext gpu{ device, gpuStage.Resources(), **library, pool, deviceStage.Filter() };
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> preferred = CreateShaders(gpu, planet::CrustFlagChoice::BestForDevice, k_AllEntryPoints);

        if (!preferred.has_value())
        {
            std::println(stderr, "the device runs no variant of {}", planet::k_PlanetModuleName);
            return 1;
        }

        std::println("the device prefers crust flags in {}", planet::ToString((*preferred)->CrustFlags()));

        for (const planet::CrustFlagChoice storage : k_FlagStorages)
        {
            const std::string_view storageName = storage == planet::CrustFlagChoice::Bytes ? "bytes" : "words";
            planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> probe = CreateShaders(gpu, storage, k_AllEntryPoints);

            // a device without 8-bit storage runs the word variant only; that is not a failure
            if (!probe.has_value() && probe.error() == planet::PlanetGpuError::NoVariantFitsDevice)
            {
                std::println("crust flags in {}: SKIP, the device cannot run this variant", storageName);
                continue;
            }

            std::println("crust flags in {}:", storageName);

            for (const std::string_view check : checks)
            {
                const bool checkPassed = RunCheck(check, gpu, *reference, storage);
                std::println("{} [{}]: {}", check, storageName, checkPassed ? "PASS" : "FAIL");
                passed = passed && checkPassed;
            }
        }
    }

    return passed ? 0 : 1;
}
