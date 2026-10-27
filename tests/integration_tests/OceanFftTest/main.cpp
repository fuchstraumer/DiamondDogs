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
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
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
#include <vector>

// Runs the OceanFft compute chain headless, through the same ocean::OceanSimulation the OceanFft application uses,
// and checks it against double precision on the CPU:
//  1. The radix-4 IFFT kernel alone, with and without wave ops, at 256 (even log2) and 512 (odd log2, so the
//     radix-2 tail stage runs). Random half input, compared with a CPU 2D inverse FFT of the same quantized values.
//  2. SpectrumConjugateCS: conj(h0(-k)), bit exact.
//  3. Tick, IFFT and resolve: the CPU repeats the tick and the transform from the GPU's own h0(k) and wave data,
//     and every field of the Displacement and Derivatives textures is compared with it.
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
        return std::format("size {} {} {}", settings.Size,
            settings.WaveSize == 0u ? std::string{ "no wave ops" } : std::format("wave {}", settings.WaveSize), k_SpreadingNames[spreading]);
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
        const std::vector<float> wave = readback.ReadHalves(waveOffset, spectrumElements * 4u);
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
                const double kx = wave[index * 4u];
                const double kz = wave[index * 4u + 2u];
                const double omega = wave[index * 4u + 3u];
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
    {
        ResourceContext resources;
        resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });
        const TestContext context{ *rhiSystem->GetDevice(), resources, **library, ocean::PickWaveSize(*rhiSystem->GetDevice()) };
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
    }

    if (failures != 0)
    {
        std::cerr << std::format("OceanFftTest: {} failure(s)\n", failures);
        return 1;
    }
    std::cout << "OceanFftTest: every check matches the CPU reference\n";
    return 0;
}
