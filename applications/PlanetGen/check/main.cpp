#include "CheckHarness.hpp"
#include "StepChecks.hpp"
#include "PlanetGpuErrors.hpp"
#include "PlanetSession.hpp"
#include "PlanetShaders.hpp"
#include "ReferenceSet.hpp"
#include "RhiFlags.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

// PlanetGenCheck: runs the PlanetLab Slang passes and compares each result with PlanetLab's reference files.
namespace
{
    struct CheckOptions
    {
        std::filesystem::path Reference;
        std::vector<planet::NamedCheck> Checks;
    };

    [[nodiscard]] std::optional<CheckOptions> ParseOptions(std::span<char* const> arguments);
}

int main(int argc, char* argv[])
{
    const std::span<char* const> arguments{ argv, static_cast<size_t>(argc) };
    const std::optional<CheckOptions> options = ParseOptions(arguments);

    if (!options.has_value())
    {
        return 1;
    }

    const std::optional<planet::ReferenceSet> reference = planet::ReferenceSet::Open(options->Reference);

    if (!reference.has_value())
    {
        return 1;
    }

    const std::unique_ptr<planet::PlanetSession> session = planet::PlanetSession::Start(
        arguments, { "PlanetGenCheck", rhi::ValidationLayers::Full, planet::ConfigFileMode::Read, {} });

    if (session == nullptr)
    {
        return 1;
    }

    const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> preferred =
        session->CreateShaders(planet::CrustFlagChoice::BestForDevice, planet::AllCheckEntryPoints());

    if (!preferred.has_value())
    {
        std::println(stderr, "the device runs no variant of {}", planet::k_PlanetModuleName);
        return 1;
    }

    std::println("PlanetGenCheck: {} points, {} plates, time {} My", reference->PointCount(), reference->PlateCount(), reference->Time());
    std::println("the device prefers crust flags in {}", planet::ToString((*preferred)->CrustFlags()));
    const std::span<const planet::NamedCheck> checksToRun = options->Checks.empty() ? planet::AllChecks() : options->Checks;
    constexpr planet::CrustFlagChoice k_Storages[]{ planet::CrustFlagChoice::Bytes, planet::CrustFlagChoice::Words };
    bool passed = true;

    for (const planet::CrustFlagChoice storage : k_Storages)
    {
        const std::string_view storageName = storage == planet::CrustFlagChoice::Bytes ? "bytes" : "words";
        const planet::PlanetGpuResult<std::unique_ptr<planet::PlanetShaders>> probe =
            session->CreateShaders(storage, planet::AllCheckEntryPoints());

        if (!probe.has_value() && probe.error() == planet::PlanetGpuError::NoVariantFitsDevice)
        {
            std::println("crust flags in {}: SKIP, the device cannot run this variant", storageName);
            continue;
        }

        if (!probe.has_value())
        {
            return 1;
        }

        std::println("crust flags in {}:", storageName);
        planet::CheckContext context{ *session, *reference, storage };

        for (const planet::NamedCheck& check : checksToRun)
        {
            const bool checkPassed = check.Run(context);
            std::println("{} [{}]: {}", check.Name, storageName, checkPassed ? "PASS" : "FAIL");
            passed = passed && checkPassed;
        }
    }

    return passed ? 0 : 1;
}

namespace
{
    std::optional<CheckOptions> ParseOptions(std::span<char* const> arguments)
    {
        constexpr std::string_view k_ReferenceFlag{ "--reference=" };
        constexpr std::string_view k_ChecksFlag{ "--checks=" };
        const std::span<const planet::NamedCheck> allChecks = planet::AllChecks();
        CheckOptions options;

        for (const char* argument : arguments.subspan(1))
        {
            const std::string_view text{ argument };

            if (text.starts_with(k_ReferenceFlag))
            {
                options.Reference = text.substr(k_ReferenceFlag.size());
            }
            else if (text.starts_with(k_ChecksFlag))
            {
                std::string_view list = text.substr(k_ChecksFlag.size());

                while (!list.empty())
                {
                    const size_t comma = list.find(',');
                    const std::string_view name = list.substr(0u, comma);
                    const auto found = std::ranges::find(allChecks, name, &planet::NamedCheck::Name);

                    if (found == allChecks.end())
                    {
                        std::println(stderr, "unknown check '{}'", name);
                        return std::nullopt;
                    }

                    options.Checks.push_back(*found);
                    list.remove_prefix(comma == std::string_view::npos ? list.size() : comma + 1u);
                }
            }
        }

        if (options.Reference.empty())
        {
            std::println(stderr, "usage: PlanetGenCheck --reference=<folder> [--checks=map,owners,front,crust,live,distance,uplift,torque,chain]");
            return std::nullopt;
        }

        return options;
    }
}
