#include "InputFrame.hpp"
#include "InputTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace
{
    bool IsInsideControlSpace(input::ControlId control) noexcept;

    bool IsButtonOrAxis(input::InputEventKind kind) noexcept;

    bool CanFold(const input::InputEvent& last, const input::InputEvent& event) noexcept;

    void Fold(input::InputEvent& last, const input::InputEvent& event) noexcept;

    input::CaptureFlags CaptureFlagFor(input::DeviceKind device) noexcept;
}

namespace input
{
    std::span<const InputEvent> InputFrame::Events() const noexcept
    {
        return events;
    }

    std::span<const uint32_t> InputFrame::Text() const noexcept
    {
        return text;
    }

    const ControlState& InputFrame::State(ControlId control) const noexcept
    {
        return states[static_cast<size_t>(control)];
    }

    float InputFrame::CursorX() const noexcept
    {
        return cursorX;
    }

    float InputFrame::CursorY() const noexcept
    {
        return cursorY;
    }

    bool InputFrame::HasFocus() const noexcept
    {
        return hasFocus;
    }

    CaptureFlags InputFrame::Capture() const noexcept
    {
        return capture;
    }

    bool InputFrame::IsCaptured(ControlId control) const noexcept
    {
        const CaptureFlags controlFlag = CaptureFlagFor(DeviceOf(control));

        return Any(capture & controlFlag);
    }

    int32_t InputFrame::DroppedEventCount() const noexcept
    {
        return droppedEventCount;
    }

    FrameAssembler::FrameAssembler(int32_t event_capacity, int32_t text_capacity) :
      states{},
      eventCapacity{ event_capacity },
      textCapacity{ text_capacity },
      cursorX{ 0.0f },
      cursorY{ 0.0f },
      hasFocus{ true },
      capture{ CaptureFlags::Invalid },
      droppedEventCount{ 0 }
    {
        events.reserve(static_cast<size_t>(event_capacity));
        text.reserve(static_cast<size_t>(text_capacity));
    }

    void FrameAssembler::StartFrame() noexcept
    {
        events.clear();
        text.clear();
        droppedEventCount = 0;
        capture = CaptureFlags::Invalid;

        for (ControlState& state : states)
        {
            state.PressedThisFrame = false;
            state.ReleasedThisFrame = false;
            state.Delta = 0.0f;
        }
    }

    void FrameAssembler::Append(const InputEvent& event) noexcept
    {
        if (IsButtonOrAxis(event.Kind) && !IsInsideControlSpace(event.Control))
        {
            ++droppedEventCount;
            return;
        }

        ApplyToState(event);
        StoreEvent(event);

        if (event.Kind == InputEventKind::FocusLost)
        {
            ReleaseHeldControls(event.TimestampNs);
        }
    }

    void FrameAssembler::SetCapture(CaptureFlags flags) noexcept
    {
        capture = flags;
    }

    const InputFrame& FrameAssembler::FinishFrame() noexcept
    {
        frame.events = events;
        frame.text = text;
        frame.states = states;
        frame.cursorX = cursorX;
        frame.cursorY = cursorY;
        frame.hasFocus = hasFocus;
        frame.capture = capture;
        frame.droppedEventCount = droppedEventCount;

        return frame;
    }

    void FrameAssembler::ApplyToState(const InputEvent& event) noexcept
    {
        switch (event.Kind)
        {
        case InputEventKind::ButtonDown: ApplyButtonDown(event); break;
        case InputEventKind::ButtonUp: ApplyButtonUp(event); break;
        case InputEventKind::AxisValue: states[static_cast<size_t>(event.Control)].Value = event.X; break;
        case InputEventKind::AxisDelta: states[static_cast<size_t>(event.Control)].Delta += event.X; break;
        case InputEventKind::Text: AppendText(event.CodePoint); break;
        case InputEventKind::FocusGained: hasFocus = true; break;
        case InputEventKind::FocusLost: hasFocus = false; break;
        case InputEventKind::CursorPosition:
            cursorX = event.X;
            cursorY = event.Y;
            break;
        default: break;
        }
    }

    void FrameAssembler::ApplyButtonDown(const InputEvent& event) noexcept
    {
        ControlState& state = states[static_cast<size_t>(event.Control)];

        // a repeat on a held key changes nothing, so the Hold timing of the first press stays correct
        if (state.Down)
        {
            return;
        }

        state.Down = true;
        state.PressedThisFrame = true;
        state.Value = 1.0f;
        state.LastChangeNs = event.TimestampNs;
    }

    void FrameAssembler::ApplyButtonUp(const InputEvent& event) noexcept
    {
        ControlState& state = states[static_cast<size_t>(event.Control)];

        if (!state.Down)
        {
            return;
        }

        state.Down = false;
        state.ReleasedThisFrame = true;
        state.Value = 0.0f;
        state.LastChangeNs = event.TimestampNs;
    }

    void FrameAssembler::AppendText(uint32_t code_point) noexcept
    {
        if (std::ssize(text) >= textCapacity)
        {
            ++droppedEventCount;
            return;
        }

        text.push_back(code_point);
    }

    void FrameAssembler::StoreEvent(const InputEvent& event) noexcept
    {
        if (std::ssize(events) < eventCapacity)
        {
            events.push_back(event);
            return;
        }

        if (!events.empty() && CanFold(events.back(), event))
        {
            Fold(events.back(), event);
            return;
        }

        ++droppedEventCount;
    }

    void FrameAssembler::ReleaseHeldControls(int64_t timestamp_ns) noexcept
    {
        for (int32_t controlIndex = 0; controlIndex < k_ControlCount; ++controlIndex)
        {
            if (!states[static_cast<size_t>(controlIndex)].Down)
            {
                continue;
            }

            const ControlId control = static_cast<ControlId>(controlIndex);

            InputEvent release{};
            release.TimestampNs = timestamp_ns;
            release.Control = control;
            release.Kind = InputEventKind::ButtonUp;
            release.Device = DeviceOf(control);
            release.Flags = EventFlags::Synthesized;

            Append(release);
        }
    }
}

namespace
{
    bool IsInsideControlSpace(input::ControlId control) noexcept
    {
        const int32_t value = static_cast<int32_t>(control);

        return value > 0 && value < input::k_ControlCount;
    }

    bool IsButtonOrAxis(input::InputEventKind kind) noexcept
    {
        return kind == input::InputEventKind::ButtonDown || kind == input::InputEventKind::ButtonUp ||
            kind == input::InputEventKind::AxisValue || kind == input::InputEventKind::AxisDelta;
    }

    bool CanFold(const input::InputEvent& last, const input::InputEvent& event) noexcept
    {
        const bool foldableKind = event.Kind == input::InputEventKind::AxisDelta ||
            event.Kind == input::InputEventKind::CursorPosition;

        return foldableKind && last.Kind == event.Kind && last.Control == event.Control;
    }

    void Fold(input::InputEvent& last, const input::InputEvent& event) noexcept
    {
        if (event.Kind == input::InputEventKind::AxisDelta)
        {
            last.X += event.X;
            last.Y += event.Y;
        }
        else
        {
            last.X = event.X;
            last.Y = event.Y;
        }

        last.TimestampNs = std::max(last.TimestampNs, event.TimestampNs);
    }

    input::CaptureFlags CaptureFlagFor(input::DeviceKind device) noexcept
    {
        switch (device)
        {
        case input::DeviceKind::Keyboard: return input::CaptureFlags::Keyboard;
        case input::DeviceKind::Mouse: return input::CaptureFlags::Mouse;
        case input::DeviceKind::Gamepad: return input::CaptureFlags::Gamepad;
        default: return input::CaptureFlags::Invalid;
        }
    }
}
