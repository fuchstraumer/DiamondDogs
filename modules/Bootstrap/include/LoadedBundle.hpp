#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_LOADED_BUNDLE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_LOADED_BUNDLE_HPP
#include "ShaderBundle.hpp"
#include <filesystem>
#include <string_view>

namespace bootstrap
{
    /** One shader bundle that ContentStage opened, and the request it came from. */
    struct LoadedBundle
    {
        // the request's file name, which FindBundle matches
        std::string_view FileName;
        std::string_view Profile;
        std::string_view BuildTreePath;
        // the descriptor name of the module that asked for the bundle, or empty for the executable's own list
        std::string_view RequestedBy;
        // where the file was found: beside the executable, or the build-tree path
        std::filesystem::path Path;
        rhi::ShaderBundle Bundle;
    };
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_LOADED_BUNDLE_HPP
