#pragma once
#ifndef DIAMOND_DOGS_PLANET_LOOM_LIVE_APP_HPP
#define DIAMOND_DOGS_PLANET_LOOM_LIVE_APP_HPP

#include "ReferenceSet.hpp"
#include "StepState.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace planet_loom
{
    struct PlanetLiveOptions
    {
        std::filesystem::path CaptureDir;
        uint32_t Order{ 7u };
        float Dt{ 0.5f };
        uint32_t Width{ 1024u };
        uint32_t Height{ 512u };
        uint32_t Mode{ 0u };
        bool Words{ false };
        bool ValidationFull{ true };
        bool Timestamps{ true };
        bool PlumesEnabled{ true };
        bool StartPaused{ false };
        int32_t ToolPort{ -1 };
        int32_t FrameLimit{ -1 };
        bool Explain{ false };
    };

    /** Returns the readable pass name for a Loom GPU timer hash, or "<unknown>". */
    [[nodiscard]] std::string_view PassNameForHash(uint64_t hash) noexcept;

    /** Runs windowed interactive PlanetGenLoom (swapchain present, ImGui HUD, and optional AgentTools server). */
    [[nodiscard]] int RunLivePlanetApp(std::span<char* const> arguments,
                                       const PlanetLiveOptions& options,
                                       const planet::ReferenceSet& reference,
                                       const planet::HealpixInitialState& initial);

    /** Runs headless PlanetGenLoom AgentTools server (no window; supports step, reset, plumes, and capture). */
    [[nodiscard]] int RunHeadlessToolApp(std::span<char* const> arguments,
                                           const PlanetLiveOptions& options,
                                           const planet::ReferenceSet& reference,
                                           const planet::HealpixInitialState& initial);
}

#endif // !DIAMOND_DOGS_PLANET_LOOM_LIVE_APP_HPP
