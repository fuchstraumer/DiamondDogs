#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_LAUNCH_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_LAUNCH_STAGE_HPP
#include "BootResult.hpp"
#include "BootTimings.hpp"
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bootstrap
{
    struct ModuleDescriptor;

    /** The config file that `--config=PATH` points to. Path is empty when the arguments name no config file.
     *  LaunchStage checks that Text is valid JSON, and each stage parses its own section from Text. */
    struct ConfigDocument
    {
        std::filesystem::path Path;
        std::string Text;
    };

    /** The first stage. It reads the arguments and the config file, chooses the profile, and selects the modules
     *  that later stages start. It also owns the boot timings, which every later stage records into. */
    class LaunchStage
    {
    public:
        LaunchStage() noexcept;
        ~LaunchStage();
        // later stages store a reference to this stage
        LaunchStage(const LaunchStage&) = delete;
        LaunchStage& operator=(const LaunchStage&) = delete;
        LaunchStage(LaunchStage&&) = delete;
        LaunchStage& operator=(LaunchStage&&) = delete;

        /** @brief Reads the arguments (`--config=PATH`, `--profile=NAME`, `--boot-timings`) and the config file, and
         *  selects the modules. With a profile, the profile's list in the config file selects them. Without one,
         *  every linked module is selected. Fails when a name is unknown or repeats, when a descriptor sets neither
         *  or both start functions, when a dependency is not selected or does not start earlier, and when a module
         *  starts after `last_stage`. The arguments and the linked list must outlive this stage, as `argv` and a
         *  constant array do. */
        [[nodiscard]] BootResult Start(std::span<char* const> _arguments,
                                       std::span<const ModuleDescriptor> linked_modules,
                                       Stage last_stage);

        /** @brief Returns every argument, unchanged, so that a module start function can read its own flags. */
        [[nodiscard]] std::span<const std::string_view> Arguments() const noexcept;
        [[nodiscard]] const ConfigDocument& Config() const noexcept;
        /** @brief Returns the profile name, or an empty view when no profile was given. */
        [[nodiscard]] std::string_view ProfileName() const noexcept;
        /** @brief Returns the selected modules, in the order of the linked list. */
        [[nodiscard]] std::span<const ModuleDescriptor* const> Selected() const noexcept;
        [[nodiscard]] Stage LastStage() const noexcept;
        /** @brief Returns true when the arguments contain `--boot-timings`. */
        [[nodiscard]] bool BootTimingsRequested() const noexcept;
        /** @brief Returns the path beside the executable when the file is there, and `build_tree_path` when not. */
        [[nodiscard]] std::filesystem::path FindBeside(std::string_view file_name,
                                                       std::string_view build_tree_path) const;
        /** @brief Returns the timings of the whole boot. Later stages take this stage as const and still record their
         *  spans, so the recorder is mutable: it is a diagnostic, not part of the launch state. */
        [[nodiscard]] BootTimings& Timings() const noexcept;

    private:
        [[nodiscard]] BootResult readArguments();
        [[nodiscard]] BootResult readConfig();
        [[nodiscard]] BootResult selectModules(std::span<const ModuleDescriptor> linked_modules);

        std::vector<std::string_view> arguments;
        std::string_view configArgument;
        std::string_view profileName;
        ConfigDocument config;
        std::vector<const ModuleDescriptor*> selected;
        std::filesystem::path executableDirectory;
        Stage lastStage{};
        bool bootTimingsRequested{ false };
        mutable BootTimings timings;
    };
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_LAUNCH_STAGE_HPP
