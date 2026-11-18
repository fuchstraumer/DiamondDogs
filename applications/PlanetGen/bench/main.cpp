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
#include "GpuTimers.hpp"
#include "LaunchStage.hpp"
#include "LoadedBundle.hpp"
#include "ModuleDescriptor.hpp"
#include "PlanetShaders.hpp"
#include "PlanetStep.hpp"
#include "Queue.hpp"
#include "ReferenceSet.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibrary.hpp"
#include "SpineNeeds.hpp"
#include "StepState.hpp"
#include "timing/MonotonicClock.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

// PlanetGenBench: the GPU time of each pass of one plate step, on the reference state of PlanetLab. Each measured step
// is one submit: PlanetStep::RecordReset (not timed), then the passes, each in its own GPU timer scope. Headless and
// with no validation layer, so only the passes are measured. GPU timers exist in Debug and RelWithDebInfo; measure in
// RelWithDebInfo, on an idle GPU.
//
//     PlanetGenBench --reference=<folder> [--steps=N] [--warmup=N] [--dispatches] [--json=<file>]
//
// --json appends one line with the results to the file, so that later runs can be compared with this one.
namespace
{
    constexpr std::string_view k_ReferenceFlag{ "--reference=" };
    constexpr std::string_view k_StepsFlag{ "--steps=" };
    constexpr std::string_view k_WarmupFlag{ "--warmup=" };
    constexpr std::string_view k_JsonFlag{ "--json=" };
    constexpr std::string_view k_DispatchesFlag{ "--dispatches" };
    constexpr uint32_t k_DefaultSteps = 200u;
    constexpr uint32_t k_DefaultWarmup = 20u;
    // one block is recording while the one before it waits for Collect
    constexpr uint32_t k_TimerBlocks = 2u;
    // the step, 6 passes and 12 dispatches, with room to spare
    constexpr uint32_t k_ScopesPerStep = 32u;

#ifdef CMAKE_INTDIR
    constexpr std::string_view k_BuildType{ CMAKE_INTDIR };
#else
    constexpr std::string_view k_BuildType{ "unknown" };
#endif

    constexpr bootstrap::BundleRequest k_Bundles[]
    {
        bootstrap::BundleRequest
        {
            .FileName = "ShaderLibrary.ldmanifest",
            .BuildTreePath = PLANET_GEN_MANIFEST,
            .Profile = rhi::k_SpirvProfileName,
        },
    };

    struct BenchOptions
    {
        std::filesystem::path Reference;
        std::filesystem::path Json;
        uint32_t Steps{ k_DefaultSteps };
        uint32_t Warmup{ k_DefaultWarmup };
        planet::StepTimerDetail Detail{ planet::StepTimerDetail::Passes };
    };

    struct Summary
    {
        double Min{ 0.0 };
        double Median{ 0.0 };
        double P95{ 0.0 };
    };

    // one row of the table: a scope, in the record order of the step, and how deep it is nested
    struct ScopeRow
    {
        uint64_t Hash{ 0u };
        uint32_t Depth{ 0u };
        std::string Name;
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

    [[nodiscard]] std::optional<uint32_t> ParseCount(std::string_view text)
    {
        uint32_t value = 0u;
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);

        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        {
            return std::nullopt;
        }

        return value;
    }

    [[nodiscard]] std::optional<BenchOptions> ParseOptions(std::span<char*> arguments)
    {
        BenchOptions options;

        for (const char* argument : arguments.subspan(1))
        {
            const std::string_view text{ argument };
            std::optional<uint32_t> count;

            if (text.starts_with(k_ReferenceFlag))
            {
                options.Reference = text.substr(k_ReferenceFlag.size());
                continue;
            }

            if (text == k_DispatchesFlag)
            {
                options.Detail = planet::StepTimerDetail::Dispatches;
                continue;
            }

            if (text.starts_with(k_JsonFlag))
            {
                options.Json = text.substr(k_JsonFlag.size());
                continue;
            }

            if (text.starts_with(k_StepsFlag))
            {
                count = ParseCount(text.substr(k_StepsFlag.size()));
                options.Steps = count.value_or(0u);
            }
            else if (text.starts_with(k_WarmupFlag))
            {
                count = ParseCount(text.substr(k_WarmupFlag.size()));
                options.Warmup = count.value_or(0u);
            }

            if (!count.has_value())
            {
                std::println(stderr, "PlanetGenBench: '{}' is not an option, or its count is not a number", text);
                return std::nullopt;
            }
        }

        if (options.Reference.empty() || options.Steps == 0u)
        {
            std::println(stderr, "usage: PlanetGenBench --reference=<folder> [--steps=N] [--warmup=N] [--dispatches] [--json=<file>]");
            return std::nullopt;
        }

        return options;
    }

    [[nodiscard]] Summary Summarize(std::vector<double> samples)
    {
        if (samples.empty())
        {
            return Summary{};
        }

        std::ranges::sort(samples);
        const size_t last = samples.size() - 1u;
        return Summary{ samples.front(), samples[last / 2u], samples[static_cast<size_t>(0.95 * static_cast<double>(last))] };
    }

    // one submit: the reset, then the timed step, and a wait for the fence
    [[nodiscard]] bool SubmitStep(rhi::Device& device, rhi::CommandPool& pool, const planet::PlanetStep& step, rhi::GpuTimers& timers,
                                  planet::StepTimerDetail detail, uint64_t submission)
    {
        RhiAssert(pool.Reset());
        rhi::CommandBuffer commandBuffer(pool.GetCommandBuffer(0));
        RhiAssert(commandBuffer.Begin());

        if (step.RecordReset(commandBuffer) != planet::PlanetGpuError::Success)
        {
            return false;
        }

        {
            rhi::GpuTimerBlock block(timers, commandBuffer, submission);
            step.RecordStep(commandBuffer, block, detail);
        }

        RhiAssert(commandBuffer.End());

        const rhi::CommandBufferHandle handle = commandBuffer.Handle();
        rhi::Fence fence(device.Handle());
        RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, fence.Handle()));

        if (!fence.Wait())
        {
            std::println(stderr, "PlanetGenBench: a step did not finish");
            return false;
        }

        return true;
    }

    [[nodiscard]] std::string JsonSummary(const Summary& summary)
    {
        return std::format("{{\"min\": {:.3f}, \"median\": {:.3f}, \"p95\": {:.3f}}}", summary.Min, summary.Median, summary.P95);
    }
}

int main(int argc, char* argv[])
{
#if !RHI_GPU_TIMERS_ENABLED
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::println(stderr, "PlanetGenBench: GPU timers are compiled out of Release builds. Build RelWithDebInfo");
    return 1;
#else
    const std::optional<BenchOptions> options = ParseOptions(std::span{ argv, static_cast<size_t>(argc) });

    if (!options.has_value())
    {
        return 1;
    }

    std::optional<planet::ReferenceSet> reference = planet::ReferenceSet::Open(options->Reference);

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
    needs.append_range(bootstrap::QueryPoolNeeds());
    const bootstrap::DeviceRequirements requirements = bootstrap::GatherDeviceRequirements(content, needs);
    // the validation layer instruments and slows the passes it would be measuring
    const bootstrap::DeviceConfig config
    {
        .ApiVersion = rhi::ApiVersion::Latest,
        .Validation = rhi::ValidationLayers::None,
        .ApplicationName = "PlanetGenBench",
    };
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

    planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders =
        planet::PlanetShaders::Create(device, **library, deviceStage.Filter(), planet::CrustFlagChoice::BestForDevice, planet::k_StepEntryPoints);

    if (!shaders.has_value())
    {
        return 1;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.GetPhysicalDevice().As<VkPhysicalDevice>(), &properties);
    const std::string_view flagStorage = planet::ToString((*shaders)->CrustFlags());

    if (k_BuildType == "Debug")
    {
        std::println("PlanetGenBench: this is a Debug build; the shaders are the same, but measure in RelWithDebInfo");
    }

    std::println("PlanetGenBench: {}, {} points, {} plates, crust flags in {}, {} build", properties.deviceName, reference->PointCount(),
                 reference->PlateCount(), flagStorage, k_BuildType);
    std::println("{} steps after {} warm-up steps. GPU times in microseconds", options->Steps, options->Warmup);

    std::unordered_map<uint64_t, std::vector<double>> samples;
    std::vector<ScopeRow> rows;
    std::vector<double> submitMilliseconds;

    {
        planet::GpuBuffers buffers(gpuStage.Resources());

        if (!planet::AddStepBuffers(buffers, **shaders, *reference, planet::StepReadback::None))
        {
            return 1;
        }

        const planet::StepSize size{ static_cast<uint32_t>(reference->PointCount()), static_cast<uint32_t>(reference->PlateCount()) };
        planet::PlanetGpuResult<std::unique_ptr<planet::PlanetStep>> step = planet::PlanetStep::Create(**shaders, buffers, size);

        if (!step.has_value())
        {
            return 1;
        }

        rhi::GpuTimers timers(device, rhi::GpuTimersCreateInfo{ device.GetQueueFamilyIndices().Graphics, k_TimerBlocks, k_ScopesPerStep });

        if (!timers.Active())
        {
            std::println(stderr, "PlanetGenBench: GPU timers are inactive: {}", timers.InactiveReason());
            return 1;
        }

        rhi::CommandPool pool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
        RhiAssert(pool.AllocateCommandBuffers(1));
        const uint32_t total = options->Warmup + options->Steps;

        for (uint32_t index = 0; index < total; ++index)
        {
            const foundation::Stopwatch submitTime;

            if (!SubmitStep(device, pool, **step, timers, options->Detail, index))
            {
                return 1;
            }

            const double milliseconds = submitTime.ElapsedMilliseconds();

            // the fence passed, so the block of this step finished
            timers.Collect(index + 1u);
            const std::vector<rhi::GpuTimingBlock> completed = timers.TakeCompleted();

            if (completed.size() != 1u || completed.front().DroppedScopes != 0u)
            {
                std::println(stderr, "PlanetGenBench: step {} collected {} blocks", index, completed.size());
                return 1;
            }

            if (index < options->Warmup)
            {
                continue;
            }

            submitMilliseconds.push_back(milliseconds);

            for (const rhi::GpuTiming& timing : completed.front().Timings)
            {
                samples[timing.Hash].push_back(timing.Nanoseconds * 1.0e-3);

                // every step records the same scopes, so the first measured step gives the rows
                if (index == options->Warmup)
                {
                    rows.push_back(ScopeRow{ timing.Hash, timing.Depth, std::string{ timers.Name(timing.Hash) } });
                }
            }
        }
    }

    const std::string_view detail = options->Detail == planet::StepTimerDetail::Dispatches ? "dispatches" : "passes";
    std::string json = std::format("{{\"device\": \"{}\", \"build\": \"{}\", \"points\": {}, \"plates\": {}, \"flags\": \"{}\", \"steps\": {}, \"detail\": \"{}\", \"scopes\": {{",
                                   properties.deviceName, k_BuildType, reference->PointCount(), reference->PlateCount(), flagStorage, options->Steps, detail);
    std::println("{:<26} {:>10} {:>10} {:>10}", "scope", "min", "median", "p95");

    for (size_t index = 0; index < rows.size(); ++index)
    {
        const ScopeRow& row = rows[index];
        const Summary summary = Summarize(samples[row.Hash]);
        const std::string label = std::string(2u * row.Depth, ' ') + row.Name;
        std::println("{:<26} {:>10.1f} {:>10.1f} {:>10.1f}", label, summary.Min, summary.Median, summary.P95);
        json += std::format("{}\"{}\": {}", index == 0u ? "" : ", ", row.Name, JsonSummary(summary));
    }

    const Summary submit = Summarize(submitMilliseconds);
    std::println("CPU, submit to fence, ms: min {:.3f}, median {:.3f}, p95 {:.3f}", submit.Min, submit.Median, submit.P95);
    json += std::format("}}, \"submit_ms\": {}}}", JsonSummary(submit));

    if (!options->Json.empty())
    {
        std::ofstream file(options->Json, std::ios::app);

        if (!file)
        {
            std::println(stderr, "PlanetGenBench: cannot open {}", options->Json.string());
            return 1;
        }

        file << json << "\n";
        std::println("appended the results to {}", options->Json.string());
    }

    return 0;
#endif
}
