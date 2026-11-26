#include "BenchReport.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "DeviceRequirements.hpp"
#include "GpuBuffers.hpp"
#include "GpuTimers.hpp"
#include "Healpix.hpp"
#include "PlanetSession.hpp"
#include "ReferenceSet.hpp"
#include "SpineNeeds.hpp"
#include "StepState.hpp"
#include "timing/MonotonicClock.hpp"
#include <charconv>
#include <cstdio>
#include <memory>
#include <print>
#include <unordered_map>

// PlanetGenBench: measures the GPU time of each pass of one plate step on the PlanetLab reference state.
namespace
{
#if RHI_GPU_TIMERS_ENABLED
#ifdef CMAKE_INTDIR
    constexpr std::string_view k_BuildType{ CMAKE_INTDIR };
#else
    constexpr std::string_view k_BuildType{ "unknown" };
#endif

    struct BenchOptions
    {
        std::filesystem::path Reference;
        std::filesystem::path Json;
        uint32_t Order{ 7u };
        uint32_t Steps{ 200u };
        uint32_t Warmup{ 20u };
        planet::StepTimerDetail Detail{ planet::StepTimerDetail::Passes };
        bool FrameBoundaries{ false };
    };

    [[nodiscard]] bool ParseCount(std::string_view text, uint32_t& value);
    [[nodiscard]] std::optional<BenchOptions> ParseOptions(std::span<char* const> arguments);
#endif
}

int main(int argc, char* argv[])
{
#if !RHI_GPU_TIMERS_ENABLED
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::println(stderr, "PlanetGenBench: GPU timers are compiled out of Release builds. Build RelWithDebInfo");
    return 1;
#else
    const std::span<char* const> arguments{ argv, static_cast<size_t>(argc) };
    const std::optional<BenchOptions> options = ParseOptions(arguments);

    if (!options.has_value())
    {
        return 1;
    }

    const std::optional<planet::ReferenceSet> reference = planet::ReferenceSet::Open(options->Reference);

    if (!reference.has_value())
    {
        return 1;
    }

    std::vector<bootstrap::Need> extraNeeds;
    extraNeeds.append_range(bootstrap::QueryPoolNeeds());

    if (options->FrameBoundaries)
    {
        extraNeeds.append_range(planet::FrameBoundaryNeeds());
    }

    const planet::PlanetSessionConfig sessionConfig
    {
        "PlanetGenBench",
        rhi::ValidationLayers::None,
        planet::ConfigFileMode::Ignore,
        extraNeeds,
    };
    const std::unique_ptr<planet::PlanetSession> session =
        planet::PlanetSession::Start(arguments, sessionConfig);

    if (session == nullptr)
    {
        return 1;
    }

    if (options->FrameBoundaries && !session->HasFrameBoundaries())
    {
        std::println(stderr, "PlanetGenBench: --frame-boundaries needs VK_EXT_frame_boundary, and the device lacks it");
        return 1;
    }

    const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> shaders =
        session->CreateShaders(planet::CrustFlagChoice::BestForDevice, planet::k_StepEntryPoints);

    if (!shaders.has_value())
    {
        return 1;
    }

    const std::string_view flagStorage = planet::ToString((*shaders)->CrustFlags());

    if (options->FrameBoundaries)
    {
        std::println("PlanetGenBench: each step's submit ends a frame (VK_EXT_frame_boundary)");
    }

    if (k_BuildType == "Debug")
    {
        std::println("PlanetGenBench: this is a Debug build; the shaders are the same, but measure in RelWithDebInfo");
    }

    const uint32_t pointCount = static_cast<uint32_t>(planet::HealpixPixelCount(options->Order));
    std::println("PlanetGenBench: {}, {} points (HEALPix order {}), {} plates, crust flags in {}, {} build",
                 session->DeviceName(), pointCount, options->Order, reference->PlateCount(), flagStorage, k_BuildType);
    std::println("{} steps after {} warm-up steps. GPU times in microseconds", options->Steps, options->Warmup);

    std::unordered_map<uint64_t, std::vector<double>> samples;
    std::vector<uint64_t> scopeHashes;
    std::vector<planet::ScopeTimingReport> reports;
    std::vector<double> submitMilliseconds;

    {
        planet::GpuBuffers buffers(session->Resources());
        const planet::StepSize size
        {
            pointCount,
            static_cast<uint32_t>(reference->PlateCount()),
        };

        if (!planet::AddHealpixStepBuffers(buffers, **shaders, *reference, options->Order, (*shaders)->CrustFlags(), true))
        {
            return 1;
        }

        const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetStep>> step =
            planet::PlanetStep::Create(**shaders, buffers, size);

        if (!step.has_value())
        {
            return 1;
        }

        const rhi::GpuTimersCreateInfo timerInfo
        {
            session->Device().GetQueueFamilyIndices().Graphics,
            2u,
            64u,
        };
        rhi::GpuTimers timers(session->Device(), timerInfo);

        if (!timers.Active())
        {
            std::println(stderr, "PlanetGenBench: GPU timers are inactive: {}", timers.InactiveReason());
            return 1;
        }

        const int64_t warmup = static_cast<int64_t>(options->Warmup);
        const int64_t total = warmup + static_cast<int64_t>(options->Steps);

        for (int64_t index = 0; index < total; ++index)
        {
            const foundation::Stopwatch submitTime;
            rhi::CommandBuffer commandBuffer = session->BeginCommands();

            if ((*step)->RecordReset(commandBuffer) != planet::PlanetGpuError::Success)
            {
                session->DiscardCommands(commandBuffer);
                return 1;
            }

            {
                rhi::GpuTimerBlock block(timers, commandBuffer, static_cast<uint64_t>(index));
                (*step)->RecordStep(commandBuffer, block, options->Detail);
            }

            const bool submitted = options->FrameBoundaries
                ? session->SubmitFrameEndAndWait(commandBuffer, static_cast<uint64_t>(index))
                : session->SubmitAndWait(commandBuffer);

            if (!submitted)
            {
                return 1;
            }

            const double milliseconds = submitTime.ElapsedMilliseconds();
            timers.Collect(static_cast<uint64_t>(index) + 1u);
            const std::vector<rhi::GpuTimingBlock> completed = timers.TakeCompleted();

            if (completed.size() != 1u || completed.front().DroppedScopes != 0u)
            {
                std::println(stderr, "PlanetGenBench: step {} collected {} blocks", index, completed.size());
                return 1;
            }

            if (index < warmup)
            {
                continue;
            }

            submitMilliseconds.push_back(milliseconds);

            for (const rhi::GpuTiming& timing : completed.front().Timings)
            {
                samples[timing.Hash].push_back(timing.Nanoseconds * 1.0e-3);

                if (index == warmup)
                {
                    scopeHashes.push_back(timing.Hash);
                    reports.push_back(planet::ScopeTimingReport
                    {
                        std::string{ timers.Name(timing.Hash) },
                        timing.Depth,
                        {},
                    });
                }
            }
        }
    }

    for (int64_t index = 0; index < std::ssize(reports); ++index)
    {
        const size_t row = static_cast<size_t>(index);
        reports[row].Summary = planet::SummarizeSamples(samples[scopeHashes[row]]);
    }

    const planet::TimingSummary submitSummary = planet::SummarizeSamples(submitMilliseconds);
    planet::PrintTimingTable(reports, submitSummary);

    if (!options->Json.empty())
    {
        const planet::BenchMetadata metadata
        {
            session->DeviceName(),
            k_BuildType,
            static_cast<int32_t>(pointCount),
            reference->PlateCount(),
            flagStorage,
            options->Steps,
            options->Detail,
        };
        const std::optional<std::string> jsonLine =
            planet::FormatBenchJsonLine(metadata, reports, submitSummary);

        if (!jsonLine.has_value() || !planet::AppendJsonLine(options->Json, *jsonLine))
        {
            return 1;
        }
    }

    return 0;
#endif
}

namespace
{
#if RHI_GPU_TIMERS_ENABLED
    bool ParseCount(std::string_view text, uint32_t& value)
    {
        const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

    std::optional<BenchOptions> ParseOptions(std::span<char* const> arguments)
    {
        BenchOptions options;

        for (const char* argument : arguments.subspan(1))
        {
            const std::string_view text{ argument };
            bool valid = true;

            if (text.starts_with("--reference="))
            {
                options.Reference = text.substr(12u);
            }
            else if (text == "--dispatches")
            {
                options.Detail = planet::StepTimerDetail::Dispatches;
            }
            else if (text == "--frame-boundaries")
            {
                options.FrameBoundaries = true;
            }
            else if (text.starts_with("--json="))
            {
                options.Json = text.substr(7u);
            }
            else if (text.starts_with("--order="))
            {
                valid = ParseCount(text.substr(8u), options.Order) &&
                        options.Order >= 4u &&
                        options.Order <= 9u;
            }
            else if (text.starts_with("--steps="))
            {
                valid = ParseCount(text.substr(8u), options.Steps);
            }
            else if (text.starts_with("--warmup="))
            {
                valid = ParseCount(text.substr(9u), options.Warmup);
            }
            else
            {
                valid = false;
            }

            if (!valid)
            {
                std::println(stderr, "PlanetGenBench: '{}' is not an option, or its count is not a number", text);
                return std::nullopt;
            }
        }

        if (options.Reference.empty() || options.Steps == 0u)
        {
            std::println(stderr, "usage: PlanetGenBench --reference=<folder> [--order=N] [--steps=N] [--warmup=N] [--dispatches] [--frame-boundaries] [--json=<file>]");
            return std::nullopt;
        }

        return options;
    }
#endif
}
