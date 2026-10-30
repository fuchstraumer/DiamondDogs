#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "GpuTimers.hpp"
#include "OceanSimulation.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "SeaState.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "OceanCdlod.hpp"
#include "ExrLoader.hpp"
#include "SourcePaths.hpp"
#include "OceanAtmosphere.hpp"
#include "ManifestDescriptors.hpp"
#include "ImageDataFormats.hpp"
#include "ResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "ResourceMessageReply.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Runs the OceanFft compute chain headless, through the same ocean::OceanSimulation the OceanFft application uses,
// and checks it against double precision on the CPU:
//  1. The radix-4 IFFT kernel alone, with and without wave ops, at 256 (even log2) and 512 (odd log2, so the
//     radix-2 tail stage runs). Random half input, compared with a CPU 2D inverse FFT of the same quantized values.
//  2. SpectrumConjugateCS: conj(h0(-k)), bit exact.
//  3. Tick, IFFT and resolve: the CPU repeats the tick and the transform from the GPU's own h0(k) and omega,
//     and every field of the Displacement and Derivatives textures is compared with it. The default settings run
//     the fused kernels, and one more run uses the unfused passes.
//  4. The fused kernels (OceanTickRowsCS, OceanColumnsResolveCS) against the unfused passes, from the same spectrum
//     and time: Displacement, Derivatives and Whitecaps (mip 0) must be bit identical.
// The GPU works in half precision, so the comparisons use the relative RMS error over each field.

namespace
{
    using Complex = std::complex<double>;
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    // half has 11 bits of mantissa, and every IFFT stage rounds once more
    constexpr double k_IfftTolerance = 1.0e-2;
    constexpr double k_PipelineTolerance = 2.0e-2;
    // a mip texel is one more half rounding of four halves
    constexpr double k_MipTolerance = 2.0e-3;

    struct TestContext
    {
        rhi::Device& Device;
        ResourceContext& Resources;
        const rhi::ShaderLibrary& Library;
        uint32_t WaveSize{ 0u };
    };

    // records, submits on the general queue, and waits
    [[nodiscard]] bool Submit(rhi::Device& device, const std::function<void(rhi::CommandBuffer&)>& record)
    {
        rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
        RhiAssert(commandPool.AllocateCommandBuffers(1));
        rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
        RhiAssert(commandBuffer.Begin());
        record(commandBuffer);
        RhiAssert(commandBuffer.End());
        const rhi::CommandBufferHandle handle = commandBuffer.Handle();
        rhi::Fence fence(device.Handle());
        RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()));
        return fence.Wait();
    }

    /** @brief A host-readable buffer that copies land in, read back as halves */
    class Readback
    {
    public:
        Readback(ResourceContext& resources, uint64_t bytes) : resources{ resources }, bytes{ bytes }
        {
            BufferCreateInfo info{};
            info.Domain = ResourceDomain::GPUToCPU;
            info.Usage = BufferUsageBits::CopyDestination;
            info.Size = bytes;
            std::shared_ptr<GraphicsResourceReply> reply = resources.CreateBuffer(info, {});
            if (reply->WaitForCompletion(k_TimeoutNs) == MessageReply::Status::Completed)
            {
                buffer = reply->GetResource();
            }
        }
        ~Readback()
        {
            if (buffer)
            {
                (void)resources.DestroyResource(buffer)->WaitForCompletion(k_TimeoutNs);
            }
        }
        [[nodiscard]] bool Valid() const noexcept { return static_cast<bool>(buffer); }
        [[nodiscard]] rhi::BufferHandle Handle() const noexcept { return rhi::BufferHandle{ buffer.VkHandle }; }

        [[nodiscard]] std::vector<float> ReadHalves(uint64_t offset, uint64_t count) const
        {
            std::vector<float> values;
            std::shared_ptr<PointerMessageReply> map = resources.MapBuffer(buffer, bytes, 0u);
            if (map->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                return values;
            }
            const uint16_t* halves = reinterpret_cast<const uint16_t*>(static_cast<const std::byte*>(map->GetPointer()) + offset);
            values.reserve(count);
            for (uint64_t i = 0; i < count; ++i)
            {
                values.push_back(ocean::HalfToFloat(halves[i]));
            }
            (void)resources.UnmapBuffer(buffer, bytes, 0u)->WaitForCompletion(k_TimeoutNs);
            return values;
        }

        [[nodiscard]] std::vector<uint16_t> ReadRaw(uint64_t offset, uint64_t count) const
        {
            std::vector<uint16_t> values(count);
            std::shared_ptr<PointerMessageReply> map = resources.MapBuffer(buffer, bytes, 0u);
            if (map->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                return {};
            }
            std::memcpy(values.data(), static_cast<const std::byte*>(map->GetPointer()) + offset, count * sizeof(uint16_t));
            (void)resources.UnmapBuffer(buffer, bytes, 0u)->WaitForCompletion(k_TimeoutNs);
            return values;
        }

        [[nodiscard]] std::vector<uint32_t> ReadWords(uint64_t offset, uint64_t count) const
        {
            std::vector<uint32_t> values(count);
            std::shared_ptr<PointerMessageReply> map = resources.MapBuffer(buffer, bytes, 0u);
            if (map->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                return {};
            }
            std::memcpy(values.data(), static_cast<const std::byte*>(map->GetPointer()) + offset, count * sizeof(uint32_t));
            (void)resources.UnmapBuffer(buffer, bytes, 0u)->WaitForCompletion(k_TimeoutNs);
            return values;
        }

    private:
        ResourceContext& resources;
        uint64_t bytes{ 0u };
        GraphicsResource buffer;
    };

    void ComputeToCopyBarrier(rhi::CommandBuffer& command_buffer)
    {
        // a tick writes every level of the textures with compute
        const rhi::GlobalBarrier barrier{ ocean::OceanSimulation::k_TickStages, ocean::OceanSimulation::k_TickWriteAccess, rhi::PipelineStage::Copy, rhi::Access::TransferRead };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &barrier, 1 }, {}, {} });
    }

    void CopyToHostBarrier(rhi::CommandBuffer& command_buffer)
    {
        const rhi::GlobalBarrier barrier{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
        command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &barrier, 1 }, {}, {} });
    }

    // in place, unnormalized, with e^(+i): the inverse transform without the 1/N
    void InverseFft(std::span<Complex> values)
    {
        const size_t n = values.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1u;
            for (; (j & bit) != 0u; bit >>= 1u)
            {
                j ^= bit;
            }
            j ^= bit;
            if (i < j)
            {
                std::swap(values[i], values[j]);
            }
        }
        for (size_t length = 2; length <= n; length <<= 1u)
        {
            const Complex step = std::polar(1.0, 2.0 * std::numbers::pi / static_cast<double>(length));
            for (size_t start = 0; start < n; start += length)
            {
                Complex twiddle{ 1.0, 0.0 };
                for (size_t k = 0; k < length / 2u; ++k)
                {
                    const Complex even = values[start + k];
                    const Complex odd = values[start + k + length / 2u] * twiddle;
                    values[start + k] = even + odd;
                    values[start + k + length / 2u] = even - odd;
                    twiddle *= step;
                }
            }
        }
    }

    // rows (contiguous x) then columns, in place on one size * size plane
    void InverseFft2D(std::span<Complex> plane, uint32_t size)
    {
        std::vector<Complex> column(size);
        for (uint32_t y = 0; y < size; ++y)
        {
            InverseFft(plane.subspan(size_t{ y } * size, size));
        }
        for (uint32_t x = 0; x < size; ++x)
        {
            for (uint32_t y = 0; y < size; ++y)
            {
                column[y] = plane[size_t{ y } * size + x];
            }
            InverseFft(column);
            for (uint32_t y = 0; y < size; ++y)
            {
                plane[size_t{ y } * size + x] = column[y];
            }
        }
    }

    struct ErrorStats
    {
        double RelativeRms{ 0.0 };
        double ReferenceRms{ 0.0 };
        bool Finite{ true };
    };

    [[nodiscard]] ErrorStats Compare(std::span<const double> gpu, std::span<const double> reference)
    {
        double errorSquared = 0.0;
        double referenceSquared = 0.0;
        ErrorStats stats{};
        for (size_t i = 0; i < gpu.size(); ++i)
        {
            if (!std::isfinite(gpu[i]))
            {
                stats.Finite = false;
            }
            const double error = gpu[i] - reference[i];
            errorSquared += error * error;
            referenceSquared += reference[i] * reference[i];
        }
        stats.ReferenceRms = std::sqrt(referenceSquared / static_cast<double>(gpu.size()));
        stats.RelativeRms = referenceSquared > 0.0 ? std::sqrt(errorSquared / referenceSquared) : std::sqrt(errorSquared);
        return stats;
    }

    [[nodiscard]] std::string Describe(const ocean::OceanSimulationSettings& settings)
    {
        constexpr std::array<std::string_view, 3> k_SpreadingNames{ "Mitsuyasu", "Hasselmann", "Donelan-Banner" };
        const uint32_t spreading = std::min(settings.Spectrum.DirectionalSpreadingMode, 2u);
        return std::format("size {} {} {} {}", settings.Size,
            settings.WaveSize == 0u ? std::string{ "no wave ops" } : std::format("wave {}", settings.WaveSize), k_SpreadingNames[spreading],
            settings.FusedKernels ? "fused" : "unfused");
    }

    int32_t TestIfft(const TestContext& context, const ocean::OceanSimulationSettings& settings)
    {
        const std::string name = Describe(settings);
        auto simulation = ocean::OceanSimulation::Create(context.Device, context.Resources, context.Library, settings);
        if (!simulation.has_value())
        {
            std::cerr << std::format("[IFFT {}] {}\n", name, simulation.error());
            return 1;
        }
        const ocean::OceanSimulation& ocean = **simulation;
        const uint32_t size = settings.Size;
        const uint64_t planeElements = uint64_t{ size } * size;
        const uint64_t slices = uint64_t{ ocean::k_CascadeCount } * 2u;
        const uint64_t inputBytes = ocean.BufferBytes(ocean::OceanSimulation::Buffer::IfftInput);

        // random complex input in [-1, 1], quantized to half so the CPU transforms exactly what the GPU reads
        std::mt19937 generator{ 0x0CEA4u + size };
        std::uniform_real_distribution<float> distribution{ -1.0f, 1.0f };
        std::vector<uint16_t> input(planeElements * slices * 4u);
        for (uint16_t& value : input)
        {
            value = ocean::FloatToHalf(distribution(generator));
        }
        const RhiBufferResourceData inputData{ std::as_bytes(std::span{ input }), 0u };
        std::shared_ptr<ResourceTransferReply> upload = context.Resources.SetBufferData(ocean.GetBuffer(ocean::OceanSimulation::Buffer::IfftInput), std::span{ &inputData, 1 });
        if (upload->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << std::format("[IFFT {}] the input upload failed\n", name);
            return 1;
        }

        Readback readback(context.Resources, inputBytes);
        const bool finished = readback.Valid() && Submit(context.Device, [&](rhi::CommandBuffer& command_buffer)
        {
            // the upload ran on the transfer queue and finished before this submit
            ocean.RecordIfft(command_buffer);
            ComputeToCopyBarrier(command_buffer);
            const rhi::BufferCopy region{ 0u, 0u, inputBytes };
            command_buffer.CopyBuffer(rhi::BufferHandle{ ocean.GetBuffer(ocean::OceanSimulation::Buffer::IfftInput).VkHandle }, readback.Handle(), std::span{ &region, 1 });
            CopyToHostBarrier(command_buffer);
        });
        if (!finished)
        {
            std::cerr << std::format("[IFFT {}] the dispatch did not finish\n", name);
            return 1;
        }
        const std::vector<float> output = readback.ReadHalves(0u, planeElements * slices * 4u);

        // every slice holds two complex planes, interleaved as (re0, im0, re1, im1)
        const double scale = 1.0 / static_cast<double>(planeElements);
        std::vector<double> gpuValues;
        std::vector<double> referenceValues;
        gpuValues.reserve(output.size());
        referenceValues.reserve(output.size());
        std::vector<Complex> plane(planeElements);
        for (uint64_t slice = 0; slice < slices; ++slice)
        {
            for (uint32_t pair = 0; pair < 2u; ++pair)
            {
                const uint64_t base = slice * planeElements * 4u;
                for (uint64_t i = 0; i < planeElements; ++i)
                {
                    plane[i] = Complex{ ocean::HalfToFloat(input[base + i * 4u + pair * 2u]), ocean::HalfToFloat(input[base + i * 4u + pair * 2u + 1u]) };
                }
                InverseFft2D(plane, size);
                for (uint64_t i = 0; i < planeElements; ++i)
                {
                    referenceValues.push_back(plane[i].real() * scale);
                    referenceValues.push_back(plane[i].imag() * scale);
                    gpuValues.push_back(output[base + i * 4u + pair * 2u]);
                    gpuValues.push_back(output[base + i * 4u + pair * 2u + 1u]);
                }
            }
        }

        const ErrorStats stats = Compare(gpuValues, referenceValues);
        const bool passed = stats.Finite && stats.RelativeRms < k_IfftTolerance;
        (passed ? std::cout : std::cerr) << std::format("[IFFT {}] relative RMS error {:.2e} (limit {:.0e}){}\n",
            name, stats.RelativeRms, k_IfftTolerance, stats.Finite ? "" : ", non-finite values");
        return passed ? 0 : 1;
    }

    // the distance in representable halves between two bit patterns
    [[nodiscard]] uint32_t HalfUlpDistance(uint16_t a, uint16_t b) noexcept
    {
        const auto ordered = [](uint16_t bits) -> int32_t
        {
            const int32_t magnitude = bits & 0x7FFF;
            return (bits & 0x8000) != 0 ? -magnitude : magnitude;
        };

        return static_cast<uint32_t>(std::abs(ordered(a) - ordered(b)));
    }

    int32_t TestFusedMatchesUnfused(const TestContext& context, const ocean::OceanSimulationSettings& settings, float time)
    {
        const std::string name = Describe(settings);
        ocean::OceanSimulationSettings fusedSettings = settings;
        fusedSettings.FusedKernels = true;
        auto simulation = ocean::OceanSimulation::Create(context.Device, context.Resources, context.Library, fusedSettings);
        if (!simulation.has_value())
        {
            std::cerr << std::format("[fused vs unfused {}] {}\n", name, simulation.error());
            return 1;
        }
        ocean::OceanSimulation& ocean = **simulation;
        const uint32_t size = settings.Size;
        const uint64_t spectrumElements = uint64_t{ size } * size * ocean::k_CascadeCount;

        // Displacement and Derivatives (rgba16f), then Whitecaps (rg16f): mip 0 of every layer
        const uint64_t textureBytes = spectrumElements * 8u;
        const uint64_t derivativesOffset = textureBytes;
        const uint64_t whitecapsOffset = textureBytes * 2u;
        const uint64_t readbackBytes = whitecapsOffset + spectrumElements * 4u;

        // the first run also computes the spectrum, and the second run reuses it: both ticks see the same h0(k) and time
        const auto run = [&](bool fused, bool initialize, const Readback& readback)
        {
            ocean.SetFusedKernels(fused);
            rhi::GpuTimers timers(context.Device, rhi::GpuTimersCreateInfo{ context.Device.GetQueueFamilyIndices().Graphics, 1u, 8u });

            return readback.Valid() && Submit(context.Device, [&](rhi::CommandBuffer& command_buffer)
            {
                rhi::GpuTimerBlock timerBlock(timers, command_buffer, 0u);

                if (initialize)
                {
                    ocean.RecordInitialize(command_buffer);
                }

                ocean.RecordTick(command_buffer, timerBlock, time);
                ComputeToCopyBarrier(command_buffer);

                const std::array<std::pair<GraphicsResource, uint64_t>, 3> images
                {
                    std::pair{ ocean.Displacement(), uint64_t{ 0u } },
                    std::pair{ ocean.Derivatives(), derivativesOffset },
                    std::pair{ ocean.Whitecaps(), whitecapsOffset }
                };

                for (const auto& [image, offset] : images)
                {
                    rhi::BufferImageCopy region{};
                    region.BufferOffset = offset;
                    region.LayerCount = ocean::k_CascadeCount;
                    region.Width = size;
                    region.Height = size;
                    command_buffer.CopyImageToBuffer(rhi::ImageHandle{ image.VkHandle }, rhi::ImageLayout::General, readback.Handle(), std::span{ &region, 1 });
                }

                CopyToHostBarrier(command_buffer);
            });
        };

        const Readback fusedReadback(context.Resources, readbackBytes);
        const Readback unfusedReadback(context.Resources, readbackBytes);

        if (!run(true, true, fusedReadback) || !run(false, false, unfusedReadback))
        {
            std::cerr << std::format("[fused vs unfused {}] the dispatches did not finish\n", name);
            return 1;
        }

        struct Field
        {
            std::string_view Name;
            uint64_t Offset;
            uint64_t Halves;
        };
        const std::array<Field, 3> fields
        {
            Field{ "Displacement", 0u, spectrumElements * 4u },
            Field{ "Derivatives", derivativesOffset, spectrumElements * 4u },
            Field{ "Whitecaps", whitecapsOffset, spectrumElements * 2u }
        };

        int32_t failures = 0;

        for (const Field& field : fields)
        {
            const std::vector<uint16_t> fusedValues = fusedReadback.ReadRaw(field.Offset, field.Halves);
            const std::vector<uint16_t> unfusedValues = unfusedReadback.ReadRaw(field.Offset, field.Halves);
            uint64_t differences = 0u;
            uint64_t nonZero = 0u;
            uint32_t maxUlp = 0u;

            for (uint64_t i = 0; i < field.Halves; ++i)
            {
                nonZero += (unfusedValues[i] & 0x7FFFu) != 0u ? 1u : 0u;

                if (fusedValues[i] != unfusedValues[i])
                {
                    ++differences;
                    maxUlp = std::max(maxUlp, HalfUlpDistance(fusedValues[i], unfusedValues[i]));
                }
            }

            // an all-zero output would match trivially
            const bool passed = differences == 0u && nonZero != 0u && fusedValues.size() == field.Halves && unfusedValues.size() == field.Halves;
            (passed ? std::cout : std::cerr) << std::format("[fused vs unfused {}] {:<12} {} of {} halves differ (max {} ulp), {} non-zero in the unfused output\n",
                name, field.Name, differences, field.Halves, maxUlp, nonZero);
            failures += passed ? 0 : 1;
        }

        return failures;
    }

    int32_t TestPipeline(const TestContext& context, const ocean::OceanSimulationSettings& settings, float time)
    {
        const std::string name = Describe(settings);
        auto simulation = ocean::OceanSimulation::Create(context.Device, context.Resources, context.Library, settings);
        if (!simulation.has_value())
        {
            std::cerr << std::format("[pipeline {}] {}\n", name, simulation.error());
            return 1;
        }
        const ocean::OceanSimulation& ocean = **simulation;
        using Buffer = ocean::OceanSimulation::Buffer;
        const uint32_t size = settings.Size;
        const uint64_t planeElements = uint64_t{ size } * size;
        const uint64_t spectrumElements = planeElements * ocean::k_CascadeCount;

        // one readback for everything: h0K, h0KConjugate, WaveData, then the two textures (rgba16f, every layer)
        const uint64_t h0Offset = 0u;
        const uint64_t conjugateOffset = h0Offset + ocean.BufferBytes(Buffer::H0K);
        const uint64_t waveOffset = conjugateOffset + ocean.BufferBytes(Buffer::H0KConjugate);
        const uint64_t displacementOffset = waveOffset + ocean.BufferBytes(Buffer::WaveData);
        const uint64_t textureBytes = spectrumElements * 8u;
        const uint64_t derivativesOffset = displacementOffset + textureBytes;
        // and every other mip of the displacement, which OceanMipsCS made from mip 0. Both of its dispatches write some
        const uint32_t mipLevels = ocean.MipLevels();
        std::vector<uint64_t> mipOffsets{ displacementOffset };
        uint64_t readbackBytes = derivativesOffset + textureBytes;

        for (uint32_t level = 1u; level < mipLevels; ++level)
        {
            const uint64_t levelSize = size >> level;
            mipOffsets.push_back(readbackBytes);
            readbackBytes += levelSize * levelSize * ocean::k_CascadeCount * 8u;
        }

        Readback readback(context.Resources, readbackBytes);
        // per-pass GPU times, for information only: one cold frame is noisy, so nothing is checked against them
        rhi::GpuTimers timers(context.Device, rhi::GpuTimersCreateInfo{ context.Device.GetQueueFamilyIndices().Graphics, 1u, 8u });

        const bool finished = readback.Valid() && Submit(context.Device, [&](rhi::CommandBuffer& command_buffer)
        {
            rhi::GpuTimerBlock timerBlock(timers, command_buffer, 0u);
            ocean.RecordInitialize(command_buffer);
            ocean.RecordTick(command_buffer, timerBlock, time);
            ComputeToCopyBarrier(command_buffer);
            for (const auto& [buffer, offset] : { std::pair{ Buffer::H0K, h0Offset }, std::pair{ Buffer::H0KConjugate, conjugateOffset }, std::pair{ Buffer::WaveData, waveOffset } })
            {
                const rhi::BufferCopy region{ 0u, offset, ocean.BufferBytes(buffer) };
                command_buffer.CopyBuffer(rhi::BufferHandle{ ocean.GetBuffer(buffer).VkHandle }, readback.Handle(), std::span{ &region, 1 });
            }
            for (const auto& [image, offset] : { std::pair{ ocean.Displacement(), displacementOffset }, std::pair{ ocean.Derivatives(), derivativesOffset } })
            {
                rhi::BufferImageCopy region{};
                region.BufferOffset = offset;
                region.LayerCount = ocean::k_CascadeCount;
                region.Width = size;
                region.Height = size;
                command_buffer.CopyImageToBuffer(rhi::ImageHandle{ image.VkHandle }, rhi::ImageLayout::General, readback.Handle(), std::span{ &region, 1 });
            }

            for (uint32_t level = 1u; level < mipLevels; ++level)
            {
                rhi::BufferImageCopy mipRegion{};
                mipRegion.BufferOffset = mipOffsets[level];
                mipRegion.MipLevel = level;
                mipRegion.LayerCount = ocean::k_CascadeCount;
                mipRegion.Width = size >> level;
                mipRegion.Height = size >> level;
                command_buffer.CopyImageToBuffer(rhi::ImageHandle{ ocean.Displacement().VkHandle }, rhi::ImageLayout::General, readback.Handle(), std::span{ &mipRegion, 1 });
            }

            CopyToHostBarrier(command_buffer);
        });
        if (!finished)
        {
            std::cerr << std::format("[pipeline {}] the dispatches did not finish\n", name);
            return 1;
        }

        int32_t failures = 0;
        // Submit waited on the fence, so submission 0 finished
        timers.Collect(1u);

        if (const std::optional<rhi::GpuTimingBlock> timings = timers.Latest(); timings.has_value())
        {
            std::string line = std::format("[pipeline {}] GPU", name);

            for (const rhi::GpuTiming& timing : timings->Timings)
            {
                line += std::format(" | {} {:.1f} us", timers.Name(timing.Hash), timing.Nanoseconds * 1e-3);
            }

            std::cout << line << "\n";
        }

        // 2. the conjugate pass only flips one sign bit of the mirrored element, so it must match exactly
        const std::vector<uint16_t> h0Raw = readback.ReadRaw(h0Offset, spectrumElements * 2u);
        const std::vector<uint16_t> conjugateRaw = readback.ReadRaw(conjugateOffset, spectrumElements * 2u);
        uint64_t conjugateMismatches = 0u;
        uint64_t nonZeroModes = 0u;
        for (uint32_t cascade = 0; cascade < ocean::k_CascadeCount; ++cascade)
        {
            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const uint64_t index = (uint64_t{ cascade } * size + y) * size + x;
                    const uint64_t mirrored = (uint64_t{ cascade } * size + (size - y) % size) * size + (size - x) % size;
                    const uint16_t expectedReal = h0Raw[mirrored * 2u];
                    const uint16_t expectedImaginary = static_cast<uint16_t>(h0Raw[mirrored * 2u + 1u] ^ 0x8000u);
                    if (conjugateRaw[index * 2u] != expectedReal || conjugateRaw[index * 2u + 1u] != expectedImaginary)
                    {
                        ++conjugateMismatches;
                    }
                    if ((h0Raw[index * 2u] & 0x7FFFu) != 0u || (h0Raw[index * 2u + 1u] & 0x7FFFu) != 0u)
                    {
                        ++nonZeroModes;
                    }
                }
            }
        }
        if (conjugateMismatches != 0u)
        {
            std::cerr << std::format("[pipeline {}] SpectrumConjugateCS: {} of {} elements differ from conj(h0(-k))\n", name, conjugateMismatches, spectrumElements);
            ++failures;
        }
        if (nonZeroModes == 0u)
        {
            std::cerr << std::format("[pipeline {}] SpectrumInitCS wrote an empty spectrum\n", name);
            ++failures;
        }

        // 3. the tick and the transform in double precision, from the GPU's inputs
        const std::vector<float> h0 = readback.ReadHalves(h0Offset, spectrumElements * 2u);
        const std::vector<float> h0Conjugate = readback.ReadHalves(conjugateOffset, spectrumElements * 2u);
        const std::vector<float> omegas = readback.ReadHalves(waveOffset, spectrumElements);
        const std::vector<float> displacement = readback.ReadHalves(displacementOffset, spectrumElements * 4u);
        const std::vector<float> derivatives = readback.ReadHalves(derivativesOffset, spectrumElements * 4u);

        constexpr std::array<std::string_view, 8> k_FieldNames{ "Dx", "height", "Dz", "jacobian", "dHeight/dx", "dHeight/dz", "dDx/dx", "dDz/dz" };
        std::array<std::vector<double>, 8> gpuFields;
        std::array<std::vector<double>, 8> referenceFields;
        const double choppiness = settings.Choppiness;
        std::array<std::vector<Complex>, 8> planes;
        for (std::vector<Complex>& plane : planes)
        {
            plane.resize(planeElements);
        }
        const Complex i{ 0.0, 1.0 };
        for (uint32_t cascade = 0; cascade < ocean::k_CascadeCount; ++cascade)
        {
            // the eight spectra of OceanTickCS: D = -i (k / |k|) h, and a derivative along x is a multiply by i k.x
            for (uint64_t element = 0; element < planeElements; ++element)
            {
                const uint64_t index = cascade * planeElements + element;
                // k follows from the thread position and the cascade, as WaveSpacing computes it on the GPU
                const double waveSpacing = 2.0 * std::numbers::pi / static_cast<double>(settings.LengthScales[cascade]);
                const int64_t offsetX = static_cast<int64_t>(element % size) - static_cast<int64_t>(size / 2u);
                const int64_t offsetY = static_cast<int64_t>(element / size) - static_cast<int64_t>(size / 2u);
                const double kx = waveSpacing * static_cast<double>(offsetX);
                const double kz = waveSpacing * static_cast<double>(offsetY);
                const double omega = omegas[index];
                const Complex phase = std::polar(1.0, omega * static_cast<double>(time));
                const Complex h = Complex{ h0[index * 2u], h0[index * 2u + 1u] } * phase + Complex{ h0Conjugate[index * 2u], h0Conjugate[index * 2u + 1u] } * std::conj(phase);
                const double kLength = std::hypot(kx, kz);
                const double inverseKLength = kLength > 1e-6 ? 1.0 / kLength : 0.0;
                planes[0][element] = i * h * (kx * inverseKLength);           // Dx
                planes[1][element] = h;                                       // height
                planes[2][element] = i * h * (kz * inverseKLength);           // Dz
                planes[4][element] = i * h * kx;                              // dHeight/dx
                planes[5][element] = i * h * kz;                              // dHeight/dz
                planes[6][element] = -h * (kx * kx * inverseKLength);         // dDx/dx
                planes[7][element] = -h * (kz * kz * inverseKLength);         // dDz/dz
                planes[3][element] = -h * (kx * kz * inverseKLength);         // dDx/dz, for the Jacobian
            }
            for (std::vector<Complex>& plane : planes)
            {
                InverseFft2D(plane, size);
            }
            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const uint64_t element = uint64_t{ y } * size + x;
                    const uint64_t texel = cascade * planeElements + element;
                    // the spectrum is centered at size / 2, which multiplies the transform by (-1)^(x + y)
                    const double sign = ((x + y) & 1u) == 0u ? 1.0 : -1.0;
                    const double dDxDx = sign * planes[6][element].real() * choppiness;
                    const double dDzDz = sign * planes[7][element].real() * choppiness;
                    const double dDxDz = sign * planes[3][element].real() * choppiness;
                    const std::array<double, 8> reference
                    {
                        sign * planes[0][element].real() * choppiness,
                        sign * planes[1][element].real(),
                        sign * planes[2][element].real() * choppiness,
                        (1.0 + dDxDx) * (1.0 + dDzDz) - dDxDz * dDxDz,
                        sign * planes[4][element].real(),
                        sign * planes[5][element].real(),
                        dDxDx,
                        dDzDz,
                    };
                    for (size_t field = 0; field < 4u; ++field)
                    {
                        gpuFields[field].push_back(displacement[texel * 4u + field]);
                        gpuFields[field + 4u].push_back(derivatives[texel * 4u + field]);
                    }
                    for (size_t field = 0; field < reference.size(); ++field)
                    {
                        referenceFields[field].push_back(reference[field]);
                    }
                }
            }
        }

        for (size_t field = 0; field < k_FieldNames.size(); ++field)
        {
            const ErrorStats stats = Compare(gpuFields[field], referenceFields[field]);
            const bool passed = stats.Finite && stats.RelativeRms < k_PipelineTolerance;
            (passed ? std::cout : std::cerr) << std::format("[pipeline {}] {:<11} RMS {:.3e}, relative RMS error {:.2e} (limit {:.0e}){}\n",
                name, k_FieldNames[field], stats.ReferenceRms, stats.RelativeRms, k_PipelineTolerance, stats.Finite ? "" : ", non-finite values");
            failures += passed ? 0 : 1;
        }

        // 4. every mip texel is the average of the 2x2 block above it, so each level must be that average of the level
        // the GPU wrote above it. The worst level is reported
        ErrorStats worstMip{};
        uint32_t worstMipLevel = 0u;
        bool mipsPassed = true;
        std::vector<float> above = displacement;

        for (uint32_t level = 1u; level < mipLevels; ++level)
        {
            const uint32_t aboveSize = size >> (level - 1u);
            const uint32_t levelSize = size >> level;
            const uint64_t levelElements = uint64_t{ levelSize } * levelSize * ocean::k_CascadeCount;
            const std::vector<float> mip = readback.ReadHalves(mipOffsets[level], levelElements * 4u);
            std::vector<double> gpuMip;
            std::vector<double> referenceMip;
            gpuMip.reserve(levelElements * 4u);
            referenceMip.reserve(levelElements * 4u);

            for (uint32_t cascade = 0; cascade < ocean::k_CascadeCount; ++cascade)
            {
                for (uint32_t y = 0; y < levelSize; ++y)
                {
                    for (uint32_t x = 0; x < levelSize; ++x)
                    {
                        const uint64_t mipTexel = (uint64_t{ cascade } * levelSize + y) * levelSize + x;
                        const uint64_t texel = (uint64_t{ cascade } * aboveSize + 2u * y) * aboveSize + 2u * x;

                        for (uint64_t channel = 0; channel < 4u; ++channel)
                        {
                            const double average = 0.25 * (double{ above[texel * 4u + channel] } + above[(texel + 1u) * 4u + channel] +
                                above[(texel + aboveSize) * 4u + channel] + above[(texel + aboveSize + 1u) * 4u + channel]);
                            gpuMip.push_back(mip[mipTexel * 4u + channel]);
                            referenceMip.push_back(average);
                        }
                    }
                }
            }

            const ErrorStats stats = Compare(gpuMip, referenceMip);
            const bool passed = stats.Finite && stats.RelativeRms < k_MipTolerance;

            if (!passed || stats.RelativeRms >= worstMip.RelativeRms)
            {
                worstMip = stats;
                worstMipLevel = level;
            }

            mipsPassed = mipsPassed && passed;
            above = mip;
        }

        (mipsPassed ? std::cout : std::cerr) << std::format("[pipeline {}] displacement mips 1 to {} against the 2x2 average of the level above: worst relative RMS error {:.2e} at mip {} (limit {:.0e}){}\n",
            name, mipLevels - 1u, worstMip.RelativeRms, worstMipLevel, k_MipTolerance, worstMip.Finite ? "" : ", non-finite values");
        failures += mipsPassed ? 0 : 1;

        // significant wave height of all cascades together, as a plausibility number for the spectrum
        double heightSquared = 0.0;
        for (uint64_t element = 0; element < planeElements; ++element)
        {
            double height = 0.0;
            for (uint32_t cascade = 0; cascade < ocean::k_CascadeCount; ++cascade)
            {
                height += gpuFields[1][cascade * planeElements + element];
            }
            heightSquared += height * height;
        }

        // a fully developed sea has Hs = 0.21 U^2 / g. Fetch-limited JONSWAP (100 km at 10 m/s) gives about 0.75 of
        // that, so the band takes both, and catches a factor of 2 in amplitude either way
        const double significantHeight = 4.0 * std::sqrt(heightSquared / static_cast<double>(planeElements));
        const double fullyDeveloped = 0.21 * settings.Spectrum.WindSpeed * settings.Spectrum.WindSpeed / 9.81;
        const double heightRatio = significantHeight / fullyDeveloped;
        // the band holds for the wind sea alone: a swell adds its own energy
        const bool heightPassed = settings.Spectrum.SwellScale > 0.0f || (heightRatio > 0.55 && heightRatio < 1.35);
        (heightPassed ? std::cout : std::cerr) << std::format("[pipeline {}] significant wave height {:.2f} m at {} m/s wind, {:.2f} of fully developed (limits 0.55 to 1.35)\n",
            name, significantHeight, settings.Spectrum.WindSpeed, heightRatio);
        failures += heightPassed ? 0 : 1;

        // the editor's readout integrates the same spectrum on the CPU: it must agree with what the GPU made
        const double readoutHeight = ocean::MeasureSpectrum(settings.Spectrum).SignificantHeight;
        const double readoutRatio = significantHeight / readoutHeight;
        const bool readoutPassed = readoutRatio > 0.9 && readoutRatio < 1.1;
        (readoutPassed ? std::cout : std::cerr) << std::format("[pipeline {}] sea state readout {:.2f} m, GPU {:.2f} m (ratio {:.3f}, limits 0.9 to 1.1)\n",
            name, readoutHeight, significantHeight, readoutRatio);
        failures += readoutPassed ? 0 : 1;

        return failures;
    }

    [[nodiscard]] int32_t TestComputeCdlodMaxLevel()
    {
        int32_t failures = 0;
        const auto check = [&](bool condition, std::string_view desc)
        {
            if (!condition)
            {
                std::cerr << std::format("[ComputeCdlodMaxLevel] FAIL: {}\n", desc);
                ++failures;
            }
        };

        const float worldExtent = 4096.0f;
        const uint32_t baseGridCount = 64u;
        const uint32_t fftSize = 512u;

        // Test active cascade lengths from default ocean simulation settings
        const std::array<float, ocean::k_CascadeCount> cascades{ 1024.0f, 238.0f, 55.4f, 12.9f, 3.0f };
        for (size_t i = 0; i < cascades.size(); ++i)
        {
            const uint32_t level = ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, fftSize, cascades[i], 9u);
            check(level >= 1u && level <= 9u, std::format("cascade {} (length {} m) level {} outside [1, 9]", i, cascades[i], level));
        }

        // Expected levels for known inputs:
        // cascade 0 (1024m): ratio = (4096*512)/(64*1024) = 32 -> log2(32) = 5
        const uint32_t level1024 = ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, fftSize, 1024.0f, 9u);
        check(level1024 == 5u, std::format("level for 1024m expected 5, got {}", level1024));

        // cascade 4 (3.0m): ratio = 2097152 / 192 = 10922.66 -> ceil(log2) = 14 -> clamped to 9
        const uint32_t level3 = ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, fftSize, 3.0f, 9u);
        check(level3 == 9u, std::format("level for 3.0m expected 9, got {}", level3));

        // Edge cases
        check(ocean::ComputeCdlodMaxLevel(0.0f, baseGridCount, fftSize, 3.0f, 9u) == 9u, "zero world extent clamped to 9u");
        check(ocean::ComputeCdlodMaxLevel(worldExtent, 0u, fftSize, 3.0f, 9u) == 9u, "zero base grid count clamped to 9u");
        check(ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, 0u, 3.0f, 9u) == 9u, "zero fft size clamped to 9u");
        check(ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, fftSize, -1.0f, 9u) == 9u, "negative cascade length clamped to 9u");

        // Small ratio <= 1.0f returns 1u
        check(ocean::ComputeCdlodMaxLevel(100.0f, 64u, 64u, 200.0f, 9u) == 1u, "ratio <= 1.0f expected 1u");

        // Custom max allowed level clamping
        check(ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, fftSize, 3.0f, 6u) == 6u, "custom max 6u clamp");

        // Default cap (11u) verification
        check(ocean::ComputeCdlodMaxLevel(worldExtent, baseGridCount, fftSize, 3.0f) == 13u, "default max allowed level is 13u");
        check(ocean::ComputeCdlodMaxLevel(0.0f, baseGridCount, fftSize, 3.0f) == 13u, "zero world extent clamped to 13u default");

        if (failures == 0)
        {
            std::cout << "[ComputeCdlodMaxLevel] PASS: all cascade levels clamped correctly within [1, 9]\n";
        }
        return failures;
    }

    [[nodiscard]] int32_t TestShaderVariants(const TestContext& context)
    {
        int32_t failures = 0;
        const auto check = [&](bool condition, std::string_view desc)
        {
            if (!condition)
            {
                std::cerr << std::format("[ShaderVariants] FAIL: {}\n", desc);
                ++failures;
            }
        };

        // 1. Module "Quadtree"
        auto quadtreeIndexResult = context.Library.ModuleIndex("Quadtree");
        if (!quadtreeIndexResult.has_value())
        {
            std::cerr << std::format("[ShaderVariants] FAIL: Module Quadtree: {}\n", rhi::ToString(quadtreeIndexResult.error()));
            return 1;
        }
        const lodestone::ManifestIndex* quadtreeIndex = *quadtreeIndexResult;
        const std::array<std::string_view, 1> updateEntry{ "QuadtreeUpdateCS" };
        const std::array<std::string_view, 1> buildEntry{ "QuadtreeBuildIndirectArgsCS" };

        // Quadtree scalar variant (QUADTREE_USE_WAVE_OPS = false)
        auto quadScalarQuery = quadtreeIndex->Query().Where("QUADTREE_USE_WAVE_OPS", false);
        auto varUpdateScalar = quadScalarQuery.SingleFor(updateEntry);
        check(varUpdateScalar.has_value(), "QuadtreeUpdateCS resolves with QUADTREE_USE_WAVE_OPS=false");
        auto varBuildScalar = quadScalarQuery.SingleFor(buildEntry);
        check(varBuildScalar.has_value(), "QuadtreeBuildIndirectArgsCS resolves with QUADTREE_USE_WAVE_OPS=false");

        if (varUpdateScalar.has_value())
        {
            auto prog = context.Library.CreateProgram(context.Device.Handle(), "Quadtree", *varUpdateScalar, updateEntry);
            check(prog.has_value(), "CreateProgram QuadtreeUpdateCS (scalar) succeeded");
        }
        if (varBuildScalar.has_value())
        {
            auto prog = context.Library.CreateProgram(context.Device.Handle(), "Quadtree", *varBuildScalar, buildEntry);
            check(prog.has_value(), "CreateProgram QuadtreeBuildIndirectArgsCS (scalar) succeeded");
        }

        // Quadtree wave variant (QUADTREE_USE_WAVE_OPS = true, wave size 32)
        auto quadWaveQuery = quadtreeIndex->Query()
            .Where("QUADTREE_USE_WAVE_OPS", true)
            .Where("QUADTREE_WAVE_SIZE", 32u);
        auto varUpdateWave = quadWaveQuery.SingleFor(updateEntry);
        check(varUpdateWave.has_value(), "QuadtreeUpdateCS resolves with QUADTREE_USE_WAVE_OPS=true");
        auto varBuildWave = quadWaveQuery.SingleFor(buildEntry);
        check(varBuildWave.has_value(), "QuadtreeBuildIndirectArgsCS resolves with QUADTREE_USE_WAVE_OPS=true");

        // 2. Module "OceanRender"
        auto renderIndexResult = context.Library.ModuleIndex("OceanRender");
        if (!renderIndexResult.has_value())
        {
            std::cerr << std::format("[ShaderVariants] FAIL: Module OceanRender: {}\n", rhi::ToString(renderIndexResult.error()));
            return failures + 1;
        }
        const lodestone::ManifestIndex* renderIndex = *renderIndexResult;
        constexpr std::array<std::string_view, 2> surfaceEntry{ "OceanSurfaceVS", "OceanSurfaceFS" };
        constexpr std::array<std::string_view, 2> seabedEntry{ "SeabedVS", "SeabedFS" };

        // OceanRender scalar variant (OCEAN_USE_WAVE_OPS = false)
        auto renderScalarQuery = renderIndex->Query().Where("OCEAN_USE_WAVE_OPS", false);
        auto varSurfaceScalar = renderScalarQuery.SingleFor(surfaceEntry);
        check(varSurfaceScalar.has_value(), "OceanRender surface resolves with OCEAN_USE_WAVE_OPS=false");
        auto varSeabedScalar = renderScalarQuery.SingleFor(seabedEntry);
        check(varSeabedScalar.has_value(), "OceanRender seabed resolves with OCEAN_USE_WAVE_OPS=false");

        if (varSurfaceScalar.has_value())
        {
            auto prog = context.Library.CreateProgram(context.Device.Handle(), "OceanRender", *varSurfaceScalar, surfaceEntry);
            check(prog.has_value(), "CreateProgram OceanSurface (scalar) succeeded");
        }
        if (varSeabedScalar.has_value())
        {
            auto prog = context.Library.CreateProgram(context.Device.Handle(), "OceanRender", *varSeabedScalar, seabedEntry);
            check(prog.has_value(), "CreateProgram Seabed (scalar) succeeded");
        }

        // OceanRender wave variant (OCEAN_USE_WAVE_OPS = true, wave size 32)
        auto renderWaveQuery = renderIndex->Query()
            .Where("OCEAN_USE_WAVE_OPS", true)
            .Where("OCEAN_WAVE_SIZE", 32u);
        auto varSurfaceWave = renderWaveQuery.SingleFor(surfaceEntry);
        check(varSurfaceWave.has_value(), "OceanRender surface resolves with OCEAN_USE_WAVE_OPS=true");
        auto varSeabedWave = renderWaveQuery.SingleFor(seabedEntry);
        check(varSeabedWave.has_value(), "OceanRender seabed resolves with OCEAN_USE_WAVE_OPS=true");

        if (failures == 0)
        {
            std::cout << "[ShaderVariants] PASS: Quadtree and OceanRender capability variants resolve cleanly via ShaderLibrary\n";
        }
        return failures;
    }

    [[nodiscard]] int32_t TestOceanCdlod(const TestContext& context)
    {
        int32_t failures = 0;
        const auto check = [&](bool condition, std::string_view desc)
        {
            if (!condition)
            {
                std::cerr << std::format("[OceanCdlod] FAIL: {}\n", desc);
                ++failures;
            }
        };

        ocean::OceanCdlodSettings settings{};
        settings.WorldExtent = 4096.0f;
        settings.MaxLevel = 9u;
        settings.BaseGridCount = 64u;
        settings.MaxSelectedPatches = 16384u;
        settings.MaxNodes = 131072u;

        auto cdlodResult = ocean::OceanCdlod::Create(context.Device, context.Resources, context.Library, 1u, settings);
        if (!cdlodResult.has_value())
        {
            std::cerr << std::format("[OceanCdlod] creation failed: {}\n", cdlodResult.error());
            return 1;
        }

        auto& cdlod = **cdlodResult;
        check(cdlod.GridVertexBuffer().VkHandle != 0u, "GridVertexBuffer is valid");
        check(cdlod.GridIndexBuffer().VkHandle != 0u, "GridIndexBuffer is valid");
        check(cdlod.SelectedPatchesBuffer().VkHandle != 0u, "SelectedPatchesBuffer is valid");
        check(cdlod.IndirectDrawBuffer().VkHandle != 0u, "IndirectDrawBuffer is valid");
        check(cdlod.IndexCount() == 64u * 64u * 6u, std::format("IndexCount expected 24576, got {}", cdlod.IndexCount()));

        ocean::OceanCdlodCameraState camera{};
        camera.CameraPosition = { 0.0f, 25.0f, -100.0f };
        for (int i = 0; i < 6; ++i)
        {
            camera.FrustumPlanes[i] = { 0.0f, 0.0f, 0.0f, 10000.0f };
        }

        const uint64_t indirectBytes = sizeof(VkDrawIndexedIndirectCommand);
        Readback readback(context.Resources, indirectBytes);

        const bool submitted = readback.Valid() && Submit(context.Device, [&](rhi::CommandBuffer& cmd)
        {
            cdlod.RecordUpdate(cmd, 0u, camera, nullptr);
            ComputeToCopyBarrier(cmd);
            const rhi::BufferCopy region{ 0u, 0u, indirectBytes };
            cmd.CopyBuffer(rhi::BufferHandle{ cdlod.IndirectDrawBuffer().VkHandle }, readback.Handle(), std::span{ &region, 1 });
            CopyToHostBarrier(cmd);
        });
        check(submitted, "RecordUpdate dispatch finished without GPU error");

        if (submitted)
        {
            const std::vector<uint32_t> args = readback.ReadWords(0u, 5u);
            check(args.size() == 5u, "readback args size is 5 words");
            if (args.size() == 5u)
            {
                check(args[0] == 64u * 64u * 6u, std::format("indirect indexCount expected 24576, got {}", args[0]));
                check(args[1] > 0u, std::format("indirect instanceCount (selected patches) expected > 0, got {}", args[1]));
            }
        }

        if (failures == 0)
        {
            std::cout << "[OceanCdlod] PASS: buffers allocated, quadtree updated, indirect args generated cleanly\n";
        }
        return failures;
    }

    [[nodiscard]] int32_t TestExrLoader()
    {
        int32_t failures = 0;
        const auto check = [&](bool condition, std::string_view desc)
        {
            if (!condition)
            {
                std::cerr << std::format("[ExrLoader] FAIL: {}\n", desc);
                ++failures;
            }
        };

        // 1. Math regression check: EvaluateShIrradiance
        // L0 isotropic test: sh[0] = { 2.0f, 3.0f, 4.0f, 1.0f }, rest 0
        ocean::SphericalHarmonicsL2 sh{};
        sh[0] = { 2.0f, 3.0f, 4.0f, 1.0f };
        constexpr float Y0 = 0.28209479f;
        const std::array<std::array<float, 3>, 4> testNormals{
            std::array<float, 3>{ 1.0f, 0.0f, 0.0f },
            std::array<float, 3>{ 0.0f, 1.0f, 0.0f },
            std::array<float, 3>{ 0.0f, 0.0f, 1.0f },
            std::array<float, 3>{ 0.57735f, 0.57735f, 0.57735f }
        };
        for (const auto& n : testNormals)
        {
            const auto irr = ocean::EvaluateShIrradiance(sh, n);
            check(std::abs(irr[0] - 2.0f * Y0) < 1e-4f, "SH L0 isotropic R match");
            check(std::abs(irr[1] - 3.0f * Y0) < 1e-4f, "SH L0 isotropic G match");
            check(std::abs(irr[2] - 4.0f * Y0) < 1e-4f, "SH L0 isotropic B match");
        }

        // L1 directional test: sh[1] (Y-aligned)
        ocean::SphericalHarmonicsL2 shY{};
        shY[0] = { 1.0f, 1.0f, 1.0f, 1.0f };
        shY[1] = { 2.0f, 2.0f, 2.0f, 1.0f };
        const auto irrUp = ocean::EvaluateShIrradiance(shY, { 0.0f, 1.0f, 0.0f });
        const auto irrDown = ocean::EvaluateShIrradiance(shY, { 0.0f, -1.0f, 0.0f });
        check(irrUp[0] > irrDown[0], "SH L1 upper hemisphere irradiance > lower hemisphere");
        check(irrDown[0] >= 0.0f && irrDown[1] >= 0.0f && irrDown[2] >= 0.0f, "SH irradiance clamped non-negative");

        // 2. EXR loading regression check
        const std::filesystem::path exrPath = DiamondDogs::GetSourceRelativePath("third_party/basis_universal/test_files/memorial.exr");
        if (std::filesystem::exists(exrPath))
        {
            ocean::ExrLoadOptions options{};
            options.TargetMaxWidth = 1024u;
            auto result = ocean::LoadExrEnvironmentMap(exrPath, options);
            if (!result.has_value())
            {
                check(false, std::format("LoadExrEnvironmentMap failed: {}", result.error()));
            }
            else
            {
                check(result->Width > 0u && result->Height > 0u, "EXR dimensions valid");
                check(result->PixelsFp16.size() == static_cast<size_t>(result->Width) * result->Height * 4u, "FP16 pixel count matches dimensions");

                const float dirLen = std::sqrt(
                    result->DominantLightDirection[0] * result->DominantLightDirection[0] +
                    result->DominantLightDirection[1] * result->DominantLightDirection[1] +
                    result->DominantLightDirection[2] * result->DominantLightDirection[2]);
                check(std::abs(dirLen - 1.0f) < 1e-2f, std::format("DominantLightDirection is unit length (got {})", dirLen));
                check(result->DominantLightDirection[1] >= 0.0f, "Dominant light in upper hemisphere (+Y >= 0)");
                check(result->DominantLightRadiance[0] >= 0.0f && result->DominantLightRadiance[1] >= 0.0f && result->DominantLightRadiance[2] >= 0.0f,
                      "Dominant light radiance is non-negative");

                check(result->SphericalHarmonics[0][0] > 0.0f && result->SphericalHarmonics[0][1] > 0.0f && result->SphericalHarmonics[0][2] > 0.0f,
                      "EXR L0 diffuse coefficient is strictly positive");
                const auto zenithIrr = ocean::EvaluateShIrradiance(result->SphericalHarmonics, { 0.0f, 1.0f, 0.0f });
                check(zenithIrr[0] > 0.0f && zenithIrr[1] > 0.0f && zenithIrr[2] > 0.0f, "EXR zenith diffuse irradiance is positive");
            }
        }
        else
        {
            std::cerr << std::format("[ExrLoader] WARNING: test file not found at '{}'\n", exrPath.string());
        }

        if (failures == 0)
        {
            std::cout << "[ExrLoader] PASS: L2 SH irradiance and dominant light extraction match reference expectations\n";
        }
        return failures;
    }

    [[nodiscard]] GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply)
    {
        if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return GraphicsResource{};
        }
        return reply->GetResource();
    }

    struct alignas(16) FrameConstants
    {
        std::array<std::array<float, 4>, 9> SkyShCoeffs{};
        std::array<float, 3> SunRadianceAp1{ 1.0f, 1.0f, 1.0f };
        float FoamRoughnessScale{ 1.0f };
        std::array<float, 3> WaterScatterColorAp1{ 0.02f, 0.15f, 0.2f };
        float SssStrength{ 0.5f };
        std::array<float, 3> SeabedTintAp1{ 0.6f, 0.5f, 0.4f };
        float SeabedRoughnessScale{ 1.0f };
        uint32_t UseCdlod{ 1u };
        uint32_t UseSkyTexture{ 1u };
        float SkyIntensityScale{ 1.0f };
        float LeadrSlopeVarianceScale{ 1.0f };
        std::array<std::array<float, 4>, 4> PrevViewProjectionRows{};
        std::array<float, 2> JitterOffsetNdc{ 0.0f, 0.0f };
        std::array<float, 2> PrevJitterOffsetNdc{ 0.0f, 0.0f };
        uint32_t UseBrunetonAtmosphere{ 0u };
        uint32_t UseDeferredPath{ 0u };
        uint32_t UseSpecularAa{ 1u };
        uint32_t UseTaa{ 1u };
        float SpecularAaStrength{ 1.0f };
        float SsrIntensity{ 1.0f };
        float TaaBlendAlpha{ 0.1f };
        float AutoExposureSpeed{ 1.0f };
    };
    static_assert(sizeof(FrameConstants) == 320u);

    struct alignas(16) SpecularAaPushConstants
    {
        std::array<uint32_t, 2> Dimensions{};
        float SpecularAaStrength{ 1.0f };
        float Padding{ 0.0f };
    };
    static_assert(sizeof(SpecularAaPushConstants) == 16u);

    struct alignas(16) TaaPushConstants
    {
        std::array<uint32_t, 2> Dimensions{};
        std::array<float, 2> InvDimensions{};
    };
    static_assert(sizeof(TaaPushConstants) == 16u);

    template<typename T>
    void PushProgramConstants(rhi::CommandBuffer& cmd, const rhi::ManifestProgram& program, const T& data)
    {
        if (const std::optional<rhi::PushConstantRange> range = program.PushConstants(); range.has_value())
        {
            cmd.PushConstants(program.PipelineLayout(), range->StageFlags, 0u,
                std::as_bytes(std::span{ &data, 1 }).first(std::min<size_t>(sizeof(T), range->Size)));
        }
    }

    [[nodiscard]] int32_t TestDeferredAndAtmosphere(const TestContext& context)
    {
        int32_t failures = 0;
        const auto check = [&](bool condition, std::string_view desc)
        {
            if (!condition)
            {
                std::cerr << std::format("[DeferredAndAtmosphere] FAIL: {}\n", desc);
                ++failures;
            }
        };

        // -----------------------------------------------------------------------------------------
        // Part 1: Bruneton Atmospheric Scattering Precomputation Verification
        // -----------------------------------------------------------------------------------------
        ocean::OceanAtmosphereSettings atmoSettings{};
        auto atmoResult = ocean::OceanAtmosphere::Create(context.Device, context.Resources, context.Library, atmoSettings);
        check(atmoResult.has_value(), "OceanAtmosphere::Create succeeded");
        if (!atmoResult.has_value())
        {
            return failures + 1;
        }
        auto& atmo = **atmoResult;

        // Transmittance LUT: 256x64 RGBA16F (131,072 bytes)
        // Irradiance LUT: 64x16 RGBA16F (8,192 bytes)
        Readback transReadback(context.Resources, 131072u);
        Readback irrReadback(context.Resources, 8192u);

        const bool atmoSubmitted = transReadback.Valid() && irrReadback.Valid() && Submit(context.Device, [&](rhi::CommandBuffer& cmd)
        {
            atmo.RecordPrecompute(cmd);

            auto transitionImage = [&](const GraphicsResource& res, rhi::ImageLayout oldLayout, rhi::ImageLayout newLayout, rhi::Access srcAccess, rhi::Access dstAccess)
            {
                const rhi::ImageBarrier barrier{
                    .Image = rhi::ImageHandle{ res.VkHandle },
                    .SrcStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::AllTransfer,
                    .SrcAccess = srcAccess,
                    .DstStages = rhi::PipelineStage::AllTransfer,
                    .DstAccess = dstAccess,
                    .OldLayout = oldLayout,
                    .NewLayout = newLayout,
                    .Range = rhi::ImageSubresourceRange{
                        .Aspects = rhi::ImageAspect::Color,
                        .BaseMipLevel = 0u,
                        .LevelCount = 1u,
                        .BaseArrayLayer = 0u,
                        .LayerCount = 1u
                    }
                };
                cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &barrier, 1 } });
            };

            transitionImage(atmo.TransmittanceLut(), rhi::ImageLayout::ShaderReadOnly, rhi::ImageLayout::TransferSource, rhi::Access::ShaderSampledRead, rhi::Access::TransferRead);
            transitionImage(atmo.IrradianceLut(), rhi::ImageLayout::ShaderReadOnly, rhi::ImageLayout::TransferSource, rhi::Access::ShaderSampledRead, rhi::Access::TransferRead);

            rhi::BufferImageCopy transRegion{};
            transRegion.Width = 256u;
            transRegion.Height = 64u;
            transRegion.LayerCount = 1u;
            cmd.CopyImageToBuffer(rhi::ImageHandle{ atmo.TransmittanceLut().VkHandle }, rhi::ImageLayout::TransferSource, transReadback.Handle(), std::span{ &transRegion, 1 });

            rhi::BufferImageCopy irrRegion{};
            irrRegion.Width = 64u;
            irrRegion.Height = 16u;
            irrRegion.LayerCount = 1u;
            cmd.CopyImageToBuffer(rhi::ImageHandle{ atmo.IrradianceLut().VkHandle }, rhi::ImageLayout::TransferSource, irrReadback.Handle(), std::span{ &irrRegion, 1 });

            CopyToHostBarrier(cmd);
        });
        check(atmoSubmitted, "Atmosphere precompute submitted and executed without GPU error");

        if (atmoSubmitted)
        {
            const std::vector<float> trans = transReadback.ReadHalves(0u, 256u * 64u * 4u);
            check(trans.size() == 256u * 64u * 4u, "Transmittance LUT readback size is 256*64*4");
            if (trans.size() == 256u * 64u * 4u)
            {
                // (x=0, y=0) corresponds to zenith at sea level
                const float tR = trans[0];
                const float tG = trans[1];
                const float tB = trans[2];
                check(tR > 0.0f && tG > 0.0f && tB > 0.0f, "Zenith transmittance is positive");
                check(tR > tG && tG > tB, "Rayleigh chromatic ordering tR > tG > tB holds");

                // Reference green transmittance: exp(-tau_g) ~= exp(-0.1420) ~= 0.8676
                constexpr float expectedTG = 0.8676f;
                const float tGError = std::abs(tG - expectedTG) / expectedTG;
                check(tGError < 0.02f, std::format("Zenith green transmittance matches reference within 2% (expected ~{:.4f}, got {:.4f}, err: {:.2f}%)", expectedTG, tG, tGError * 100.0f));

                // Monotonicity along mu for r = Rg (y = 0 row):
                // Transmittance decreases monotonically as x increases from 0 (zenith) to 255 (horizon)
                bool monotonic = true;
                for (uint32_t x = 0; x < 255; ++x)
                {
                    const float cur = trans[(0 * 256 + x) * 4 + 1];
                    const float nxt = trans[(0 * 256 + x + 1) * 4 + 1];
                    if (nxt > cur + 1e-4f)
                    {
                        monotonic = false;
                        break;
                    }
                }
                check(monotonic, "Transmittance along mu at sea level decreases monotonically from zenith to horizon");
                check(trans[(0 * 256 + 255) * 4 + 1] < tG, "Horizon transmittance is strictly lower than zenith transmittance");
            }

            const std::vector<float> irr = irrReadback.ReadHalves(0u, 64u * 16u * 4u);
            check(irr.size() == 64u * 16u * 4u, "Irradiance LUT readback size is 64*16*4");
            if (irr.size() == 64u * 16u * 4u)
            {
                bool nonNegative = true;
                for (float v : irr)
                {
                    if (v < -1e-4f)
                    {
                        nonNegative = false;
                        break;
                    }
                }
                check(nonNegative, "Irradiance LUT values are all non-negative");
                const float zenithIrrR = irr[(0 * 64 + 63) * 4 + 0];
                check(zenithIrrR > 0.0f, "Zenith ground irradiance is positive");
            }
        }

        // -----------------------------------------------------------------------------------------
        // Part 2: Deferred Compute Passes (OceanSpecularAaCS and OceanTaaResolveCS)
        // -----------------------------------------------------------------------------------------
        auto defIndexResult = context.Library.ModuleIndex("OceanDeferred");
        check(defIndexResult.has_value(), "Module OceanDeferred found");
        if (!defIndexResult.has_value())
        {
            return failures + 1;
        }
        const lodestone::ManifestIndex* defIndex = *defIndexResult;
        lodestone::ManifestQueryBuilder defQuery = defIndex->Query();

        const std::array<std::string_view, 1> specAaSpan{ "OceanSpecularAaCS" };
        auto specAaVariant = *defQuery.SingleFor(specAaSpan);
        auto specAaProgramResult = context.Library.CreateProgram(context.Device.Handle(), "OceanDeferred", specAaVariant, specAaSpan);
        check(specAaProgramResult.has_value(), "CreateProgram OceanSpecularAaCS succeeded");

        const std::array<std::string_view, 1> taaSpan{ "OceanTaaResolveCS" };
        auto taaVariant = *defQuery.SingleFor(taaSpan);
        auto taaProgramResult = context.Library.CreateProgram(context.Device.Handle(), "OceanDeferred", taaVariant, taaSpan);
        check(taaProgramResult.has_value(), "CreateProgram OceanTaaResolveCS succeeded");

        if (!specAaProgramResult.has_value() || !taaProgramResult.has_value())
        {
            return failures + 1;
        }

        auto& specAaProgram = *specAaProgramResult;
        auto& taaProgram = *taaProgramResult;
        const VkDevice vkDevice = context.Device.Handle().As<VkDevice>();

        const std::array<VkDescriptorPoolSize, 4> poolSizes{{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8u },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8u },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8u },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 8u },
        }};
        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = 8u,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data()
        };
        VkDescriptorPool testDescPool = VK_NULL_HANDLE;
        RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &testDescPool)));

        std::vector<GraphicsResource> testResources;
        auto autoDestroy = [&](GraphicsResource res) -> GraphicsResource
        {
            testResources.push_back(res);
            return res;
        };

        // FrameConstants uniform buffer
        FrameConstants frameConstants{};
        frameConstants.UseSpecularAa = 1u;
        frameConstants.SpecularAaStrength = 1.0f;
        frameConstants.UseTaa = 1u;
        frameConstants.TaaBlendAlpha = 0.25f;

        BufferCreateInfo frameInfo{};
        frameInfo.Domain = ResourceDomain::CPUToGPU;
        frameInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        frameInfo.Size = sizeof(FrameConstants);
        const RhiBufferResourceData frameData{ std::as_bytes(std::span{ &frameConstants, 1 }), 0u };
        GraphicsResource frameBuffer = autoDestroy(WaitForResource(context.Resources.CreateBuffer(frameInfo, std::span{ &frameData, 1 })));

        constexpr uint32_t k_TestDim = 32u;
        const uint64_t imageBytes = k_TestDim * k_TestDim * 4u * sizeof(uint16_t);

        auto createImage = [&](ImageUsageBits usage, const char* name) -> GraphicsResource
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = usage;
            info.Type = ImageType::Type2D;
            info.Format = CommonFormats::RGBA16_Float;
            info.Width = k_TestDim;
            info.Height = k_TestDim;
            info.Depth = 1u;
            info.MipLevels = 1u;
            info.ArrayLayers = 1u;
            info.Name = name;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2D;
            info.ViewInfo->Range.LevelCount = 1u;
            info.ViewInfo->Range.LayerCount = 1u;
            return autoDestroy(WaitForResource(context.Resources.CreateImage(info, {})));
        };

        GraphicsResource gbufferNormalSigmaIn = createImage(ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferDestination, "TestNormalSigmaIn");
        GraphicsResource gbufferNormalSigmaOut = createImage(ImageUsageBits::UavComputeShader | ImageUsageBits::TransferSource, "TestNormalSigmaOut");
        GraphicsResource gbufferMotionFoam = createImage(ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferDestination, "TestMotionFoam");
        GraphicsResource hdrLightingInput = createImage(ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferDestination, "TestHdrLightingInput");
        GraphicsResource historyImage = createImage(ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferDestination, "TestHistoryImage");
        GraphicsResource hdrResolvedOutput = createImage(ImageUsageBits::UavComputeShader | ImageUsageBits::TransferSource, "TestHdrResolvedOutput");
        GraphicsResource nextHistoryOutput = createImage(ImageUsageBits::UavComputeShader | ImageUsageBits::TransferSource, "TestNextHistoryOutput");

        // Prepare synthetic test pattern
        std::vector<uint16_t> normalUploadData(k_TestDim * k_TestDim * 4u);
        std::vector<uint16_t> motionUploadData(k_TestDim * k_TestDim * 4u, 0u);
        std::vector<uint16_t> lightingUploadData(k_TestDim * k_TestDim * 4u);
        std::vector<uint16_t> historyUploadData(k_TestDim * k_TestDim * 4u);

        for (uint32_t y = 0; y < k_TestDim; ++y)
        {
            for (uint32_t x = 0; x < k_TestDim; ++x)
            {
                const size_t idx = (y * k_TestDim + x) * 4u;
                // Normal slope: step edge along x = 16
                const float nx = (x >= 16u) ? 0.6f : 0.0f;
                const float ny = 0.0f;
                const float sigSq = 0.01f;
                normalUploadData[idx + 0] = ocean::FloatToHalf(nx);
                normalUploadData[idx + 1] = ocean::FloatToHalf(ny);
                normalUploadData[idx + 2] = ocean::FloatToHalf(sigSq);
                normalUploadData[idx + 3] = ocean::FloatToHalf(sigSq);

                // HDR lighting: uniform 1.0f
                lightingUploadData[idx + 0] = ocean::FloatToHalf(1.0f);
                lightingUploadData[idx + 1] = ocean::FloatToHalf(1.0f);
                lightingUploadData[idx + 2] = ocean::FloatToHalf(1.0f);
                lightingUploadData[idx + 3] = ocean::FloatToHalf(1.0f);

                // History: initial 0.0f
                historyUploadData[idx + 0] = ocean::FloatToHalf(0.0f);
                historyUploadData[idx + 1] = ocean::FloatToHalf(0.0f);
                historyUploadData[idx + 2] = ocean::FloatToHalf(0.0f);
                historyUploadData[idx + 3] = ocean::FloatToHalf(1.0f);
            }
        }

        BufferCreateInfo uploadInfo{};
        uploadInfo.Domain = ResourceDomain::CPUToGPU;
        uploadInfo.Usage = BufferUsageBits::CopySource;
        uploadInfo.Size = imageBytes * 4u;

        std::vector<uint16_t> combinedUpload;
        combinedUpload.reserve(k_TestDim * k_TestDim * 16u);
        combinedUpload.insert(combinedUpload.end(), normalUploadData.begin(), normalUploadData.end());
        combinedUpload.insert(combinedUpload.end(), motionUploadData.begin(), motionUploadData.end());
        combinedUpload.insert(combinedUpload.end(), lightingUploadData.begin(), lightingUploadData.end());
        combinedUpload.insert(combinedUpload.end(), historyUploadData.begin(), historyUploadData.end());

        const RhiBufferResourceData uploadResourceData{ std::as_bytes(std::span{ combinedUpload }), 0u };
        GraphicsResource stagingBuffer = autoDestroy(WaitForResource(context.Resources.CreateBuffer(uploadInfo, std::span{ &uploadResourceData, 1 })));

        Readback specAaReadback(context.Resources, imageBytes);
        Readback taaReadback(context.Resources, imageBytes);

        // Descriptor sets
        const VkDescriptorSetLayout specLayout = specAaProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
        VkDescriptorSet specSet = VK_NULL_HANDLE;
        const VkDescriptorSetAllocateInfo specAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, testDescPool, 1, &specLayout };
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &specAllocInfo, &specSet)));

        const BoundResources specBound{
            { "Frame", { frameBuffer } },
            { "GBufferNormalSigmaIn", { gbufferNormalSigmaIn, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
            { "GBufferNormalSigmaOut", { gbufferNormalSigmaOut, VK_IMAGE_LAYOUT_GENERAL } },
        };
        (void)WriteManifestDescriptors(vkDevice, specSet, *defIndex, specAaVariant, specAaSpan, specBound);

        const VkDescriptorSetLayout taaLayout = taaProgram->SetLayouts()[0].As<VkDescriptorSetLayout>();
        VkDescriptorSet taaSet = VK_NULL_HANDLE;
        const VkDescriptorSetAllocateInfo taaAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, testDescPool, 1, &taaLayout };
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &taaAllocInfo, &taaSet)));

        const BoundResources taaBound{
            { "Frame", { frameBuffer } },
            { "GBufferMotionFoam", { gbufferMotionFoam, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
            { "HistoryColorTexture", { historyImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
            { "LinearClampSampler", { atmo.Sampler() } },
            { "HdrLightingInput", { hdrLightingInput, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
            { "HdrResolvedOutput", { hdrResolvedOutput, VK_IMAGE_LAYOUT_GENERAL } },
            { "NextHistoryOutput", { nextHistoryOutput, VK_IMAGE_LAYOUT_GENERAL } },
        };
        (void)WriteManifestDescriptors(vkDevice, taaSet, *defIndex, taaVariant, taaSpan, taaBound);

        const bool computeSubmitted = specAaReadback.Valid() && taaReadback.Valid() && Submit(context.Device, [&](rhi::CommandBuffer& cmd)
        {
            auto transition = [&](const GraphicsResource& res, rhi::ImageLayout oldLayout, rhi::ImageLayout newLayout, rhi::Access srcAccess, rhi::Access dstAccess, rhi::PipelineStage srcStage, rhi::PipelineStage dstStage)
            {
                const rhi::ImageBarrier barrier{
                    .Image = rhi::ImageHandle{ res.VkHandle },
                    .SrcStages = srcStage,
                    .SrcAccess = srcAccess,
                    .DstStages = dstStage,
                    .DstAccess = dstAccess,
                    .OldLayout = oldLayout,
                    .NewLayout = newLayout,
                    .Range = rhi::ImageSubresourceRange{
                        .Aspects = rhi::ImageAspect::Color,
                        .BaseMipLevel = 0u,
                        .LevelCount = 1u,
                        .BaseArrayLayer = 0u,
                        .LayerCount = 1u
                    }
                };
                cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &barrier, 1 } });
            };

            // 1. Transition inputs to TransferDst
            transition(gbufferNormalSigmaIn, rhi::ImageLayout::Undefined, rhi::ImageLayout::TransferDestination, rhi::Access::None, rhi::Access::TransferWrite, rhi::PipelineStage::None, rhi::PipelineStage::AllTransfer);
            transition(gbufferMotionFoam, rhi::ImageLayout::Undefined, rhi::ImageLayout::TransferDestination, rhi::Access::None, rhi::Access::TransferWrite, rhi::PipelineStage::None, rhi::PipelineStage::AllTransfer);
            transition(hdrLightingInput, rhi::ImageLayout::Undefined, rhi::ImageLayout::TransferDestination, rhi::Access::None, rhi::Access::TransferWrite, rhi::PipelineStage::None, rhi::PipelineStage::AllTransfer);
            transition(historyImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::TransferDestination, rhi::Access::None, rhi::Access::TransferWrite, rhi::PipelineStage::None, rhi::PipelineStage::AllTransfer);

            // 2. Copy staging buffer to images
            auto copySub = [&](const GraphicsResource& dst, uint64_t offset)
            {
                rhi::BufferImageCopy region{};
                region.BufferOffset = offset;
                region.Width = k_TestDim;
                region.Height = k_TestDim;
                region.LayerCount = 1u;
                cmd.CopyBufferToImage(rhi::BufferHandle{ stagingBuffer.VkHandle }, rhi::ImageHandle{ dst.VkHandle }, rhi::ImageLayout::TransferDestination, std::span{ &region, 1 });
            };

            copySub(gbufferNormalSigmaIn, 0u);
            copySub(gbufferMotionFoam, imageBytes);
            copySub(hdrLightingInput, imageBytes * 2u);
            copySub(historyImage, imageBytes * 3u);

            // 3. Transition to ShaderReadOnly / General
            transition(gbufferNormalSigmaIn, rhi::ImageLayout::TransferDestination, rhi::ImageLayout::ShaderReadOnly, rhi::Access::TransferWrite, rhi::Access::ShaderSampledRead, rhi::PipelineStage::AllTransfer, rhi::PipelineStage::ComputeShader);
            transition(gbufferNormalSigmaOut, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite, rhi::PipelineStage::None, rhi::PipelineStage::ComputeShader);

            // 4. Dispatch OceanSpecularAaCS
            RhiAssert(cmd.BindShaders(specAaProgram->Program()));
            const rhi::DescriptorSetHandle specHandle{ reinterpret_cast<uint64_t>(specSet) };
            cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, specAaProgram->PipelineLayout(), 0, std::span{ &specHandle, 1 });
            const SpecularAaPushConstants specPush{
                .Dimensions = { k_TestDim, k_TestDim },
                .SpecularAaStrength = 1.0f,
                .Padding = 0.0f
            };
            PushProgramConstants(cmd, *specAaProgram, specPush);
            cmd.Dispatch((k_TestDim + 7u) / 8u, (k_TestDim + 7u) / 8u, 1u);

            // 5. Transition TAA images and dispatch OceanTaaResolveCS
            transition(gbufferMotionFoam, rhi::ImageLayout::TransferDestination, rhi::ImageLayout::ShaderReadOnly, rhi::Access::TransferWrite, rhi::Access::ShaderSampledRead, rhi::PipelineStage::AllTransfer, rhi::PipelineStage::ComputeShader);
            transition(hdrLightingInput, rhi::ImageLayout::TransferDestination, rhi::ImageLayout::ShaderReadOnly, rhi::Access::TransferWrite, rhi::Access::ShaderSampledRead, rhi::PipelineStage::AllTransfer, rhi::PipelineStage::ComputeShader);
            transition(historyImage, rhi::ImageLayout::TransferDestination, rhi::ImageLayout::ShaderReadOnly, rhi::Access::TransferWrite, rhi::Access::ShaderSampledRead, rhi::PipelineStage::AllTransfer, rhi::PipelineStage::ComputeShader);
            transition(hdrResolvedOutput, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite, rhi::PipelineStage::None, rhi::PipelineStage::ComputeShader);
            transition(nextHistoryOutput, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite, rhi::PipelineStage::None, rhi::PipelineStage::ComputeShader);

            RhiAssert(cmd.BindShaders(taaProgram->Program()));
            const rhi::DescriptorSetHandle taaHandle{ reinterpret_cast<uint64_t>(taaSet) };
            cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, taaProgram->PipelineLayout(), 0, std::span{ &taaHandle, 1 });
            const TaaPushConstants taaPush{
                .Dimensions = { k_TestDim, k_TestDim },
                .InvDimensions = { 1.0f / float(k_TestDim), 1.0f / float(k_TestDim) }
            };
            PushProgramConstants(cmd, *taaProgram, taaPush);
            cmd.Dispatch((k_TestDim + 7u) / 8u, (k_TestDim + 7u) / 8u, 1u);

            // 6. Transition outputs to TransferSrc and copy to Readback
            transition(gbufferNormalSigmaOut, rhi::ImageLayout::General, rhi::ImageLayout::TransferSource, rhi::Access::ShaderStorageWrite, rhi::Access::TransferRead, rhi::PipelineStage::ComputeShader, rhi::PipelineStage::AllTransfer);
            transition(hdrResolvedOutput, rhi::ImageLayout::General, rhi::ImageLayout::TransferSource, rhi::Access::ShaderStorageWrite, rhi::Access::TransferRead, rhi::PipelineStage::ComputeShader, rhi::PipelineStage::AllTransfer);

            rhi::BufferImageCopy specRegion{};
            specRegion.Width = k_TestDim;
            specRegion.Height = k_TestDim;
            specRegion.LayerCount = 1u;
            cmd.CopyImageToBuffer(rhi::ImageHandle{ gbufferNormalSigmaOut.VkHandle }, rhi::ImageLayout::TransferSource, specAaReadback.Handle(), std::span{ &specRegion, 1 });

            rhi::BufferImageCopy taaRegion{};
            taaRegion.Width = k_TestDim;
            taaRegion.Height = k_TestDim;
            taaRegion.LayerCount = 1u;
            cmd.CopyImageToBuffer(rhi::ImageHandle{ hdrResolvedOutput.VkHandle }, rhi::ImageLayout::TransferSource, taaReadback.Handle(), std::span{ &taaRegion, 1 });

            CopyToHostBarrier(cmd);
        });

        check(computeSubmitted, "Deferred compute dispatches (Specular AA + TAA) finished without GPU error");
        if (computeSubmitted)
        {
            const std::vector<float> specData = specAaReadback.ReadHalves(0u, k_TestDim * k_TestDim * 4u);
            check(specData.size() == k_TestDim * k_TestDim * 4u, "Specular AA readback size matches");
            if (specData.size() == k_TestDim * k_TestDim * 4u)
            {
                // Flat region: (x = 5, y = 16)
                const size_t flatIdx = (16u * k_TestDim + 5u) * 4u;
                const float flatSigX = specData[flatIdx + 2];
                check(std::abs(flatSigX - 0.01f) < 0.005f, std::format("Flat region variance unchanged (~0.01, got {:.4f})", flatSigX));

                // Edge region: (x = 16, y = 16)
                const size_t edgeIdx = (16u * k_TestDim + 16u) * 4u;
                const float edgeSigX = specData[edgeIdx + 2];
                check(edgeSigX > 0.05f, std::format("Normal edge variance increased by Specular AA (>0.05, got {:.4f})", edgeSigX));

                bool noNan = true;
                for (float v : specData)
                {
                    if (std::isnan(v) || std::isinf(v)) { noNan = false; break; }
                }
                check(noNan, "No NaN or Inf in Specular AA output");
            }

            const std::vector<float> taaData = taaReadback.ReadHalves(0u, k_TestDim * k_TestDim * 4u);
            check(taaData.size() == k_TestDim * k_TestDim * 4u, "TAA readback size matches");
            if (taaData.size() == k_TestDim * k_TestDim * 4u)
            {
                bool noNan = true;
                for (float v : taaData)
                {
                    if (std::isnan(v) || std::isinf(v)) { noNan = false; break; }
                }
                check(noNan, "No NaN or Inf in TAA resolved output");

                const size_t centerIdx = (16u * k_TestDim + 16u) * 4u;
                const float centerR = taaData[centerIdx + 0];
                check(centerR > 0.5f && centerR <= 1.05f, std::format("TAA output converges toward 1.0 (got {:.4f})", centerR));
            }
        }

        // Cleanup resources
        for (const GraphicsResource& res : testResources)
        {
            if (res)
            {
                (void)context.Resources.DestroyResource(res)->WaitForCompletion(k_TimeoutNs);
            }
        }
        vkDestroyDescriptorPool(vkDevice, testDescPool, nullptr);

        if (failures == 0)
        {
            std::cout << "[DeferredAndAtmosphere] PASS: Atmosphere LUTs verified and Deferred compute passes executed cleanly\n";
        }
        return failures;
    }
}

int main()
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "OceanFftTest";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(OCEAN_FFT_MANIFEST);
    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", OCEAN_FFT_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    int32_t failures = 0;
    failures += TestComputeCdlodMaxLevel();
    failures += TestExrLoader();
    {
        ResourceContext resources;
        resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });
        const TestContext context{ *rhiSystem->GetDevice(), resources, **library, ocean::PickWaveSize(*rhiSystem->GetDevice()) };
        failures += TestShaderVariants(context);
        failures += TestOceanCdlod(context);
        failures += TestDeferredAndAtmosphere(context);
        if (context.WaveSize == 0u)
        {
            std::cout << "This device can't run the wave-op variants (subgroup size, size control or shuffles): testing without wave ops only\n";
        }

        std::vector<uint32_t> waveSizes{ 0u };
        if (context.WaveSize != 0u)
        {
            waveSizes.push_back(context.WaveSize);
        }
        for (const uint32_t waveSize : waveSizes)
        {
            for (const uint32_t size : { 256u, 512u })
            {
                ocean::OceanSimulationSettings settings{};
                settings.Size = size;
                settings.WaveSize = waveSize;
                failures += TestIfft(context, settings);
                failures += TestFusedMatchesUnfused(context, settings, 3.25f);
            }
        }

        // every spreading function is normalized over the circle, so each one must give the same plausible wave height.
        // Donelan-Banner reads the generated LUT, so this also checks its upload and its axis mapping
        for (const uint32_t spreading : { 0u, 1u, 2u })
        {
            ocean::OceanSimulationSettings settings{};
            settings.Size = 256u;
            settings.WaveSize = context.WaveSize;
            settings.Spectrum.DirectionalSpreadingMode = spreading;
            // the wave height band is for the wind sea alone: the swell adds its own energy
            settings.Spectrum.SwellScale = 0.0f;
            failures += TestPipeline(context, settings, 3.25f);
        }

        // the editor's whole pipe, swell included: the default sea state's spectrum against its own readout
        ocean::OceanSimulationSettings seaStateSettings{};
        seaStateSettings.Size = 256u;
        seaStateSettings.WaveSize = context.WaveSize;
        seaStateSettings.Spectrum = ocean::ApplySeaState(ocean::SeaState{}, seaStateSettings.Spectrum).Spectrum;
        failures += TestPipeline(context, seaStateSettings, 3.25f);

        // the default settings above run the fused kernels: run the unfused passes through the same checks too
        seaStateSettings.FusedKernels = false;
        failures += TestPipeline(context, seaStateSettings, 3.25f);
    }

    if (failures != 0)
    {
        std::cerr << std::format("OceanFftTest: {} failure(s)\n", failures);
        return 1;
    }
    std::cout << "OceanFftTest: every check matches the CPU reference\n";
    return 0;
}
