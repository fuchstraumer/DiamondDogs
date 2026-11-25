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
#include <span>
#include <string_view>
#include <unordered_map>

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

    enum class TelemetryTab : uint8_t
    {
        SeaState = 0,
        Surface = 1,
        Foam = 2,
        Physics = 3,
        Timings = 4,
        Scene = 5,
        Debug = 6,
    };

    /** @brief Opens the telemetry panel, draws its scrim, and sets ImGuiStyle from the UI tokens. Every other
     *  function in this file belongs between this call and EndTelemetryPanel, except DrawRestoreMenuButton. */
    [[nodiscard]] bool BeginTelemetryPanel(const char* name, const ImGuiFontSet& fonts, HeaderFontMode font_mode,
                                           bool* p_open);

    /** @brief Closes the telemetry panel. */
    void EndTelemetryPanel();

    /** @brief Draws a small text button that restores a closed panel. Returns true when it was clicked. */
    [[nodiscard]] bool DrawRestoreMenuButton(const ImGuiFontSet& fonts, bool* p_open);

    /** @brief Draws the title, the frame time, the compute configuration and the close button. */
    void DrawTelemetryBanner(const OceanSimulationSettings& settings, bool* p_open);

    /** @brief Draws the hero readout: significant wave height, Beaufort force, and the wind and swell compass. */
    void DrawSeaReadout(const SeaState& state, const SeaStateReadout& readout);

    /**
     * @brief Draws the run and pause control, the snapshot control and the time scale. Returns true when the pause
     * state or the time scale changed. A click on the snapshot control sets *snapshot_requested to true. While
     * snapshot_saved is true, the control reads SAVED.
     */
    [[nodiscard]] bool DrawSimulationTransportBar(bool* paused, float* time_scale, bool* snapshot_requested,
                                                  bool snapshot_saved);

    /** @brief Draws the tabs in one row, as text with an accent underline. Returns true when the tab changed. */
    [[nodiscard]] bool DrawTelemetryTabs(TelemetryTab& active_tab);

    /** @brief Draws a small capitals heading with a hairline to its right. */
    void DrawSectionLabel(std::string_view label);

    /** @brief Draws a label on the left and a value, with a smaller unit, on the right. */
    void DrawKeyValue(std::string_view label, std::string_view value, std::string_view unit);

    /** @brief Draws a label on the left and leaves the cursor in the value column, with the next item width set
     *  to fill it. Draw one stock widget (combo, input) after this call, with an id that starts with "##". */
    void BeginFieldRow(std::string_view label);

    /** @brief Draws options as text in rows. The selected option gets the accent color and an underline.
     *  Pass selected = -1 for no selection. Returns true when the user chose an option, and writes it to selected. */
    [[nodiscard]] bool DrawSelector(const char* id, std::span<const std::string_view> options, int32_t& selected,
                                    int32_t columns);

    /** @brief The same as DrawSelector, with a label on the left. The options go below the label when they do not
     *  fit beside it. */
    [[nodiscard]] bool DrawSelectorRow(const char* id, std::string_view label,
                                       std::span<const std::string_view> options, int32_t& selected,
                                       int32_t columns);

    /** @brief A two-option selector that writes a bool. Returns true when the user changed the value. */
    [[nodiscard]] bool DrawToggle(const char* id, std::string_view label, std::string_view state_off,
                                  std::string_view state_on, bool* value);

    /** @brief Draws the six sea state presets as a selector. The preset that equals the state is selected. */
    [[nodiscard]] bool DrawPresetSelector(SeaState& state);

    /** @brief Draws the sky presets as a selector. */
    [[nodiscard]] bool DrawSkyPresetSelector(OceanSkyPreset& active_preset);

    /** @brief Draws the tonemappers as a selector. */
    [[nodiscard]] bool DrawTonemapperSelector(color::TonemapperKind& active_tonemapper);

    /** @brief Draws a slider row: label left, value right, unit after the value, a hairline track below.
     *  Ctrl-click types a value. The arrow keys step the value on a focused slider. The value text and the
     *  unit text are drawn as given. Returns true when the value changed. */
    [[nodiscard]] bool DrawSlider(const char* id, std::string_view label, std::string_view hint, float* value,
                                  float min_value, float max_value, std::string_view value_text,
                                  std::string_view unit_text, bool logarithmic);

    /** @brief A slider for a heading in degrees. The fill starts at zero, and values near zero snap to zero. */
    [[nodiscard]] bool DrawAngleSlider(const char* id, std::string_view label, std::string_view hint,
                                       float* degrees, float min_degrees, float max_degrees);

    /** @brief Draws one toggle per cascade, labeled with its length scale. */
    void DrawCascadeBandStrip(const std::array<float, k_CascadeCount>& length_scales,
                              std::array<bool, k_CascadeCount>& cascade_enabled);

    /** @brief Draws the spectrum model selectors, the seed, and the resolved parameters. */
    [[nodiscard]] bool DrawSpectrumPhysicsInspector(SpectrumParameters& spectrum);

    /** @brief Draws the GPU timing tables, one for each queue that has timers. */
    void DrawGpuTimingsSection(const rhi::GpuTimers& gpu_timers,
                               const std::unordered_map<uint64_t, double>& timing_averages,
                               const rhi::GpuTimers* async_timers,
                               const std::unordered_map<uint64_t, double>* async_timing_averages);

    /** @brief Draws a dim italic note. */
    void DrawNote(std::string_view text);
}

#endif // !DIAMOND_DOGS_OCEAN_UI_HPP
