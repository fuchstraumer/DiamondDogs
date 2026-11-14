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
#include "GpuStage.hpp"
#include "GpuTimers.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "OceanSimulation.hpp"
#include "SpineNeeds.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "SeaState.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

// OceanFftBenchmark: GPU time per compute pass of the ocean simulation, for every IFFT size, radix, wave-op setting
// and path (fused or unfused kernels). Headless, with no validation layer, so nothing but the passes is measured. Needs GPU timers, so it only
// measures in Debug and RelWithDebInfo; use RelWithDebInfo, on an otherwise idle GPU.
namespace
{
    constexpr uint32_t k_TicksPerBatch = 64u;
    constexpr uint32_t k_WarmupBatches = 1u;
    constexpr uint32_t k_MeasuredBatches = 4u;
    constexpr uint32_t k_ScopesPerTick = 8u;
    constexpr float k_TickSeconds = 1.0f / 60.0f;
    // the fused path records the two fused scopes, the unfused path the three separate ones. A scope that a path does
    // not record prints a dash
    constexpr std::array<std::string_view, 7> k_Scopes
    {
        "Tick", "Tick + IFFT rows", "IFFT columns + Resolve", "Spectrum tick", "IFFT", "Resolve", "Mips"
    };

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "OceanFft.ldmanifest",
            .BuildTreePath = OCEAN_FFT_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    // the bundle lists UniformAndStorageBuffer16BitAccess, which implicitly declares this one, and the cook does not
    // list implied capabilities yet
    constexpr bootstrap::Need k_StorageBuffer16BitNeed
    {
        .Name = "StorageBuffer16BitAccess",
        .Kind = bootstrap::NeedKind::Capability,
        .Level = bootstrap::NeedLevel::Required,
    };

    // prints a boot failure to stderr
    [[nodiscard]] bool Booted(const bootstrap::BootResult& result)
    {
        if (!result.Succeeded())
        {
            std::cerr << bootstrap::FormatFailure(result) << "\n";
        }

        return result.Succeeded();
    }

    struct Percentiles
    {
        double Median{ 0.0 };
        double P95{ 0.0 };
    };

    Percentiles Measure(std::vector<double> samples)
    {
        if (samples.empty())
        {
            return Percentiles{};
        }

        std::ranges::sort(samples);
        const auto at = [&samples](double fraction) { return samples[static_cast<size_t>(fraction * static_cast<double>(samples.size() - 1u))]; };
        return Percentiles{ at(0.5), at(0.95) };
    }

    void Submit(rhi::Device& device, rhi::CommandPool& pool, const auto& record)
    {
        RhiAssert(pool.Reset());
        rhi::CommandBuffer commandBuffer(pool.GetCommandBuffer(0));
        RhiAssert(commandBuffer.Begin());
        record(commandBuffer);
        RhiAssert(commandBuffer.End());
        const rhi::CommandBufferHandle handle = commandBuffer.Handle();
        rhi::Fence fence(device.Handle());
        RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()));

        if (!fence.Wait())
        {
            throw std::runtime_error("OceanFftBenchmark: a batch did not finish");
        }
    }

    // nanosecond samples per scope name, over every measured tick of one configuration
    std::unordered_map<uint64_t, std::vector<double>> RunConfiguration(rhi::Device& device, ResourceContext& resources, const rhi::ShaderLibrary& library,
                                                                       rhi::CommandPool& pool, const ocean::OceanSimulationSettings& settings)
    {
        auto created = ocean::OceanSimulation::Create(device, resources, library, settings);

        if (!created.has_value())
        {
            throw std::runtime_error(std::format("OceanFftBenchmark: {}", created.error()));
        }

        const ocean::OceanSimulation& simulation = **created;
        rhi::GpuTimers timers(device, rhi::GpuTimersCreateInfo{ device.GetQueueFamilyIndices().Graphics, k_TicksPerBatch, k_ScopesPerTick });
        std::unordered_map<uint64_t, std::vector<double>> samples;
        float time = 0.0f;

        for (uint32_t batch = 0; batch < k_WarmupBatches + k_MeasuredBatches; ++batch)
        {
            Submit(device, pool, [&](rhi::CommandBuffer& command_buffer)
            {
                if (batch == 0u)
                {
                    simulation.RecordInitialize(command_buffer);
                }

                for (uint32_t tick = 0; tick < k_TicksPerBatch; ++tick)
                {
                    // the next tick rewrites what this one read and wrote, and the barrier keeps the ticks' scopes apart
                    const rhi::GlobalBarrier betweenTicks
                    {
                        ocean::OceanSimulation::k_TickStages,
                        ocean::OceanSimulation::k_TickWriteAccess,
                        ocean::OceanSimulation::k_TickStages,
                        ocean::OceanSimulation::k_TickWriteAccess | rhi::Access::ShaderStorageRead | rhi::Access::ShaderSampledRead
                    };
                    command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &betweenTicks, 1 }, {}, {} });

                    rhi::GpuTimerBlock block(timers, command_buffer, batch);
                    const rhi::ScopedGpuTimer whole(block, "Tick");
                    simulation.RecordTick(command_buffer, block, time);
                    time += k_TickSeconds;
                }
            });

            // the fence passed, so every block of this batch finished
            timers.Collect(batch + 1u);
            const std::vector<rhi::GpuTimingBlock> completed = timers.TakeCompleted();

            if (completed.size() != k_TicksPerBatch || timers.DroppedBlocks() != 0u)
            {
                throw std::runtime_error(std::format("OceanFftBenchmark: collected {} of {} ticks ({} dropped)", completed.size(), k_TicksPerBatch, timers.DroppedBlocks()));
            }

            if (batch < k_WarmupBatches)
            {
                continue;
            }

            for (const rhi::GpuTimingBlock& tick : completed)
            {
                for (const rhi::GpuTiming& timing : tick.Timings)
                {
                    samples[timing.Hash].push_back(timing.Nanoseconds);
                }
            }
        }

        return samples;
    }
}

int main(int argc, char* argv[])
{
#if !RHI_GPU_TIMERS_ENABLED
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::cerr << "OceanFftBenchmark: GPU timers are compiled out of Release builds. Build RelWithDebInfo\n";
    return 1;
#else
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
    needs.append_range(bootstrap::QueryPoolNeeds());
    needs.append_range(bootstrap::FeatureNeeds(ocean::k_WaveOpFeatures, bootstrap::NeedLevel::Wanted));
    needs.push_back(k_StorageBuffer16BitNeed);

    const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(content, needs);
    // the validation layer instruments and slows the passes it would be measuring
    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::None,
        .ApplicationName = "OceanFftBenchmark",
    };
    bootstrap::DeviceStage deviceStage;

    if (!Booted(deviceStage.StartHeadless(content, requirements, config)))
    {
        return 1;
    }

    bootstrap::GpuStage gpu;

    if (!Booted(gpu.StartHeadless(deviceStage)))
    {
        return 1;
    }

    rhi::Device& device = *deviceStage.Rhi().GetDevice();
    // the content stage found the bundle, so the library opens that same file
    const std::filesystem::path& bundlePath = content.Bundles().front().Path;
    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(bundlePath, rhi::k_SpirvProfileName);

    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", bundlePath.string(), rhi::ToString(library.error()));
        return 1;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.GetPhysicalDevice().As<VkPhysicalDevice>(), &properties);
    const uint32_t waveSize = ocean::PickWaveSize(device);
    std::cout << std::format("{}, timestamp period {} ns. {} ticks per configuration after {} warm-up ticks. Times in microseconds, median / p95\n",
        properties.deviceName, properties.limits.timestampPeriod, k_TicksPerBatch * k_MeasuredBatches, k_TicksPerBatch * k_WarmupBatches);

    std::string header = std::format("{:>5} {:>7} {:>5} {:>8}", "size", "radix", "wave", "path");

    for (const std::string_view scope : k_Scopes)
    {
        header += std::format(" | {:>15}", scope);
    }

    std::cout << header << "\n";

    {
        ResourceContext& resources = gpu.Resources();

        rhi::CommandPool pool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
        RhiAssert(pool.AllocateCommandBuffers(1));
        std::vector<uint32_t> waveSizes{ 0u };

        if (waveSize != 0u)
        {
            waveSizes.push_back(waveSize);
        }

        for (const uint32_t size : { 128u, 256u, 512u })
        {
            for (const uint32_t wave : waveSizes)
            {
                for (const bool fused : { true, false })
                {
                    ocean::OceanSimulationSettings settings{};
                    settings.Size = size;
                    settings.WaveSize = wave;
                    settings.FusedKernels = fused;
                    // the demo's default sea, swell included
                    settings.Spectrum = ocean::ApplySeaState(ocean::SeaState{}, settings.Spectrum).Spectrum;

                    const std::unordered_map<uint64_t, std::vector<double>> samples = RunConfiguration(device, resources, **library, pool, settings);
                    std::string line = std::format("{:>5} {:>7} {:>5} {:>8}", size, "4", wave == 0u ? "off" : std::to_string(wave), fused ? "fused" : "unfused");

                    for (const std::string_view scope : k_Scopes)
                    {
                        const auto found = samples.find(rhi::HashGpuTimerName(scope));

                        if (found == samples.end())
                        {
                            line += std::format(" | {:>15}", "-");
                            continue;
                        }

                        const Percentiles measured = Measure(found->second);
                        line += std::format(" | {:>7.1f} {:>7.1f}", measured.Median * 1e-3, measured.P95 * 1e-3);
                    }

                    std::cout << line << std::endl;
                }
            }
        }
    }

    return 0;
#endif
}
