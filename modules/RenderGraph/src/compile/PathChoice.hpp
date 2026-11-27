#pragma once
#ifndef DIAMOND_DOGS_LOOM_COMPILE_PATH_CHOICE_HPP
#define DIAMOND_DOGS_LOOM_COMPILE_PATH_CHOICE_HPP
#include "DeviceCapability.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <cstdint>
#include <source_location>
#include <string>
#include <vector>

// What ChoosePaths decided for each path list. CompiledData keeps it for ChosenPath and ExplainPaths.
namespace loom
{

    enum class PathRejection : uint8_t
    {
        Invalid = 0,
        Capability = 1,     // a statement needs a capability that the device was not created with
        Shader = 2          // a pass's entry point has no variant that the device can run (LM207)
    };

    // One path that Compile passed over, and the first reason it found.
    struct RejectedPath
    {
        KeyRef Path;
        std::source_location Where;
        PathRejection Reason{ PathRejection::Invalid };
        bootstrap::DeviceCapability Capability{ bootstrap::DeviceCapability::Invalid };    // Capability only
        KeyRef Pass;                    // the pass whose statement or shader needs what the device lacks
        std::string ShaderCapability;   // Shader only: the bundle's capability name, copied, because the bundle may close
    };

    // One list after ChoosePaths: the chosen path, the list's outputs, and what the checks after Bind need.
    struct PathChoice
    {
        KeyRef List;
        std::source_location Where;
        KeyRef Path;
        bool Forced{ false };
        std::vector<ProducesRecord> Produces;
        std::vector<KeyRef> Passes;             // the chosen path's passes
        std::vector<KeyRef> LocalResources;     // the chosen path's declarations and imports
        std::vector<RejectedPath> Rejected;     // the paths before the chosen one, in order of preference
    };

}

#endif // !DIAMOND_DOGS_LOOM_COMPILE_PATH_CHOICE_HPP
