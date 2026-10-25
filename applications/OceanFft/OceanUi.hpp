#pragma once
#ifndef DIAMOND_DOGS_OCEAN_UI_HPP
#define DIAMOND_DOGS_OCEAN_UI_HPP
#include "ImGuiModule.hpp"
#include "OceanSimulation.hpp"
#include "SeaState.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace rhi
{
    class GpuTimers;
}

namespace ocean
{
    enum class HeaderFontMode : uint8_t
    {
        Invalid = 0,
        Blinker = 1,
        Michroma = 2,
    };

    enum class AccentTone : uint8_t
    {
        Invalid = 0,
        Cyan = 1,
        Amber = 2,
        Ice = 3,
    };

    struct OceanPanelState
    {
        SeaState* State{ nullptr };
        const SeaStateReadout* Readout{ nullptr };
        SpectrumParameters* Spectrum{ nullptr };
        const OceanSimulationSettings* SimulationSettings{ nullptr };
        std::array<bool, k_CascadeCount>* CascadeEnabled{ nullptr };
        float* Choppiness{ nullptr };
        float* LodScale{ nullptr };
        float* TimeScale{ nullptr };
        float* SeabedDepth{ nullptr };
        float* SunElevation{ nullptr };
        float* SunAzimuth{ nullptr };
        bool* Paused{ nullptr };
        HeaderFontMode* FontMode{ nullptr };
        bool SeaStateChanged{ false };
        bool SpectrumChanged{ false };
    };

    /** @brief Opens the frameless chamfered telemetry panel and draws its background chassis. */
    [[nodiscard]] bool BeginTelemetryPanel(const char* name, const ImGuiFontSet& fonts, HeaderFontMode font_mode);
    /** @brief Draws the panel's outer hairline border and corner reticles, then closes the ImGui window. */
    void EndTelemetryPanel();

    /** @brief Draws the top title banner, frame rate badge, and IFFT compute configuration line. */
    void DrawTelemetryBanner(const ImGuiFontSet& fonts, HeaderFontMode font_mode, const OceanSimulationSettings& settings);

    /** @brief Draws the measured sea-state summary card and the wind/swell directional compass reticle. */
    void DrawSeaReadoutCard(const ImGuiFontSet& fonts, const SeaState& state, const SeaStateReadout& readout);

    /** @brief Draws a collapsible stenciled section header with a 1px hairline and a 2px left accent bar. */
    [[nodiscard]] bool DrawSectionHeader(const char* label, const ImGuiFontSet& fonts, HeaderFontMode font_mode,
                                         AccentTone accent_tone, bool default_open);

    /** @brief Draws a non-collapsible stenciled sub-header divider inside an open section. */
    void DrawSubSectionDivider(const char* label, const ImGuiFontSet& fonts, HeaderFontMode font_mode, AccentTone accent_tone);

    /** @brief Draws the 6 named sea-state presets as a chamfered button grid. */
    [[nodiscard]] bool DrawPresetGrid(const ImGuiFontSet& fonts, SeaState& state);

    /** @brief Draws a two-column artist-domain slider with a chunky chamfered grab block and optional semantic hint. */
    [[nodiscard]] bool DrawArtistSlider(const char* id, std::string_view label, std::string_view hint, float* value,
                                        float min_value, float max_value, std::string_view value_text,
                                        std::string_view unit_text, const ImGuiFontSet& fonts, AccentTone accent_tone,
                                        bool logarithmic);

    /** @brief Draws a centered (-max to +max) directional slider with a chunky chamfered grab block. */
    [[nodiscard]] bool DrawCenteredAngleSlider(const char* id, std::string_view label, std::string_view hint,
                                               float* degrees, float min_degrees, float max_degrees,
                                               const ImGuiFontSet& fonts, AccentTone accent_tone);

    /** @brief Draws a chamfered toggle switch for boolean state (such as Paused or Michroma header comparison). */
    [[nodiscard]] bool DrawTelemetryToggle(const char* id, std::string_view label, std::string_view state_off,
                                           std::string_view state_on, bool* value, const ImGuiFontSet& fonts,
                                           AccentTone accent_tone);

    /** @brief Draws the 4-band horizontal cascade enable selector strip. */
    void DrawCascadeBandStrip(const ImGuiFontSet& fonts, const std::array<float, k_CascadeCount>& length_scales,
                              std::array<bool, k_CascadeCount>& cascade_enabled);

    /** @brief Draws the derived physics spectrum inspector (spreading selector, seed, and resolved JONSWAP values). */
    [[nodiscard]] bool DrawSpectrumPhysicsInspector(const ImGuiFontSet& fonts, SpectrumParameters& spectrum);

    /** @brief Draws the GPU timings table with hierarchical tree ticks and proportional duration bars. */
    void DrawGpuTimingsSection(const ImGuiFontSet& fonts, HeaderFontMode font_mode, const rhi::GpuTimers& gpu_timers,
                               const std::unordered_map<uint64_t, double>& timing_averages);
}

#endif // !DIAMOND_DOGS_OCEAN_UI_HPP
