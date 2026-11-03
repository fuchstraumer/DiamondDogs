#include "GlfwInputSource.hpp"
#include "GlfwInputTranslation.hpp"
#include "InputFrame.hpp"
#include "InputTypes.hpp"
#include <cstdint>

#include "GLFW/glfw3.h"

GlfwInputSource::GlfwInputSource() noexcept :
  assembler{ nullptr },
  lastCursorX{ 0.0f },
  lastCursorY{ 0.0f },
  hasCursorBaseline{ false }
{}

void GlfwInputSource::SetAssembler(input::FrameAssembler* _assembler, int64_t timestamp_ns, bool focused) noexcept
{
    assembler = _assembler;

    if (assembler == nullptr)
    {
        return;
    }

    appendFocus(timestamp_ns, focused);
}

void GlfwInputSource::OnKey(int64_t timestamp_ns, int glfw_key, int action, int mods) noexcept
{
    const input::ControlId control = ControlFromGlfwKey(glfw_key);

    if (control == input::ControlId::Invalid)
    {
        return;
    }

    const bool pressed = action != GLFW_RELEASE;

    appendButton(timestamp_ns, control, pressed, mods);
}

void GlfwInputSource::OnChar(int64_t timestamp_ns, uint32_t code_point) noexcept
{
    input::InputEvent event{};
    event.TimestampNs = timestamp_ns;
    event.CodePoint = code_point;
    event.Kind = input::InputEventKind::Text;

    append(event);
}

void GlfwInputSource::OnMouseButton(int64_t timestamp_ns, int button, int action, int mods) noexcept
{
    const input::ControlId control = ControlFromGlfwMouseButton(button);

    if (control == input::ControlId::Invalid)
    {
        return;
    }

    const bool pressed = action != GLFW_RELEASE;

    appendButton(timestamp_ns, control, pressed, mods);
}

void GlfwInputSource::OnCursorPosition(int64_t timestamp_ns, float x, float y) noexcept
{
    input::InputEvent position{};
    position.TimestampNs = timestamp_ns;
    position.X = x;
    position.Y = y;
    position.Kind = input::InputEventKind::CursorPosition;

    append(position);

    if (hasCursorBaseline)
    {
        appendCursorDeltas(timestamp_ns, x, y);
    }

    lastCursorX = x;
    lastCursorY = y;
    hasCursorBaseline = true;
}

void GlfwInputSource::OnScroll(int64_t timestamp_ns, float x_offset, float y_offset) noexcept
{
    // GLFW inverts the Win32 horizontal wheel for consistency with macOS and X11, so we undo that here
    const float wheelX = -x_offset;

    appendAxisDelta(timestamp_ns, input::ControlId::WheelX, wheelX);
    appendAxisDelta(timestamp_ns, input::ControlId::WheelY, y_offset);
}

void GlfwInputSource::OnFocus(int64_t timestamp_ns, bool focused) noexcept
{
    if (focused)
    {
        hasCursorBaseline = false;
    }

    appendFocus(timestamp_ns, focused);
}

void GlfwInputSource::OnCursorEnter(bool entered) noexcept
{
    static_cast<void>(entered);

    hasCursorBaseline = false;
}

void GlfwInputSource::append(const input::InputEvent& event) noexcept
{
    if (assembler == nullptr)
    {
        return;
    }

    assembler->Append(event);
}

void GlfwInputSource::appendButton(int64_t timestamp_ns, input::ControlId control, bool pressed, int mods) noexcept
{
    input::InputEvent event{};
    event.TimestampNs = timestamp_ns;
    event.Control = control;
    event.Kind = pressed ? input::InputEventKind::ButtonDown : input::InputEventKind::ButtonUp;
    event.Device = input::DeviceOf(control);
    event.Modifiers = ModifiersFromGlfw(mods);

    append(event);
}

void GlfwInputSource::appendAxisDelta(int64_t timestamp_ns, input::ControlId control, float delta) noexcept
{
    if (delta == 0.0f)
    {
        return;
    }

    input::InputEvent event{};
    event.TimestampNs = timestamp_ns;
    event.X = delta;
    event.Control = control;
    event.Kind = input::InputEventKind::AxisDelta;
    event.Device = input::DeviceOf(control);

    append(event);
}

void GlfwInputSource::appendCursorDeltas(int64_t timestamp_ns, float x, float y) noexcept
{
    const float deltaX = x - lastCursorX;
    const float deltaY = y - lastCursorY;

    appendAxisDelta(timestamp_ns, input::ControlId::MouseX, deltaX);
    appendAxisDelta(timestamp_ns, input::ControlId::MouseY, deltaY);
}

void GlfwInputSource::appendFocus(int64_t timestamp_ns, bool focused) noexcept
{
    input::InputEvent event{};
    event.TimestampNs = timestamp_ns;
    event.Kind = focused ? input::InputEventKind::FocusGained : input::InputEventKind::FocusLost;

    append(event);
}
