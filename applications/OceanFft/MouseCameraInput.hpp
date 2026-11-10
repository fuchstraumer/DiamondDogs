#pragma once
#ifndef DIAMOND_DOGS_OCEAN_MOUSE_CAMERA_INPUT_HPP
#define DIAMOND_DOGS_OCEAN_MOUSE_CAMERA_INPUT_HPP
#include "ArcballCamera.hpp"
#include "ArcballController.hpp"
#include "InputFrame.hpp"
#include <cstdint>

/** @brief The size in pixels that one pixel of mouse motion turns the camera, and the scale of one wheel notch. */
struct MouseCameraSettings
{
    float OrbitRadiansPerPixel{ 0.005f };
    // one notch divides the distance by 1 / 0.9, as the wheel did before the controller existed
    float ZoomLogScalePerNotch{ 0.10536052f };
    float FocusSeconds{ 0.6f };
};

/** @brief What the mouse and keyboard buttons did to the camera this frame. */
struct MouseCameraFrame
{
    const input::InputFrame* Input{ nullptr };
    bool UiCapturesMouse{ false };
    bool UiCapturesKeyboard{ false };
    CameraViewport Viewport;
};

// Turns the mouse and keyboard of one InputFrame into ArcballController intents. This is the only place where the camera
// meets the desktop devices. A touch adapter calls the same intents from gestures.
//   left drag: orbit    right or middle drag: grab the sea and pan    shift with right or middle drag: pan in the view plane
//   wheel: zoom toward the cursor    F: focus the sea point under the cursor
class MouseCameraInput
{
public:

    MouseCameraSettings Settings;

    void Apply(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) noexcept;

private:

    enum class DragMode : uint8_t
    {
        Invalid = 0,
        None,
        Orbit,
        PanSea,
        PanViewPlane,
    };

    void startDragIfPressed(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) noexcept;
    void beginDrag(DragMode mode, input::ControlId button, ArcballCamera& camera, ArcballController& controller) noexcept;
    void continueDrag(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) const noexcept;
    void endDrag(ArcballController& controller) noexcept;
    void applyWheel(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) const noexcept;
    void applyFocusKey(const MouseCameraFrame& frame, ArcballCamera& camera, ArcballController& controller) const noexcept;

    DragMode dragMode{ DragMode::None };
    // the button that started the drag. The drag ends when that button goes up
    input::ControlId dragButton{ input::ControlId::Invalid };
};

#endif //!DIAMOND_DOGS_OCEAN_MOUSE_CAMERA_INPUT_HPP
