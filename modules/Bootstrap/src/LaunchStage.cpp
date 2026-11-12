#include "LaunchStage.hpp"
#include "ModuleDescriptor.hpp"
#include "ModuleSelection.hpp"
#include "utility/NearestNames.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace
{
    using json = nlohmann::json;

    constexpr int32_t k_MaxSuggestions = 3;
    constexpr std::string_view k_ConfigFlag{ "--config=" };
    constexpr std::string_view k_ProfileFlag{ "--profile=" };
    constexpr std::string_view k_BootTimingsFlag{ "--boot-timings" };
    constexpr std::string_view k_ProfilesKey{ "Profiles" };

    [[nodiscard]] bootstrap::BootResult LaunchFailure(bootstrap::BootResultCode code,
                                                      std::string_view subject,
                                                      std::string_view source);
    // the value after `flag`, or a MalformedArgument failure when it is empty or the flag repeats
    [[nodiscard]] bootstrap::BootResult ReadFlagValue(std::string_view argument,
                                                      std::string_view flag,
                                                      std::string_view& value);
    [[nodiscard]] std::filesystem::path ExecutableDirectory();
    [[nodiscard]] bootstrap::BootResult ReadProfileNames(const json& document,
                                                         std::string_view profile_name,
                                                         std::vector<std::string>& profile_modules);
    [[nodiscard]] std::vector<std::string_view> ProfileNamesIn(const json& profiles);
}

namespace bootstrap
{
    LaunchStage::LaunchStage() noexcept = default;

    LaunchStage::~LaunchStage() = default;

    BootResult LaunchStage::Start(std::span<char* const> _arguments,
                                  std::span<const ModuleDescriptor> linked_modules,
                                  Stage last_stage)
    {
        timings.Clear();

        const ScopedBootSpan stageSpan(timings, Stage::Launch, BootSpanKind::Stage, ToString(Stage::Launch));

        arguments.assign(_arguments.begin(), _arguments.end());
        configArgument = std::string_view{};
        profileName = std::string_view{};
        config = ConfigDocument{};
        selected.clear();
        executableDirectory = ExecutableDirectory();
        lastStage = last_stage;
        bootTimingsRequested = false;

        BootResult result = readArguments();

        if (!result.Succeeded())
        {
            return result;
        }

        result = readConfig();

        if (!result.Succeeded())
        {
            return result;
        }

        return selectModules(linked_modules);
    }

    std::span<const std::string_view> LaunchStage::Arguments() const noexcept
    {
        return arguments;
    }

    const ConfigDocument& LaunchStage::Config() const noexcept
    {
        return config;
    }

    std::string_view LaunchStage::ProfileName() const noexcept
    {
        return profileName;
    }

    std::span<const ModuleDescriptor* const> LaunchStage::Selected() const noexcept
    {
        return selected;
    }

    Stage LaunchStage::LastStage() const noexcept
    {
        return lastStage;
    }

    bool LaunchStage::BootTimingsRequested() const noexcept
    {
        return bootTimingsRequested;
    }

    std::filesystem::path LaunchStage::FindBeside(std::string_view file_name, std::string_view build_tree_path) const
    {
        const std::filesystem::path beside = executableDirectory / file_name;
        std::error_code error;

        if (!executableDirectory.empty() && std::filesystem::exists(beside, error))
        {
            return beside;
        }

        return std::filesystem::path{ build_tree_path };
    }

    BootTimings& LaunchStage::Timings() const noexcept
    {
        return timings;
    }

    BootResult LaunchStage::readArguments()
    {
        // the first argument is the executable path
        for (const std::string_view argument : arguments | std::views::drop(1))
        {
            BootResult result = SuccessIn(Stage::Launch);

            if (argument.starts_with(k_ConfigFlag))
            {
                result = ReadFlagValue(argument, k_ConfigFlag, configArgument);
            }
            else if (argument.starts_with(k_ProfileFlag))
            {
                result = ReadFlagValue(argument, k_ProfileFlag, profileName);
            }
            else if (argument == k_BootTimingsFlag)
            {
                bootTimingsRequested = true;
            }

            if (!result.Succeeded())
            {
                return result;
            }
        }

        return SuccessIn(Stage::Launch);
    }

    BootResult LaunchStage::readConfig()
    {
        if (configArgument.empty())
        {
            return SuccessIn(Stage::Launch);
        }

        config.Path = std::filesystem::path{ configArgument };

        std::ifstream file(config.Path, std::ios::binary);

        if (!file)
        {
            return LaunchFailure(BootResultCode::ConfigUnreadable, config.Path.string(), k_ConfigFlag);
        }

        std::ostringstream text;

        text << file.rdbuf();
        config.Text = std::move(text).str();

        const json document = json::parse(config.Text, nullptr, false);

        if (document.is_discarded() || !document.is_object())
        {
            return LaunchFailure(BootResultCode::ConfigInvalid, config.Path.string(), "not a JSON object");
        }

        return SuccessIn(Stage::Launch);
    }

    BootResult LaunchStage::selectModules(std::span<const ModuleDescriptor> linked_modules)
    {
        BootResult result = CheckLinkedModules(linked_modules);

        if (!result.Succeeded())
        {
            return result;
        }

        if (profileName.empty())
        {
            const std::vector<std::string_view> everyName =
                linked_modules | std::views::transform(&ModuleDescriptor::Name)
                               | std::ranges::to<std::vector<std::string_view>>();

            return SelectModules(linked_modules, everyName, "every linked module", lastStage, selected);
        }

        if (config.Path.empty())
        {
            return LaunchFailure(BootResultCode::ProfileWithoutConfig, profileName, k_ProfileFlag);
        }

        // the config text was parsed once in readConfig; a second parse here keeps the JSON type out of the header
        const json document = json::parse(config.Text, nullptr, false);
        std::vector<std::string> profileModules;

        result = ReadProfileNames(document, profileName, profileModules);

        if (!result.Succeeded())
        {
            return result;
        }

        const std::vector<std::string_view> wantedNames = profileModules | std::ranges::to<std::vector<std::string_view>>();
        const std::string wantedSource = std::format("profile {}", profileName);

        return SelectModules(linked_modules, wantedNames, wantedSource, lastStage, selected);
    }
}

namespace
{
    bootstrap::BootResult LaunchFailure(bootstrap::BootResultCode code, std::string_view subject, std::string_view source)
    {
        return bootstrap::BootResult
        {
            .Code = code,
            .FailedStage = bootstrap::Stage::Launch,
            .Subject = std::string{ subject },
            .Source = std::string{ source },
        };
    }

    bootstrap::BootResult ReadFlagValue(std::string_view argument, std::string_view flag, std::string_view& value)
    {
        const std::string_view flagValue = argument.substr(flag.size());

        if (flagValue.empty())
        {
            return LaunchFailure(bootstrap::BootResultCode::MalformedArgument, argument, "the value is empty");
        }

        if (!value.empty())
        {
            return LaunchFailure(bootstrap::BootResultCode::MalformedArgument, argument, "the flag repeats");
        }

        value = flagValue;
        return bootstrap::SuccessIn(bootstrap::Stage::Launch);
    }

    std::filesystem::path ExecutableDirectory()
    {
#ifdef _WIN32
        std::array<wchar_t, 32768> modulePath{};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));

        if (length == 0u)
        {
            return std::filesystem::path{};
        }

        const std::wstring_view path{ modulePath.data(), length };

        return std::filesystem::path{ path }.parent_path();
#else
        return std::filesystem::path{};
#endif
    }

    bootstrap::BootResult ReadProfileNames(const json& document,
                                           std::string_view profile_name,
                                           std::vector<std::string>& profile_modules)
    {
        const auto profiles = document.find(k_ProfilesKey);

        if (profiles == document.end())
        {
            return LaunchFailure(bootstrap::BootResultCode::UnknownProfile, profile_name, "the config has no Profiles");
        }

        if (!profiles->is_object())
        {
            return LaunchFailure(bootstrap::BootResultCode::ConfigInvalid, k_ProfilesKey, "Profiles is not an object");
        }

        const auto profile = profiles->find(profile_name);

        if (profile == profiles->end())
        {
            const std::vector<std::string_view> profileNames = ProfileNamesIn(*profiles);
            bootstrap::BootResult failure = LaunchFailure(bootstrap::BootResultCode::UnknownProfile,
                                                          profile_name,
                                                          k_ProfilesKey);

            failure.Suggestions = foundation::NearestNames(profile_name, profileNames, k_MaxSuggestions);
            return failure;
        }

        const std::string profilePath = std::format("Profiles.{}", profile_name);

        if (!profile->is_array())
        {
            return LaunchFailure(bootstrap::BootResultCode::ConfigInvalid, profilePath, "the profile is not an array");
        }

        for (const json& entry : *profile)
        {
            if (!entry.is_string())
            {
                return LaunchFailure(bootstrap::BootResultCode::ConfigInvalid,
                                     profilePath,
                                     "a module name is not a string");
            }

            profile_modules.push_back(entry.get<std::string>());
        }

        return bootstrap::SuccessIn(bootstrap::Stage::Launch);
    }

    std::vector<std::string_view> ProfileNamesIn(const json& profiles)
    {
        std::vector<std::string_view> names;

        for (auto entry = profiles.begin(); entry != profiles.end(); ++entry)
        {
            names.emplace_back(entry.key());
        }

        return names;
    }
}
