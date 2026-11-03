#include "ArcballController.hpp"
#include "ArcballCamera.hpp"
#include "math/Math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace
{
    // a hitch or a debugger break must not release all pending motion in one frame
    constexpr float k_MaxDeltaSeconds = 0.1f;
    constexpr float k_MinTimeConstant = 1.0e-3f;
    // the release inertia takes the average motion of this much drag time before the release
    constexpr double k_InertiaWindowSeconds = 0.08;
    constexpr float k_AngleEpsilon = 1.0e-5f;
    constexpr float k_LogScaleEpsilon = 1.0e-5f;
    constexpr float k_PanEpsilon = 1.0e-4f;
    constexpr float k_InertiaStopAngularSpeed = 0.01f;
    constexpr float k_InertiaStopLogSpeed = 0.005f;
    constexpr float k_InertiaStopPanSpeedPerDistance = 0.005f;
    constexpr float k_MinScaleRatio = 1.0e-3f;

    /** @brief Applies the fraction alpha of the remaining amount. Returns the applied amount. */
    [[nodiscard]] float ConsumeExponential(float& remaining, float& velocity, float alpha, float epsilon) noexcept;
    /** @brief Advances a critically damped spring that drives the remaining amount to zero, with the exact
     *  solution for the step. Returns the applied amount. */
    [[nodiscard]] float ConsumeSpring(float& remaining, float& velocity, float omega, float delta_seconds, float epsilon) noexcept;
    /** @brief Decays the velocity over the step. Returns the distance traveled in the step. */
    [[nodiscard]] float DecayInertia(float& velocity, float decay, float tau) noexcept;
    [[nodiscard]] float SmootherStep(float t) noexcept;
}

using namespace math;

void ArcballController::BeginManipulation(ArcballCamera& camera) noexcept
{
    cancelTransition();
    inertiaActive = false;
    inertiaVelocity = MotionSample{};
    camera.Reseat();

    manipulating = true;
    frameDeltas = MotionSample{};
    sampleHead = 0;
    sampleCount = 0;
}

void ArcballController::EndManipulation() noexcept
{
    if (!manipulating)
    {
        return;
    }

    manipulating = false;
    // the deltas since the last Update belong to a frame that has not ended yet. the last step length stands in for it
    pushSample(lastDeltaSeconds);

    if (!Settings.Inertia || sampleCount == 0)
    {
        return;
    }

    const int32_t newestIndex = (sampleHead + k_SampleCapacity - 1) % k_SampleCapacity;
    const double windowStart = samples[newestIndex].EndSeconds - k_InertiaWindowSeconds;
    MotionSample total{};

    for (int32_t age = 0; age < sampleCount; ++age)
    {
        const int32_t index = (newestIndex + k_SampleCapacity - age) % k_SampleCapacity;
        const MotionSample& sample = samples[index];

        if (sample.EndSeconds < windowStart)
        {
            break;
        }

        total.DurationSeconds += sample.DurationSeconds;
        total.Yaw += sample.Yaw;
        total.Pitch += sample.Pitch;
        total.PanX += sample.PanX;
        total.PanZ += sample.PanZ;
        total.LogScale += sample.LogScale;
        total.Twist += sample.Twist;
    }

    if (total.DurationSeconds <= 0.0f)
    {
        return;
    }

    const float inverseDuration = 1.0f / total.DurationSeconds;
    inertiaVelocity = MotionSample
    {
        .Yaw = total.Yaw * inverseDuration,
        .Pitch = total.Pitch * inverseDuration,
        .PanX = total.PanX * inverseDuration,
        .PanZ = total.PanZ * inverseDuration,
        .LogScale = total.LogScale * inverseDuration,
        .Twist = total.Twist * inverseDuration,
    };
    inertiaActive = true;
}

void ArcballController::Orbit(ArcballCamera& camera, float d_yaw_radians, float d_pitch_radians, CameraMotionKind kind) noexcept
{
    cancelTransition();

    if (smoothes(kind))
    {
        yawChannel.Remaining += d_yaw_radians;
        pitchChannel.Remaining += d_pitch_radians;
        return;
    }

    camera.Orbit(d_yaw_radians, d_pitch_radians);

    if (kind == CameraMotionKind::Drag)
    {
        recordDrag(MotionSample{ .Yaw = d_yaw_radians, .Pitch = d_pitch_radians });
    }
}

void ArcballController::PanScreen(ArcballCamera& camera, const Float2& from_pixels, const Float2& to_pixels, const CameraViewport& viewport) noexcept
{
    cancelTransition();
    const Float3 delta = computePanDelta(camera, from_pixels, to_pixels, viewport);

    if (smoothes(CameraMotionKind::Drag))
    {
        panXChannel.Remaining += delta.x;
        panZChannel.Remaining += delta.z;
        return;
    }

    camera.TranslateTarget(delta);
    recordDrag(MotionSample{ .PanX = delta.x, .PanZ = delta.z });
}

void ArcballController::PanViewPlane(ArcballCamera& camera, float dx_pixels, float dy_pixels, const CameraViewport& viewport) noexcept
{
    cancelTransition();
    camera.PanViewPlane(dx_pixels, dy_pixels, viewport);
}

void ArcballController::ZoomAt(ArcballCamera& camera, float log_scale, const Float2& cursor_pixels, const CameraViewport& viewport, CameraMotionKind kind) noexcept
{
    cancelTransition();
    // re-seating does not move the view, and it makes the distance match the surface under the view
    camera.Reseat();

    const std::optional<Float3> hit = camera.PickPlane(cursor_pixels, viewport);
    const Float3 anchor = hit.value_or(camera.Current.Target);
    zoomAnchor = anchor;

    if (smoothes(kind))
    {
        zoomChannel.Remaining += log_scale;
        return;
    }

    camera.ScaleAbout(anchor, log_scale);

    if (kind == CameraMotionKind::Drag)
    {
        recordDrag(MotionSample{ .LogScale = log_scale });
    }
}

void ArcballController::Gesture2(ArcballCamera& camera, const Float2& previous_centroid_pixels, const Float2& centroid_pixels, float scale_ratio, float twist_radians, float d_pitch_radians, const CameraViewport& viewport) noexcept
{
    cancelTransition();
    const float logScale = std::log(std::max(scale_ratio, k_MinScaleRatio));
    const Float3 panDelta = computePanDelta(camera, previous_centroid_pixels, centroid_pixels, viewport);
    // after the pan, the world point that was under the previous centroid is under the current one
    const std::optional<Float3> previousHit = camera.PickPlane(previous_centroid_pixels, viewport);
    const Float3 anchor = previousHit.value_or(camera.Current.Target + panDelta);
    // a counter-clockwise turn of the world, seen from above, is a clockwise turn of the rig around +y
    const float rigTwist = -twist_radians;
    zoomAnchor = anchor;
    twistAnchor = anchor;

    if (smoothes(CameraMotionKind::Drag))
    {
        panXChannel.Remaining += panDelta.x;
        panZChannel.Remaining += panDelta.z;
        zoomChannel.Remaining += logScale;
        twistChannel.Remaining += rigTwist;
        pitchChannel.Remaining += d_pitch_radians;
        return;
    }

    camera.TranslateTarget(panDelta);
    camera.ScaleAbout(anchor, logScale);
    camera.RotateAbout(anchor, rigTwist);
    camera.Orbit(0.0f, d_pitch_radians);
    recordDrag(MotionSample{ .Pitch = d_pitch_radians, .PanX = panDelta.x, .PanZ = panDelta.z, .LogScale = logScale, .Twist = rigTwist });
}

void ArcballController::Focus(ArcballCamera& camera, const Float2& cursor_pixels, const CameraViewport& viewport, float seconds) noexcept
{
    const std::optional<Float3> hit = camera.PickPlane(cursor_pixels, viewport);

    if (!hit.has_value())
    {
        return;
    }

    ArcballCamera::State goal = camera.Current;
    goal.Target = *hit;
    math::Float3 delta = camera.Position() - *hit;
    goal.Distance = delta.Length();
    TransitionTo(camera, goal, seconds);
}

void ArcballController::TransitionTo(ArcballCamera& camera, const ArcballCamera::State& goal, float seconds) noexcept
{
    Stop();

    if (seconds <= 0.0f)
    {
        camera.SetState(goal);
        return;
    }

    // a copy clamps the goal the same way the camera clamps any state
    ArcballCamera goalCamera = camera;
    goalCamera.SetState(goal);

    transition.Active = true;
    transition.DurationSeconds = seconds;
    transition.ElapsedSeconds = 0.0f;
    transition.From = camera.Current;
    transition.To = goalCamera.Current;
    transition.FromOrientation = camera.Orientation();
    transition.ToOrientation = goalCamera.Orientation();
}

void ArcballController::Stop() noexcept
{
    yawChannel = Channel{};
    pitchChannel = Channel{};
    zoomChannel = Channel{};
    twistChannel = Channel{};
    panXChannel = Channel{};
    panZChannel = Channel{};
    inertiaActive = false;
    inertiaVelocity = MotionSample{};
    transition.Active = false;
    frameDeltas = MotionSample{};
    sampleHead = 0;
    sampleCount = 0;
}

void ArcballController::Update(ArcballCamera& camera, float delta_seconds) noexcept
{
    const float deltaSeconds = std::clamp(delta_seconds, 0.0f, k_MaxDeltaSeconds);
    clockSeconds += static_cast<double>(deltaSeconds);
    lastDeltaSeconds = deltaSeconds;

    if (manipulating)
    {
        pushSample(deltaSeconds);
    }

    if (transition.Active)
    {
        advanceTransition(camera, deltaSeconds);
        return;
    }

    consumeChannels(camera, deltaSeconds);
    advanceInertia(camera, deltaSeconds);
}

bool ArcballController::IsMoving() const noexcept
{
    const std::array<const Channel*, 6> channels
    {
        &yawChannel,
        &pitchChannel,
        &zoomChannel,
        &twistChannel,
        &panXChannel,
        &panZChannel,
    };
    const auto isPending = [](const Channel* channel) { return channel->Remaining != 0.0f || channel->Velocity != 0.0f; };
    return inertiaActive || transition.Active || std::ranges::any_of(channels, isPending);
}

bool ArcballController::smoothes(CameraMotionKind kind) const noexcept
{
    if (kind == CameraMotionKind::Step)
    {
        return Settings.Cinematic || Settings.SmoothSteps;
    }

    return Settings.Cinematic;
}

void ArcballController::cancelTransition() noexcept
{
    transition.Active = false;
}

void ArcballController::recordDrag(const MotionSample& deltas) noexcept
{
    frameDeltas.Yaw += deltas.Yaw;
    frameDeltas.Pitch += deltas.Pitch;
    frameDeltas.PanX += deltas.PanX;
    frameDeltas.PanZ += deltas.PanZ;
    frameDeltas.LogScale += deltas.LogScale;
    frameDeltas.Twist += deltas.Twist;
}

void ArcballController::pushSample(float delta_seconds) noexcept
{
    frameDeltas.DurationSeconds = delta_seconds;
    frameDeltas.EndSeconds = clockSeconds;
    samples[sampleHead] = frameDeltas;
    sampleHead = (sampleHead + 1) % k_SampleCapacity;
    sampleCount = std::min(sampleCount + 1, k_SampleCapacity);
    frameDeltas = MotionSample{};
}

void ArcballController::consumeChannels(ArcballCamera& camera, float delta_seconds) noexcept
{
    const bool useSpring = Settings.Cinematic;
    const float omega = 2.0f / std::max(Settings.CinematicTime, k_MinTimeConstant);
    const float alpha = 1.0f - std::exp(-delta_seconds / std::max(Settings.DiscreteTau, k_MinTimeConstant));
    const auto consume = [&](Channel& channel, float epsilon)
    {
        return useSpring ? ConsumeSpring(channel.Remaining, channel.Velocity, omega, delta_seconds, epsilon) : ConsumeExponential(channel.Remaining, channel.Velocity, alpha, epsilon);
    };

    const float yaw = consume(yawChannel, k_AngleEpsilon);
    const float pitch = consume(pitchChannel, k_AngleEpsilon);
    const float logScale = consume(zoomChannel, k_LogScaleEpsilon);
    const float twist = consume(twistChannel, k_AngleEpsilon);
    const float panX = consume(panXChannel, k_PanEpsilon);
    const float panZ = consume(panZChannel, k_PanEpsilon);

    if (yaw != 0.0f || pitch != 0.0f)
    {
        camera.Orbit(yaw, pitch);
    }

    if (logScale != 0.0f)
    {
        camera.ScaleAbout(zoomAnchor, logScale);
    }

    if (twist != 0.0f)
    {
        camera.RotateAbout(twistAnchor, twist);
    }

    if (panX != 0.0f || panZ != 0.0f)
    {
        camera.TranslateTarget(Float3{ panX, 0.0f, panZ });
    }
}

void ArcballController::advanceInertia(ArcballCamera& camera, float delta_seconds) noexcept
{
    if (!inertiaActive)
    {
        return;
    }

    const float tau = std::max(Settings.InertiaTau, k_MinTimeConstant);
    const float decay = std::exp(-delta_seconds / tau);
    const float yaw = DecayInertia(inertiaVelocity.Yaw, decay, tau);
    const float pitch = DecayInertia(inertiaVelocity.Pitch, decay, tau);
    const float panX = DecayInertia(inertiaVelocity.PanX, decay, tau);
    const float panZ = DecayInertia(inertiaVelocity.PanZ, decay, tau);
    const float logScale = DecayInertia(inertiaVelocity.LogScale, decay, tau);
    const float twist = DecayInertia(inertiaVelocity.Twist, decay, tau);

    camera.Orbit(yaw, pitch);
    camera.TranslateTarget(Float3{ panX, 0.0f, panZ });
    camera.ScaleAbout(zoomAnchor, logScale);
    camera.RotateAbout(twistAnchor, twist);

    const float panSpeed = std::hypot(inertiaVelocity.PanX, inertiaVelocity.PanZ);
    const bool angularStopped = std::abs(inertiaVelocity.Yaw) < k_InertiaStopAngularSpeed && std::abs(inertiaVelocity.Pitch) < k_InertiaStopAngularSpeed && std::abs(inertiaVelocity.Twist) < k_InertiaStopAngularSpeed;
    const bool zoomStopped = std::abs(inertiaVelocity.LogScale) < k_InertiaStopLogSpeed;
    const bool panStopped = panSpeed < k_InertiaStopPanSpeedPerDistance * camera.Current.Distance;

    if (angularStopped && zoomStopped && panStopped)
    {
        inertiaActive = false;
        inertiaVelocity = MotionSample{};
    }
}

void ArcballController::advanceTransition(ArcballCamera& camera, float delta_seconds) noexcept
{
    transition.ElapsedSeconds += delta_seconds;
    const float progress = std::min(transition.ElapsedSeconds / transition.DurationSeconds, 1.0f);

    if (progress >= 1.0f)
    {
        camera.SetState(transition.To);
        transition.Active = false;
        return;
    }

    const float eased = SmootherStep(progress);
    const Quaternion orientation = Quaternion::Slerp(transition.FromOrientation, transition.ToOrientation, eased);
    const Float3 back = math::FromVector(orientation.RotateVector(Vector{ 0.0f, 0.0f, 1.0f, 0.0f }));
    // the slerp can carry a little roll between two turntable poses. reading yaw and pitch from the back axis drops it
    const float logDistance = std::lerp(std::log(transition.From.Distance), std::log(transition.To.Distance), eased);

    ArcballCamera::State state;
    state.Target = transition.From.Target + (transition.To.Target - transition.From.Target) * eased;
    state.Yaw = math::ATan2(back.x, back.z);
    state.Pitch = math::ASin(std::clamp(back.y, -1.0f, 1.0f));
    state.Distance = std::exp(logDistance);
    camera.SetState(state);
}

Float3 ArcballController::computePanDelta(const ArcballCamera& camera, const Float2& from_pixels, const Float2& to_pixels, const CameraViewport& viewport) const noexcept
{
    const std::optional<Float3> fromHit = camera.PickPlane(from_pixels, viewport);
    const std::optional<Float3> toHit = camera.PickPlane(to_pixels, viewport);

    if (fromHit.has_value() && toHit.has_value())
    {
        return *fromHit - *toHit;
    }

    // a pointer above the horizon has no plane point. pan in the horizontal plane at the target's pixel scale instead
    const float worldPerPixel = 2.0f * camera.Current.Distance * math::Tan(0.5f * camera.FovY) / std::max(viewport.Height, 1.0f);
    const ScalarSinCos yaw = math::SinCos(camera.Current.Yaw);
    const Float3 right{ yaw.cos, 0.0f, -yaw.sin };
    const Float3 heading{ -yaw.sin, 0.0f, -yaw.cos };
    const float deltaX = to_pixels.x - from_pixels.x;
    const float deltaY = to_pixels.y - from_pixels.y;
    return right * (-deltaX * worldPerPixel) + heading * (deltaY * worldPerPixel);
}

namespace
{
    float ConsumeExponential(float& remaining, float& velocity, float alpha, float epsilon) noexcept
    {
        velocity = 0.0f;
        float applied = remaining * alpha;
        remaining -= applied;

        if (std::abs(remaining) < epsilon)
        {
            applied += remaining;
            remaining = 0.0f;
        }

        return applied;
    }

    float ConsumeSpring(float& remaining, float& velocity, float omega, float delta_seconds, float epsilon) noexcept
    {
        // x(t) = (x0 + (v0 + w x0) t) e^(-w t), and v(t) = (v0 - w (v0 + w x0) t) e^(-w t)
        const float decay = std::exp(-omega * delta_seconds);
        const float drive = (velocity + omega * remaining) * delta_seconds;
        float nextRemaining = (remaining + drive) * decay;
        float nextVelocity = (velocity - omega * drive) * decay;

        if (std::abs(nextRemaining) < epsilon && std::abs(nextVelocity) < epsilon * omega)
        {
            nextRemaining = 0.0f;
            nextVelocity = 0.0f;
        }

        const float applied = remaining - nextRemaining;
        remaining = nextRemaining;
        velocity = nextVelocity;
        return applied;
    }

    float DecayInertia(float& velocity, float decay, float tau) noexcept
    {
        const float traveled = velocity * tau * (1.0f - decay);
        velocity *= decay;
        return traveled;
    }

    float SmootherStep(float t) noexcept
    {
        return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f);
    }

}
