#pragma once
#ifndef DIAMOND_DOGS_INPUT_SYSTEM_INPUT_FRAME_HPP
#define DIAMOND_DOGS_INPUT_SYSTEM_INPUT_FRAME_HPP
#include "InputTypes.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace input
{
    struct ControlState
    {
        int64_t LastChangeNs;
        float Value;
        float Delta;
        bool Down;
        bool PressedThisFrame;
        bool ReleasedThisFrame;
    };

    enum class CaptureFlags : uint8_t
    {
        Invalid = 0,
        Keyboard = 1 << 0,
        Mouse = 1 << 1,
        Gamepad = 1 << 2,
    };

    constexpr CaptureFlags operator|(CaptureFlags left, CaptureFlags right) noexcept
    {
        return static_cast<CaptureFlags>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr CaptureFlags operator&(CaptureFlags left, CaptureFlags right) noexcept
    {
        return static_cast<CaptureFlags>(static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    constexpr CaptureFlags& operator|=(CaptureFlags& left, CaptureFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    /** @brief Returns true when any bit of the value is set. */
    constexpr bool Any(CaptureFlags value) noexcept
    {
        return static_cast<uint8_t>(value) != 0;
    }

    class FrameAssembler;

    /** @brief The input of one frame. It refers to storage in the FrameAssembler that built it. */
    class InputFrame
    {
    public:

        /** @brief Returns the events of this frame in arrival order. */
        [[nodiscard]] std::span<const InputEvent> Events() const noexcept;

        /** @brief Returns the Unicode code points typed in this frame, in arrival order. */
        [[nodiscard]] std::span<const uint32_t> Text() const noexcept;

        /** @brief Returns the state of a control. The control must be inside the control space. */
        [[nodiscard]] const ControlState& State(ControlId control) const noexcept;

        [[nodiscard]] float CursorX() const noexcept;

        [[nodiscard]] float CursorY() const noexcept;

        [[nodiscard]] bool HasFocus() const noexcept;

        [[nodiscard]] CaptureFlags Capture() const noexcept;

        /** @brief Returns true when the capture flags include the device class of the control. */
        [[nodiscard]] bool IsCaptured(ControlId control) const noexcept;

        /** @brief Returns how many events did not fit in this frame, or had a control outside the control space.
         * Code points are counted in DroppedTextCount. */
        [[nodiscard]] int32_t DroppedEventCount() const noexcept;

        /** @brief Returns how many code points did not fit in the text list of this frame. */
        [[nodiscard]] int32_t DroppedTextCount() const noexcept;

    private:

        friend class FrameAssembler;

        std::span<const InputEvent> events;
        std::span<const uint32_t> text;
        std::span<const ControlState> states;
        float cursorX = 0.0f;
        float cursorY = 0.0f;
        bool hasFocus = true;
        CaptureFlags capture = CaptureFlags::Invalid;
        int32_t droppedEventCount = 0;
        int32_t droppedTextCount = 0;
    };

    /** @brief Builds one InputFrame at a time from the events that a platform source appends. */
    class FrameAssembler
    {
    public:

        FrameAssembler(int32_t event_capacity, int32_t text_capacity);

        FrameAssembler(const FrameAssembler&) = delete;

        FrameAssembler& operator=(const FrameAssembler&) = delete;

        /** @brief Clears the data of the last frame. Down, Value, LastChangeNs, cursor and focus persist. */
        void StartFrame() noexcept;

        /** @brief Updates the state of the control at once, then stores the event if there is room. */
        void Append(const InputEvent& event) noexcept;

        void SetCapture(CaptureFlags flags) noexcept;

        /** @brief Returns the frame. It is valid until the next StartFrame. */
        [[nodiscard]] const InputFrame& FinishFrame() noexcept;

    private:

        void applyToState(const InputEvent& event) noexcept;
        void applyButtonDown(const InputEvent& event) noexcept;
        void applyButtonUp(const InputEvent& event) noexcept;
        void applyCursorPosition(const InputEvent& event) noexcept;
        void appendText(uint32_t code_point) noexcept;
        void storeEvent(const InputEvent& event) noexcept;
        void releaseHeldControls(int64_t timestamp_ns) noexcept;

        std::vector<InputEvent> events;
        std::vector<uint32_t> text;
        std::array<ControlState, k_ControlCount> states;
        int32_t eventCapacity;
        int32_t textCapacity;
        float cursorX;
        float cursorY;
        bool hasFocus;
        CaptureFlags capture;
        int32_t droppedEventCount;
        int32_t droppedTextCount;
        InputFrame frame;
    };
}

#endif // !DIAMOND_DOGS_INPUT_SYSTEM_INPUT_FRAME_HPP
