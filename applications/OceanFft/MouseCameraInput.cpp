#include "MouseCameraInput.hpp"
#include "ArcballCamera.hpp"
#include "InputFrame.hpp"
#include "InputTypes.hpp"
#include "math/Math.hpp"

namespace
{
    [[nodiscard]] bool IsShiftDown(const input::InputFrame& input) noexcept;
    [[nodiscard]] math::Float2 CursorPosition(const input::InputFrame& input) noexcept;
}

void MouseCameraInput::Apply(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) noexcept
{
    const bool dragging = dragMode != DragMode::None;

    // a drag that started on the sea keeps going when the cursor crosses the panel
    if (!dragging && frame.UiCapturesMouse)
    {
        applyFocusKey(frame, camera, controller);
        return;
    }

    if (dragging)
    {
        continueDrag(frame, camera, controller);
    }
    else
    {
        startDragIfPressed(frame, camera, controller);
    }

    applyWheel(frame, camera, controller);
    applyFocusKey(frame, camera, controller);
}

void MouseCameraInput::startDragIfPressed(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) noexcept
{
    const input::InputFrame& input = *frame.Input;
    const bool shiftDown = IsShiftDown(input);

    if (input.State(input::ControlId::MouseLeft).PressedThisFrame)
    {
        beginDrag(DragMode::Orbit, input::ControlId::MouseLeft, camera, controller);
    }
    else if (input.State(input::ControlId::MouseRight).PressedThisFrame)
    {
        beginDrag(shiftDown ? DragMode::PanViewPlane : DragMode::PanSea, input::ControlId::MouseRight, camera, controller);
    }
    else if (input.State(input::ControlId::MouseMiddle).PressedThisFrame)
    {
        beginDrag(shiftDown ? DragMode::PanViewPlane : DragMode::PanSea, input::ControlId::MouseMiddle, camera, controller);
    }

    if (dragMode != DragMode::None)
    {
        continueDrag(frame, camera, controller);
    }
}

void MouseCameraInput::beginDrag(DragMode mode, input::ControlId button, ArcballCamera& camera, ArcballController& controller) noexcept
{
    dragMode = mode;
    dragButton = button;
    controller.BeginManipulation(camera);
}

void MouseCameraInput::continueDrag(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) const noexcept
{
    const input::InputFrame& input = *frame.Input;
    const input::ControlState& mouseX = input.State(input::ControlId::MouseX);
    const input::ControlState& mouseY = input.State(input::ControlId::MouseY);
    const float deltaX = mouseX.Delta;
    const float deltaY = mouseY.Delta;

    if (deltaX == 0.0f && deltaY == 0.0f)
    {
        return;
    }

    if (dragMode == DragMode::Orbit)
    {
        // dragging right turns the camera left around the sea, and dragging down raises the view, as before
        controller.Orbit(camera, -deltaX * Settings.OrbitRadiansPerPixel, deltaY * Settings.OrbitRadiansPerPixel, CameraMotionKind::Drag);
        return;
    }

    if (dragMode == DragMode::PanViewPlane)
    {
        controller.PanViewPlane(camera, deltaX, deltaY, frame.Viewport);
        return;
    }

    const math::Float2 cursor = CursorPosition(input);
    const math::Float2 previousCursor{ cursor.x - deltaX, cursor.y - deltaY };
    controller.PanScreen(camera, previousCursor, cursor, frame.Viewport);
}

void MouseCameraInput::endDrag(ArcballController& controller) noexcept
{
    controller.EndManipulation();
    dragMode = DragMode::None;
    dragButton = input::ControlId::Invalid;
}

void MouseCameraInput::applyWheel(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) const noexcept
{
    const input::InputFrame& input = *frame.Input;
    const float notches = input.State(input::ControlId::WheelY).Delta;

    if (notches == 0.0f)
    {
        return;
    }

    controller.ZoomAt(camera, notches * Settings.ZoomLogScalePerNotch, CursorPosition(input), frame.Viewport, CameraMotionKind::Step);
}

void MouseCameraInput::applyFocusKey(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) const noexcept
{
    const input::InputFrame& input = *frame.Input;

    if (frame.UiCapturesKeyboard || !input.State(input::ControlId::KeyF).PressedThisFrame)
    {
        return;
    }

    controller.Focus(camera, CursorPosition(input), frame.Viewport, Settings.FocusSeconds);
}

namespace
{
    bool IsShiftDown(const input::InputFrame& input) noexcept
    {
        return input.State(input::ControlId::ShiftLeft).Down || input.State(input::ControlId::ShiftRight).Down;
    }

    math::Float2 CursorPosition(const input::InputFrame& input) noexcept
    {
        return math::Float2{ input.CursorX(), input.CursorY() };
    }
}
