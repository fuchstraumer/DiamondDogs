#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "GpuPyramid.hpp"
#include "ImageDataFormats.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "ShaderLibrary.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Runs pyramid::MipGenerator (NvproPyramid, the Slang transcription of NVIDIA's nvpro_pyramid) headless on rgba32f
// and rgba16f 2D array textures of random data, and checks every level of every layer against the CPU:
//  - power-of-two sizes, where the fast pipeline fills up to six levels per dispatch and the general pipeline the
//    1x1 tail;
//  - odd and non-square sizes, where only the general pipeline runs, with its 3-tap kernel;
//  - the average, min and max reductions, and the fp16 groupshared cache.
// Each level is compared with the reduction of the level above it, as the GPU wrote it. Min and max must match
// exactly. An average may differ by float rounding: the GPU sums in another order, and the bilinear fetch and the
// shuffles keep values that the reference reads back from memory.

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    // float32 rounding, reordered sums, and the hardware filter of the bilinear fetch
    constexpr double k_AverageTolerance = 1.0e-5;
    // the fp16 cache rounds the levels it holds once more, and an rgba16f texture rounds every level it stores
    constexpr double k_HalfTolerance = 2.0e-3;
    constexpr uint32_t k_Channels = 4u;

    // rgba32f checks the arithmetic tightly. rgba16f is what the ocean uses: the level bindings declare no format,
    // and a declared rgba32f would make every rgba16f view undefined (validation reports it)
    enum class TexelFormat : uint8_t
    {
        Rgba32f,
        Rgba16f,
    };

    struct TestCase
    {
        uint32_t Width;
        uint32_t Height;
        uint32_t Layers;
        pyramid::Reduction Reduce;
        bool SharedF16;
        TexelFormat Format;
    };

    [[nodiscard]] float HalfToFloat(uint16_t value) noexcept
    {
        const uint32_t sign = (value & 0x8000u) << 16u;
        const uint32_t exponent = (value >> 10u) & 0x1Fu;
        const uint32_t mantissa = value & 0x3FFu;

        if (exponent == 0u)
        {
            const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
            return sign != 0u ? -magnitude : magnitude;
        }

        if (exponent == 31u)
        {
            return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13u));
        }

        return std::bit_cast<float>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
    }

    // round to nearest even. The test values are finite and well inside the half range, but some are denormal halves
    [[nodiscard]] uint16_t FloatToHalf(float value) noexcept
    {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
        const float magnitude = std::fabs(value);

        if (magnitude < 6.103515625e-05f)
        {
            // denormal: round to the nearest multiple of 2^-24
            return static_cast<uint16_t>(sign | static_cast<uint16_t>(std::nearbyint(magnitude * 16777216.0f)));
        }

        const uint32_t magnitudeBits = bits & 0x7FFFFFFFu;
        const uint32_t rounded = magnitudeBits + 0x0FFFu + ((magnitudeBits >> 13u) & 1u);
        return static_cast<uint16_t>(sign | ((rounded - (112u << 23u)) >> 13u));
    }

    [[nodiscard]] std::string_view ReductionName(pyramid::Reduction reduction) noexcept
    {
        switch (reduction)
        {
        case pyramid::Reduction::Average: return "average";
        case pyramid::Reduction::Min: return "min";
        default: return "max";
        }
    }

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

    [[nodiscard]] GraphicsResource WaitForResource(const std::shared_ptr<GraphicsResourceReply>& reply)
    {
        if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            return GraphicsResource::Null();
        }

        return reply->GetResource();
    }

    [[nodiscard]] uint32_t LevelSize(uint32_t size, uint32_t level) noexcept
    {
        return std::max(size >> level, 1u);
    }

    // nvpro_pyramid.glsl, kernelSizeFromInputSize_: 1 tap for a 1-texel edge, 3 taps for an odd one, 2 otherwise
    [[nodiscard]] uint32_t KernelTaps(uint32_t input_size) noexcept
    {
        return input_size == 1u ? 1u : (2u | (input_size & 1u));
    }

    // the weights of each tap along one axis. Three taps use the general pipeline's energy-conserving weights for
    // output texel `coord` of `output_size`
    [[nodiscard]] std::array<double, 3> TapWeights(uint32_t taps, uint32_t coord, uint32_t output_size) noexcept
    {
        if (taps == 1u)
        {
            return { 1.0, 0.0, 0.0 };
        }

        if (taps == 2u)
        {
            return { 0.5, 0.5, 0.0 };
        }

        const double n = output_size;
        const double w0 = (n - coord) / (2.0 * n + 1.0);
        const double w1 = n / (2.0 * n + 1.0);
        return { w0, w1, 1.0 - w0 - w1 };
    }

    // one level from the level above it, every layer and channel, in double
    [[nodiscard]] std::vector<double> ReduceLevel(std::span<const float> above, uint32_t above_width, uint32_t above_height, uint32_t layers, pyramid::Reduction reduction)
    {
        const uint32_t width = std::max(above_width / 2u, 1u);
        const uint32_t height = std::max(above_height / 2u, 1u);
        const uint32_t tapsX = KernelTaps(above_width);
        const uint32_t tapsY = KernelTaps(above_height);
        std::vector<double> level(uint64_t{ width } * height * layers * k_Channels);

        for (uint32_t layer = 0u; layer < layers; ++layer)
        {
            for (uint32_t y = 0u; y < height; ++y)
            {
                for (uint32_t x = 0u; x < width; ++x)
                {
                    const std::array<double, 3> weightsX = TapWeights(tapsX, x, width);
                    const std::array<double, 3> weightsY = TapWeights(tapsY, y, height);

                    for (uint32_t channel = 0u; channel < k_Channels; ++channel)
                    {
                        double value = reduction == pyramid::Reduction::Average ? 0.0 : std::nan("");

                        for (uint32_t ty = 0u; ty < tapsY; ++ty)
                        {
                            for (uint32_t tx = 0u; tx < tapsX; ++tx)
                            {
                                const uint64_t texel = (uint64_t{ layer } * above_height + (2u * y + ty)) * above_width + (2u * x + tx);
                                const double sample = above[texel * k_Channels + channel];

                                if (reduction == pyramid::Reduction::Average)
                                {
                                    value += weightsX[tx] * weightsY[ty] * sample;
                                }
                                else if (std::isnan(value))
                                {
                                    value = sample;
                                }
                                else
                                {
                                    value = reduction == pyramid::Reduction::Min ? std::min(value, sample) : std::max(value, sample);
                                }
                            }
                        }

                        level[((uint64_t{ layer } * height + y) * width + x) * k_Channels + channel] = value;
                    }
                }
            }
        }

        return level;
    }

    struct LevelError
    {
        double RelativeRms{ 0.0 };
        uint64_t Mismatches{ 0u };
        bool Finite{ true };
    };

    [[nodiscard]] LevelError Compare(std::span<const float> gpu, std::span<const double> reference)
    {
        LevelError error{};
        double errorSquared = 0.0;
        double referenceSquared = 0.0;

        for (size_t i = 0; i < gpu.size(); ++i)
        {
            if (!std::isfinite(gpu[i]))
            {
                error.Finite = false;
            }

            const double difference = double{ gpu[i] } - reference[i];
            errorSquared += difference * difference;
            referenceSquared += reference[i] * reference[i];
            // min and max pick one of the GPU's own values, so a float32 equality is the test
            error.Mismatches += gpu[i] != static_cast<float>(reference[i]) ? 1u : 0u;
        }

        error.RelativeRms = referenceSquared > 0.0 ? std::sqrt(errorSquared / referenceSquared) : std::sqrt(errorSquared);
        return error;
    }

    [[nodiscard]] int32_t RunCase(rhi::Device& device, ResourceContext& resources, const pyramid::MipGenerator& generator, const TestCase& test)
    {
        const uint32_t mipLevels = static_cast<uint32_t>(std::bit_width(std::max(test.Width, test.Height)));
        const bool half = test.Format == TexelFormat::Rgba16f;
        const uint64_t channelBytes = half ? sizeof(uint16_t) : sizeof(float);
        const std::string name = std::format("{}x{}x{} {} {}{}", test.Width, test.Height, test.Layers, half ? "rgba16f" : "rgba32f",
            ReductionName(test.Reduce), test.SharedF16 ? " shared fp16" : "");

        // offsets of each level in the readback (and of level 0 in the upload)
        std::vector<uint64_t> levelOffsets;
        uint64_t readbackBytes = 0u;

        for (uint32_t level = 0u; level < mipLevels; ++level)
        {
            levelOffsets.push_back(readbackBytes);
            readbackBytes += uint64_t{ LevelSize(test.Width, level) } * LevelSize(test.Height, level) * test.Layers * k_Channels * channelBytes;
        }

        // random level 0, from a fixed seed. Negative values too, so that min and max are not the same test. For
        // rgba16f the values are rounded to halves first, so that the upload is exact
        std::vector<float> levelZero(uint64_t{ test.Width } * test.Height * test.Layers * k_Channels);
        std::mt19937 random(test.Width * 7919u + test.Height * 104729u + test.Layers);
        std::uniform_real_distribution<float> distribution(-2.0f, 2.0f);
        std::ranges::generate(levelZero, [&]() { return distribution(random); });
        std::vector<uint16_t> levelZeroHalves;

        if (half)
        {
            for (float& value : levelZero)
            {
                levelZeroHalves.push_back(FloatToHalf(value));
                value = HalfToFloat(levelZeroHalves.back());
            }
        }

        BufferCreateInfo uploadInfo{};
        uploadInfo.Domain = ResourceDomain::CPUToGPU;
        uploadInfo.Usage = BufferUsageBits::CopySource;
        uploadInfo.Size = levelZero.size() * channelBytes;
        const RhiBufferResourceData uploadData{ half ? std::as_bytes(std::span{ levelZeroHalves }) : std::as_bytes(std::span{ levelZero }), 0u };
        const GraphicsResource upload = WaitForResource(resources.CreateBuffer(uploadInfo, std::span{ &uploadData, 1 }));

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = readbackBytes;
        const GraphicsResource readback = WaitForResource(resources.CreateBuffer(readbackInfo, {}));

        ImageCreateInfo imageInfo{};
        imageInfo.Domain = ResourceDomain::GPUOnly;
        imageInfo.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader |
            ImageUsageBits::TransferSource | ImageUsageBits::TransferDestination;
        imageInfo.Type = ImageType::Type2D;
        imageInfo.Format = half ? CommonFormats::RGBA16_Float : CommonFormats::RGBA32_Float;
        imageInfo.Width = test.Width;
        imageInfo.Height = test.Height;
        imageInfo.Depth = 1u;
        imageInfo.MipLevels = mipLevels;
        imageInfo.ArrayLayers = test.Layers;
        imageInfo.ViewInfo = ImageViewCreateInfo{};
        imageInfo.ViewInfo->ViewType = ImageViewType::Type2DArray;
        imageInfo.ViewInfo->Range.LevelCount = mipLevels;
        imageInfo.ViewInfo->Range.LayerCount = test.Layers;
        const GraphicsResource image = WaitForResource(resources.CreateImage(imageInfo, {}));

        const auto destroy = [&]()
        {
            for (const GraphicsResource& resource : { upload, readback, image })
            {
                if (resource)
                {
                    (void)resources.DestroyResource(resource)->WaitForCompletion(k_TimeoutNs);
                }
            }
        };

        if (!upload || !readback || !image)
        {
            std::cerr << std::format("[{}] creating the buffers or the image failed\n", name);
            destroy();
            return 1;
        }

        int32_t failures = 0;

        {
            std::expected<std::unique_ptr<pyramid::MipTarget>, std::string> target = generator.CreateTarget(pyramid::MipTargetCreateInfo{
                reinterpret_cast<VkImage>(image.VkHandle), half ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT,
                test.Width, test.Height, mipLevels, test.Layers });

            if (!target.has_value())
            {
                std::cerr << std::format("[{}] CreateTarget: {}\n", name, target.error());
                destroy();
                return 1;
            }

            const rhi::ImageHandle imageHandle{ image.VkHandle };
            const rhi::ImageSubresourceRange allLevels{ rhi::ImageAspect::Color, 0u, mipLevels, 0u, test.Layers };
            const std::array<const pyramid::MipTarget*, 1> targets{ target->get() };

            const bool finished = Submit(device, [&](rhi::CommandBuffer& command_buffer)
            {
                const rhi::ImageBarrier toCopy{ imageHandle, rhi::PipelineStage::None, rhi::Access::None, rhi::PipelineStage::Copy, rhi::Access::TransferWrite,
                    rhi::ImageLayout::Undefined, rhi::ImageLayout::General, allLevels };
                command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toCopy, 1 } });

                rhi::BufferImageCopy region{};
                region.LayerCount = test.Layers;
                region.Width = test.Width;
                region.Height = test.Height;
                command_buffer.CopyBufferToImage(rhi::BufferHandle{ upload.VkHandle }, imageHandle, rhi::ImageLayout::General, std::span{ &region, 1 });

                const rhi::GlobalBarrier copyToPyramid{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, pyramid::MipGenerator::k_ReadStages, pyramid::MipGenerator::k_ReadAccess };
                command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &copyToPyramid, 1 }, {}, {} });
                generator.Record(command_buffer, targets);

                const rhi::GlobalBarrier pyramidToCopy{ pyramid::MipGenerator::k_WriteStages, pyramid::MipGenerator::k_WriteAccess, rhi::PipelineStage::Copy, rhi::Access::TransferRead };
                command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &pyramidToCopy, 1 }, {}, {} });

                for (uint32_t level = 0u; level < mipLevels; ++level)
                {
                    rhi::BufferImageCopy levelRegion{};
                    levelRegion.BufferOffset = levelOffsets[level];
                    levelRegion.MipLevel = level;
                    levelRegion.LayerCount = test.Layers;
                    levelRegion.Width = LevelSize(test.Width, level);
                    levelRegion.Height = LevelSize(test.Height, level);
                    command_buffer.CopyImageToBuffer(imageHandle, rhi::ImageLayout::General, rhi::BufferHandle{ readback.VkHandle }, std::span{ &levelRegion, 1 });
                }

                const rhi::GlobalBarrier copyToHost{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
                command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &copyToHost, 1 }, {}, {} });
            });

            if (!finished)
            {
                std::cerr << std::format("[{}] the dispatches did not finish\n", name);
                destroy();
                return 1;
            }

            // every level as floats; the offsets below count floats, not bytes
            std::vector<float> levels(readbackBytes / channelBytes);
            std::shared_ptr<PointerMessageReply> map = resources.MapBuffer(readback, readbackBytes, 0u);

            if (map->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << std::format("[{}] mapping the readback failed\n", name);
                destroy();
                return 1;
            }

            if (half)
            {
                std::vector<uint16_t> halves(levels.size());
                std::memcpy(halves.data(), map->GetPointer(), readbackBytes);
                std::ranges::transform(halves, levels.begin(), HalfToFloat);
            }
            else
            {
                std::memcpy(levels.data(), map->GetPointer(), readbackBytes);
            }

            (void)resources.UnmapBuffer(readback, readbackBytes, 0u)->WaitForCompletion(k_TimeoutNs);

            // the upload must have arrived unchanged, or nothing below means anything
            const std::span<const float> gpuLevelZero = std::span{ levels }.first(levelZero.size());

            if (!std::ranges::equal(gpuLevelZero, levelZero))
            {
                std::cerr << std::format("[{}] level 0 does not match the upload\n", name);
                ++failures;
            }

            const double tolerance = test.SharedF16 || half ? k_HalfTolerance : k_AverageTolerance;
            LevelError worst{};
            uint32_t worstLevel = 0u;
            bool passed = true;

            for (uint32_t level = 1u; level < mipLevels; ++level)
            {
                const uint32_t aboveWidth = LevelSize(test.Width, level - 1u);
                const uint32_t aboveHeight = LevelSize(test.Height, level - 1u);
                const uint64_t aboveCount = uint64_t{ aboveWidth } * aboveHeight * test.Layers * k_Channels;
                const uint64_t count = uint64_t{ LevelSize(test.Width, level) } * LevelSize(test.Height, level) * test.Layers * k_Channels;
                const std::span<const float> above = std::span{ levels }.subspan(levelOffsets[level - 1u] / channelBytes, aboveCount);
                const std::span<const float> gpu = std::span{ levels }.subspan(levelOffsets[level] / channelBytes, count);
                const std::vector<double> reference = ReduceLevel(above, aboveWidth, aboveHeight, test.Layers, test.Reduce);
                const LevelError error = Compare(gpu, reference);

                // min and max are exact, unless the fp16 cache rounded a level they read
                const bool exact = test.Reduce != pyramid::Reduction::Average && !test.SharedF16;
                const bool levelPassed = error.Finite && (exact ? error.Mismatches == 0u : error.RelativeRms < tolerance);

                if (!levelPassed || error.RelativeRms >= worst.RelativeRms)
                {
                    worst = error;
                    worstLevel = level;
                }

                passed = passed && levelPassed;
            }

            (passed ? std::cout : std::cerr) << std::format("[{}] {} levels in {} dispatches ({} fast): worst level {}, relative RMS error {:.2e}, {} mismatches{}\n",
                name, mipLevels, (*target)->DispatchCount(), (*target)->FastDispatchCount(), worstLevel, worst.RelativeRms, worst.Mismatches,
                worst.Finite ? "" : ", non-finite values");
            failures += passed ? 0 : 1;
        }

        destroy();
        return failures;
    }
}

int main()
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "GpuPyramidTest";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(GPU_PYRAMID_TEST_MANIFEST);
    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", GPU_PYRAMID_TEST_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    // power-of-two sizes take the fast pipeline (six levels, then the rest), odd and non-square ones the general
    // pipeline only. 1024 takes six levels and then four; 128 ends with a general dispatch for its 2x2 level
    using enum TexelFormat;
    const std::array k_Cases
    {
        TestCase{ 256u, 256u, 4u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 1024u, 1024u, 1u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 128u, 128u, 2u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 512u, 128u, 1u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 127u, 127u, 1u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 255u, 97u, 3u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 200u, 3u, 1u, pyramid::Reduction::Average, false, Rgba32f },
        TestCase{ 256u, 256u, 2u, pyramid::Reduction::Average, true, Rgba32f },
        TestCase{ 255u, 97u, 1u, pyramid::Reduction::Average, true, Rgba32f },
        TestCase{ 256u, 256u, 2u, pyramid::Reduction::Max, false, Rgba32f },
        TestCase{ 255u, 97u, 2u, pyramid::Reduction::Max, false, Rgba32f },
        TestCase{ 512u, 512u, 1u, pyramid::Reduction::Min, false, Rgba32f },
        TestCase{ 127u, 33u, 1u, pyramid::Reduction::Min, false, Rgba32f },
        // the ocean's textures: 4 cascades of rgba16f
        TestCase{ 256u, 256u, 4u, pyramid::Reduction::Average, false, Rgba16f },
        TestCase{ 255u, 97u, 2u, pyramid::Reduction::Average, false, Rgba16f },
        TestCase{ 256u, 256u, 1u, pyramid::Reduction::Max, false, Rgba16f },
    };

    int32_t failures = 0;

    {
        ResourceContext resources;
        resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });
        rhi::Device& device = *rhiSystem->GetDevice();

        for (const TestCase& test : k_Cases)
        {
            std::expected<std::unique_ptr<pyramid::MipGenerator>, std::string> generator =
                pyramid::MipGenerator::Create(device, **library, pyramid::MipGeneratorCreateInfo{ test.Reduce, test.SharedF16 });

            if (!generator.has_value())
            {
                std::cerr << std::format("MipGenerator::Create ({}): {}\n", ReductionName(test.Reduce), generator.error());
                ++failures;
                continue;
            }

            if (!(*generator)->HasFastPipeline())
            {
                std::cout << "This device can't run the fast pipeline (full subgroups of 32 with shuffles): general pipeline only\n";
            }

            failures += RunCase(device, resources, **generator, test);
        }
    }

    if (failures != 0)
    {
        std::cerr << std::format("GpuPyramidTest: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << "GpuPyramidTest: every level matches the CPU reference\n";
    return 0;
}
