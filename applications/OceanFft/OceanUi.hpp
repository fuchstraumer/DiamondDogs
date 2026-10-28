#pragma once
#ifndef DIAMOND_DOGS_OCEAN_UI_HPP
#define DIAMOND_DOGS_OCEAN_UI_HPP
#include "DisplayTarget.hpp"
#include "ImGuiModule.hpp"
#include "OceanEnvironment.hpp"
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

    enum class TelemetryTab : uint8_t
    {
        SeaState = 0,
        Surface = 1,
        Foam = 2,
        Physics = 3,
        Timings = 4,
        Scene = 5,
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

    /** @brief Opens the rounded, horizontally resizable telemetry panel and draws its chassis. */
    [[nodiscard]] bool BeginTelemetryPanel(const char* name, const ImGuiFontSet& fonts, HeaderFontMode font_mode,
                                           bool* p_open = nullptr);
    /** @brief Closes the ImGui telemetry panel. */
    void EndTelemetryPanel();

    /** @brief Draws a compact floating pill button in the upper-left quadrant to restore a closed panel. */
    [[nodiscard]] bool DrawRestoreMenuButton(const ImGuiFontSet& fonts, bool* p_open);

    /** @brief Draws the top title banner, frame rate badge, compute config, and optional close button. */
    void DrawTelemetryBanner(const ImGuiFontSet& fonts, HeaderFontMode font_mode, const OceanSimulationSettings& settings,
                             bool* p_open = nullptr);

    /** @brief Draws the measured sea-state summary card and the wind/swell directional compass reticle. */
    void DrawSeaReadoutCard(const ImGuiFontSet& fonts, const SeaState& state, const SeaStateReadout& readout);

    /** @brief Draws the top-level simulation transport bar (Run/Pause toggle and time scale indicator). */
    [[nodiscard]] bool DrawSimulationTransportBar(const ImGuiFontSet& fonts, bool* paused, float* time_scale,
                                                 AccentTone tone);

    /** @brief Draws the top HUD tab bar selector (Sea State, Surface, Foam, Physics, Timings, Scene). */
    [[nodiscard]] bool DrawTelemetryTabBar(const ImGuiFontSet& fonts, TelemetryTab& active_tab, AccentTone tone);

    /** @brief Draws a collapsible stenciled section header with a 1px hairline and an accent bar. */
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

    /** @brief Draws the horizontal cascade enable selector strip, one band per cascade. */
    void DrawCascadeBandStrip(const ImGuiFontSet& fonts, const std::array<float, k_CascadeCount>& length_scales,
                              std::array<bool, k_CascadeCount>& cascade_enabled);

    /** @brief Draws the derived physics spectrum inspector (spreading selector, seed, and resolved JONSWAP values). */
    [[nodiscard]] bool DrawSpectrumPhysicsInspector(const ImGuiFontSet& fonts, SpectrumParameters& spectrum);

    /** @brief Draws the 6 HDRI skydome presets as a button grid. */
    [[nodiscard]] bool DrawSkyPresetGrid(const ImGuiFontSet& fonts, OceanSkyPreset& active_preset);

    /** @brief Draws the tonemapper presets (AgX, KhronosNeutral, Passthrough, FalseColorEv) as a button grid. */
    [[nodiscard]] bool DrawTonemapperGrid(const ImGuiFontSet& fonts, color::TonemapperKind& active_tonemapper);

    /** @brief Draws the GPU timings table with hierarchical tree ticks and proportional duration bars. */
    void DrawGpuTimingsSection(const ImGuiFontSet& fonts, HeaderFontMode font_mode, const rhi::GpuTimers& gpu_timers,
                               const std::unordered_map<uint64_t, double>& timing_averages,
                               const rhi::GpuTimers* async_timers = nullptr,
                               const std::unordered_map<uint64_t, double>* async_timing_averages = nullptr);
}

#endif // !DIAMOND_DOGS_OCEAN_UI_HPP
