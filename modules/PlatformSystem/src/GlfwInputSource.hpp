#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_GLFW_INPUT_SOURCE_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_GLFW_INPUT_SOURCE_HPP
#include "InputTypes.hpp"
#include <cstdint>

namespace input
{
    class FrameAssembler;
}

/**
 * @brief Turns the values of GLFW input callbacks into input::InputEvent records, and appends them to a
 * FrameAssembler. The methods take plain values and a timestamp, so a test drives them with no window.
 */
class GlfwInputSource
{
public:

    GlfwInputSource() noexcept;

    /** @brief Sets the assembler that receives the events. Null means events are discarded. A non-null assembler
     * at once receives one focus event with the real focus state of the window, because a new assembler starts
     * with focus gained. The caller keeps the assembler alive until it passes null, or destroys the window
     * system. The source never owns the assembler. */
    void SetAssembler(input::FrameAssembler* assembler, int64_t timestamp_ns, bool focused) noexcept;

    /** @brief PRESS and REPEAT make ButtonDown, and RELEASE makes ButtonUp. A key with no control makes no event. */
    void OnKey(int64_t timestamp_ns, int glfw_key, int action, int mods) noexcept;

    /** @brief Appends a Text event with the whole code point. */
    void OnChar(int64_t timestamp_ns, uint32_t code_point) noexcept;

    void OnMouseButton(int64_t timestamp_ns, int button, int action, int mods) noexcept;

    /** @brief Takes x and y in framebuffer pixels. Appends CursorPosition, then an AxisDelta on MouseX and on
     * MouseY with the change since the last position, and skips a zero component. The first position after
     * construction, after focus gain and after cursor enter makes no delta. */
    void OnCursorPosition(int64_t timestamp_ns, float x, float y) noexcept;

    /** @brief Takes the offsets as GLFW reports them. Appends an AxisDelta in notches on WheelX and on WheelY,
     * and skips a zero component. WheelY is positive away from the user. GLFW makes a positive x offset mean
     * left on every platform, so WheelX negates it, and WheelX is positive to the right. */
    void OnScroll(int64_t timestamp_ns, float x_offset, float y_offset) noexcept;

    void OnFocus(int64_t timestamp_ns, bool focused) noexcept;

    /** @brief Resets the baseline of the cursor delta. Appends no event. */
    void OnCursorEnter(bool entered) noexcept;

private:

    void append(const input::InputEvent& event) noexcept;
    void appendButton(int64_t timestamp_ns, input::ControlId control, bool pressed, int mods) noexcept;
    void appendAxisDelta(int64_t timestamp_ns, input::ControlId control, float delta) noexcept;
    void appendCursorDeltas(int64_t timestamp_ns, float x, float y) noexcept;
    void appendFocus(int64_t timestamp_ns, bool focused) noexcept;

    input::FrameAssembler* assembler;
    float lastCursorX;
    float lastCursorY;
    bool hasCursorBaseline;
};

#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_GLFW_INPUT_SOURCE_HPP
