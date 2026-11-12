#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_MODULE_SELECTION_HPP
#define DIAMOND_DOGS_BOOTSTRAP_MODULE_SELECTION_HPP
#include "BootResult.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace bootstrap
{
    struct ModuleDescriptor;

    /** @brief Returns the stage where the module starts: Core for StartCore, Gpu for StartGpu. The descriptor was
     *  checked, so exactly one of them is set. */
    [[nodiscard]] Stage StartStageOf(const ModuleDescriptor& descriptor) noexcept;

    /** @brief Checks the linked list itself: every name is set and unique, and every descriptor sets exactly one
     *  start function. */
    [[nodiscard]] BootResult CheckLinkedModules(std::span<const ModuleDescriptor> linked_modules);

    /** @brief Selects the linked modules whose names are in `wanted_names`, in linked order, and checks the
     *  selection: every wanted name is linked and unique, every dependency is selected and starts earlier, and no
     *  module starts after `last_stage`. `wanted_source` is written to Source on failure ("profile demo"). */
    [[nodiscard]] BootResult SelectModules(std::span<const ModuleDescriptor> linked_modules,
                                           std::span<const std::string_view> wanted_names,
                                           std::string_view wanted_source,
                                           Stage last_stage,
                                           std::vector<const ModuleDescriptor*>& selected);
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_MODULE_SELECTION_HPP
