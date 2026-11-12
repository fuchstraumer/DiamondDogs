#include "BootResult.hpp"
#include "BootTimings.hpp"
#include "CoreStage.hpp"
#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include <cstdio>
#include <print>
#include <span>

namespace
{
    // the cooker links no optional modules yet
    constexpr std::span<const bootstrap::ModuleDescriptor> k_CookerModules{};

    [[nodiscard]] int ReportFailure(const bootstrap::BootResult& failure);
}

int main(int argc, char* argv[])
{
    const std::span<char* const> arguments{ argv, static_cast<size_t>(argc) };
    bootstrap::LaunchStage launch;
    const bootstrap::BootResult launched = launch.Start(arguments, k_CookerModules, bootstrap::Stage::Core);

    if (!launched.Succeeded())
    {
        return ReportFailure(launched);
    }

    bootstrap::CoreStage core;
    const bootstrap::BootResult started = core.Start(launch);

    if (!started.Succeeded())
    {
        return ReportFailure(started);
    }

    if (launch.BootTimingsRequested())
    {
        std::println("{}", bootstrap::FormatBootTimings(launch.Timings(), "no device"));
    }

    std::println("ContentCooker: booted without the RHI. It has no recipes to cook yet.");
    return 0;
}

namespace
{
    int ReportFailure(const bootstrap::BootResult& failure)
    {
        std::println(stderr, "{}", bootstrap::FormatFailure(failure));
        return 1;
    }
}
