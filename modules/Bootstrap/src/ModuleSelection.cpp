#include "ModuleSelection.hpp"
#include "ModuleDescriptor.hpp"
#include "utility/NearestNames.hpp"
#include <algorithm>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>

namespace
{
    constexpr int32_t k_MaxSuggestions = 3;

    [[nodiscard]] bootstrap::BootResult LaunchFailure(bootstrap::BootResultCode code,
                                                      std::string_view module,
                                                      std::string_view subject,
                                                      std::string_view source);
    [[nodiscard]] std::vector<std::string_view> LinkedNames(std::span<const bootstrap::ModuleDescriptor> linked_modules);
    // -1 when no descriptor has the name
    [[nodiscard]] int64_t FindLinkedIndex(std::span<const bootstrap::ModuleDescriptor> linked_modules,
                                          std::string_view name) noexcept;
    [[nodiscard]] int64_t FindSelectedIndex(std::span<const bootstrap::ModuleDescriptor* const> selected,
                                            std::string_view name) noexcept;
    [[nodiscard]] bootstrap::BootResult CheckWantedNames(std::span<const bootstrap::ModuleDescriptor> linked_modules,
                                                         std::span<const std::string_view> wanted_names,
                                                         std::string_view wanted_source);
    [[nodiscard]] bootstrap::BootResult CheckLastStage(const bootstrap::ModuleDescriptor& descriptor,
                                                       bootstrap::Stage last_stage);
    [[nodiscard]] bootstrap::BootResult CheckDependency(std::span<const bootstrap::ModuleDescriptor> linked_modules,
                                                        std::span<const bootstrap::ModuleDescriptor* const> selected,
                                                        int64_t dependent_index,
                                                        std::string_view dependency_name,
                                                        std::string_view wanted_source);
}

namespace bootstrap
{
    Stage StartStageOf(const ModuleDescriptor& descriptor) noexcept
    {
        if (descriptor.StartCore != nullptr)
        {
            return Stage::Core;
        }

        return Stage::Gpu;
    }

    BootResult CheckLinkedModules(std::span<const ModuleDescriptor> linked_modules)
    {
        for (auto&& [index, descriptor] : std::views::enumerate(linked_modules))
        {
            if (descriptor.Name.empty())
            {
                const std::string subject = std::format("linked module {}", index);

                return LaunchFailure(BootResultCode::InvalidDescriptor, {}, subject, "the descriptor has no name");
            }

            const bool startsInCore = descriptor.StartCore != nullptr;
            const bool startsInGpu = descriptor.StartGpu != nullptr;

            if (startsInCore == startsInGpu)
            {
                const std::string_view reason = startsInCore ? "both StartCore and StartGpu are set"
                                                             : "neither StartCore nor StartGpu is set";

                return LaunchFailure(BootResultCode::InvalidDescriptor, descriptor.Name, descriptor.Name, reason);
            }

            const std::span<const ModuleDescriptor> earlier = linked_modules.first(static_cast<size_t>(index));

            if (FindLinkedIndex(earlier, descriptor.Name) != -1)
            {
                return LaunchFailure(BootResultCode::DuplicateModule,
                                     descriptor.Name,
                                     descriptor.Name,
                                     "the linked module list");
            }
        }

        return SuccessIn(Stage::Launch);
    }

    BootResult SelectModules(std::span<const ModuleDescriptor> linked_modules,
                             std::span<const std::string_view> wanted_names,
                             std::string_view wanted_source,
                             Stage last_stage,
                             std::vector<const ModuleDescriptor*>& selected)
    {
        selected.clear();

        BootResult checked = CheckWantedNames(linked_modules, wanted_names, wanted_source);

        if (!checked.Succeeded())
        {
            return checked;
        }

        for (const ModuleDescriptor& descriptor : linked_modules)
        {
            if (std::ranges::contains(wanted_names, descriptor.Name))
            {
                selected.push_back(&descriptor);
            }
        }

        for (auto&& [index, descriptor] : std::views::enumerate(selected))
        {
            checked = CheckLastStage(*descriptor, last_stage);

            if (!checked.Succeeded())
            {
                return checked;
            }

            for (const std::string_view dependencyName : descriptor->DependsOn)
            {
                checked = CheckDependency(linked_modules, selected, index, dependencyName, wanted_source);

                if (!checked.Succeeded())
                {
                    return checked;
                }
            }
        }

        return SuccessIn(Stage::Launch);
    }
}

namespace
{
    bootstrap::BootResult LaunchFailure(bootstrap::BootResultCode code,
                                        std::string_view module,
                                        std::string_view subject,
                                        std::string_view source)
    {
        return bootstrap::BootResult
        {
            .Code = code,
            .FailedStage = bootstrap::Stage::Launch,
            .Module = module,
            .Subject = std::string{ subject },
            .Source = std::string{ source },
        };
    }

    std::vector<std::string_view> LinkedNames(std::span<const bootstrap::ModuleDescriptor> linked_modules)
    {
        return linked_modules | std::views::transform(&bootstrap::ModuleDescriptor::Name)
                              | std::ranges::to<std::vector<std::string_view>>();
    }

    int64_t FindLinkedIndex(std::span<const bootstrap::ModuleDescriptor> linked_modules, std::string_view name) noexcept
    {
        const auto found = std::ranges::find(linked_modules, name, &bootstrap::ModuleDescriptor::Name);

        if (found == linked_modules.end())
        {
            return -1;
        }

        return std::distance(linked_modules.begin(), found);
    }

    int64_t FindSelectedIndex(std::span<const bootstrap::ModuleDescriptor* const> selected,
                              std::string_view name) noexcept
    {
        for (auto&& [index, descriptor] : std::views::enumerate(selected))
        {
            if (descriptor->Name == name)
            {
                return index;
            }
        }

        return -1;
    }

    bootstrap::BootResult CheckWantedNames(std::span<const bootstrap::ModuleDescriptor> linked_modules,
                                           std::span<const std::string_view> wanted_names,
                                           std::string_view wanted_source)
    {
        for (auto&& [index, name] : std::views::enumerate(wanted_names))
        {
            if (FindLinkedIndex(linked_modules, name) == -1)
            {
                const std::vector<std::string_view> linkedNames = LinkedNames(linked_modules);
                bootstrap::BootResult failure = LaunchFailure(bootstrap::BootResultCode::UnknownModule,
                                                              {},
                                                              name,
                                                              wanted_source);

                failure.Suggestions = foundation::NearestNames(name, linkedNames, k_MaxSuggestions);
                return failure;
            }

            const std::span<const std::string_view> earlier = wanted_names.first(static_cast<size_t>(index));

            if (std::ranges::contains(earlier, name))
            {
                return LaunchFailure(bootstrap::BootResultCode::DuplicateModule, {}, name, wanted_source);
            }
        }

        return bootstrap::SuccessIn(bootstrap::Stage::Launch);
    }

    bootstrap::BootResult CheckLastStage(const bootstrap::ModuleDescriptor& descriptor, bootstrap::Stage last_stage)
    {
        const bootstrap::Stage startStage = bootstrap::StartStageOf(descriptor);

        if (startStage > last_stage)
        {
            const std::string source = std::format("the module starts in {}, and this executable stops after {}",
                                                   bootstrap::ToString(startStage),
                                                   bootstrap::ToString(last_stage));

            return LaunchFailure(bootstrap::BootResultCode::ModuleAfterLastStage,
                                 descriptor.Name,
                                 descriptor.Name,
                                 source);
        }

        return bootstrap::SuccessIn(bootstrap::Stage::Launch);
    }

    bootstrap::BootResult CheckDependency(std::span<const bootstrap::ModuleDescriptor> linked_modules,
                                          std::span<const bootstrap::ModuleDescriptor* const> selected,
                                          int64_t dependent_index,
                                          std::string_view dependency_name,
                                          std::string_view wanted_source)
    {
        const bootstrap::ModuleDescriptor& dependent = *selected[static_cast<size_t>(dependent_index)];
        const int64_t linkedIndex = FindLinkedIndex(linked_modules, dependency_name);

        if (linkedIndex == -1)
        {
            const std::vector<std::string_view> linkedNames = LinkedNames(linked_modules);
            bootstrap::BootResult failure = LaunchFailure(bootstrap::BootResultCode::UnknownModule,
                                                          dependent.Name,
                                                          dependency_name,
                                                          "DependsOn");

            failure.Suggestions = foundation::NearestNames(dependency_name, linkedNames, k_MaxSuggestions);
            return failure;
        }

        const int64_t selectedIndex = FindSelectedIndex(selected, dependency_name);

        if (selectedIndex == -1)
        {
            return LaunchFailure(bootstrap::BootResultCode::DependencyNotSelected,
                                 dependent.Name,
                                 dependency_name,
                                 wanted_source);
        }

        const bootstrap::ModuleDescriptor& dependency = *selected[static_cast<size_t>(selectedIndex)];
        const bootstrap::Stage dependentStage = bootstrap::StartStageOf(dependent);
        const bootstrap::Stage dependencyStage = bootstrap::StartStageOf(dependency);

        if (dependencyStage > dependentStage)
        {
            const std::string source = std::format("the dependency starts in {}, after {}",
                                                   bootstrap::ToString(dependencyStage),
                                                   bootstrap::ToString(dependentStage));

            return LaunchFailure(bootstrap::BootResultCode::DependencyNotEarlier,
                                 dependent.Name,
                                 dependency_name,
                                 source);
        }

        if (selectedIndex >= dependent_index)
        {
            return LaunchFailure(bootstrap::BootResultCode::DependencyNotEarlier,
                                 dependent.Name,
                                 dependency_name,
                                 "the dependency is not earlier in the linked module list");
        }

        return bootstrap::SuccessIn(bootstrap::Stage::Launch);
    }
}
