#pragma once
#ifndef DIAMOND_DOGS_OCEAN_ARCBALL_CONTROLLER_HPP
#define DIAMOND_DOGS_OCEAN_ARCBALL_CONTROLLER_HPP
#include "ArcballCamera.hpp"
#include "math/Math.hpp"
#include <array>
#include <cstdint>

/** @brief Tells the controller how an input moves the camera. */
enum class CameraMotionKind : uint8_t
{
    Invalid = 0,
    // follows a pointer: applies at once and feeds the release inertia. Cinematic mode smooths it
    Drag,
    // a discrete step, such as a wheel notch, a key, or a script command: smoothed when smoothing is on
    Step,
};

// Turns input intents into camera motion, with smoothing, inertia and timed transitions. It knows no input device:
// adapters convert mouse, wheel, keyboard or touch input into these calls. It stores motion state only. The pose stays
// in the ArcballCamera that each call takes, so one controller never keeps a reference to a camera.
class ArcballController
{
public:

    struct Smoothing
    {
        // spreads each Step over DiscreteTau seconds of exponential decay instead of applying it in one frame
        bool SmoothSteps{ true };
        float DiscreteTau{ 0.07f };
        // drives every channel, drags included, with a critically damped spring of this smooth time
        bool Cinematic{ false };
        float CinematicTime{ 0.45f };
        // keeps a drag moving after release, decaying over InertiaTau seconds
        bool Inertia{ true };
        float InertiaTau{ 0.30f };
    };

    Smoothing Settings;

    /** @brief Starts a pointer drag: stops transitions and inertia, and re-seats the pivot on the reference plane. */
    void BeginManipulation(ArcballCamera& camera) noexcept;
    /** @brief Ends a pointer drag and starts the release inertia from the motion of its last frames. */
    void EndManipulation() noexcept;

    void Orbit(ArcballCamera& camera, float d_yaw_radians, float d_pitch_radians, CameraMotionKind kind) noexcept;
    /** @brief Moves the world with the pointer: the reference plane point under from_pixels ends up under to_pixels. */
    void PanScreen(ArcballCamera& camera, const math::Float2& from_pixels, const math::Float2& to_pixels, const CameraViewport& viewport) noexcept;
    /** @brief Moves the target in the view plane, with no smoothing and no inertia. */
    void PanViewPlane(ArcballCamera& camera, float dx_pixels, float dy_pixels, const CameraViewport& viewport) noexcept;
    /** @brief Zooms toward the reference plane point under the cursor, or toward the target when there is none.
     *  Positive log_scale moves closer: one log unit divides the distance by e. */
    void ZoomAt(ArcballCamera& camera, float log_scale, const math::Float2& cursor_pixels, const CameraViewport& viewport, CameraMotionKind kind) noexcept;
    /** @brief Applies one frame of a two-pointer gesture as one step, about the plane point under the centroid.
     *  scale_ratio is the pointer spread now over the spread before, so above 1 zooms in. twist_radians is
     *  positive for a counter-clockwise turn as the user sees the screen. */
    void Gesture2(ArcballCamera& camera, const math::Float2& previous_centroid_pixels, const math::Float2& centroid_pixels, float scale_ratio, float twist_radians, float d_pitch_radians, const CameraViewport& viewport) noexcept;
    /** @brief Starts a transition that centers the plane point under the cursor, at the same range from the eye. */
    void Focus(ArcballCamera& camera, const math::Float2& cursor_pixels, const CameraViewport& viewport, float seconds) noexcept;
    /** @brief Starts a transition from the camera's current state to the goal: slerp for the orientation, a
     *  logarithmic blend for the distance, and a linear blend for the target. Zero seconds applies the goal at once. */
    void TransitionTo(ArcballCamera& camera, const ArcballCamera::State& goal, float seconds) noexcept;
    /** @brief Drops all pending motion, inertia and transitions. Call it before an absolute camera change. */
    void Stop() noexcept;

    /** @brief Advances the pending motion by one frame. delta_seconds clamps to a short maximum, so that a hitch
     *  does not release all pending motion at once. */
    void Update(ArcballCamera& camera, float delta_seconds) noexcept;
    [[nodiscard]] bool IsMoving() const noexcept;

private:

    struct Channel
    {
        float Remaining{ 0.0f };
        float Velocity{ 0.0f };
    };

    struct MotionSample
    {
        float DurationSeconds{ 0.0f };
        double EndSeconds{ 0.0 };
        float Yaw{ 0.0f };
        float Pitch{ 0.0f };
        float PanX{ 0.0f };
        float PanZ{ 0.0f };
        float LogScale{ 0.0f };
        float Twist{ 0.0f };
    };

    struct Transition
    {
        bool Active{ false };
        float DurationSeconds{ 0.0f };
        float ElapsedSeconds{ 0.0f };
        ArcballCamera::State From;
        ArcballCamera::State To;
        math::Quaternion FromOrientation;
        math::Quaternion ToOrientation;
    };

    static constexpr int32_t k_SampleCapacity{ 64 };

    [[nodiscard]] bool smoothes(CameraMotionKind kind) const noexcept;
    void cancelTransition() noexcept;
    void recordDrag(const MotionSample& deltas) noexcept;
    void pushSample(float delta_seconds) noexcept;
    void consumeChannels(ArcballCamera& camera, float delta_seconds) noexcept;
    void advanceInertia(ArcballCamera& camera, float delta_seconds) noexcept;
    void advanceTransition(ArcballCamera& camera, float delta_seconds) noexcept;
    [[nodiscard]] math::Float3 computePanDelta(const ArcballCamera& camera, const math::Float2& from_pixels, const math::Float2& to_pixels, const CameraViewport& viewport) const noexcept;

    Channel yawChannel;
    Channel pitchChannel;
    Channel zoomChannel;
    Channel twistChannel;
    Channel panXChannel;
    Channel panZChannel;
    math::Float3 zoomAnchor;
    math::Float3 twistAnchor;

    bool manipulating{ false };
    MotionSample frameDeltas;
    std::array<MotionSample, k_SampleCapacity> samples{};
    int32_t sampleHead{ 0 };
    int32_t sampleCount{ 0 };

    bool inertiaActive{ false };
    MotionSample inertiaVelocity;

    Transition transition;
    double clockSeconds{ 0.0 };
    float lastDeltaSeconds{ 0.0f };
};

#endif //!DIAMOND_DOGS_OCEAN_ARCBALL_CONTROLLER_HPP
