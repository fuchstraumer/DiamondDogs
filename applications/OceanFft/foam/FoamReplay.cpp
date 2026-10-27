#include "FoamReplay.hpp"
#include "OceanFoam.hpp"
#include "OceanSimulation.hpp"
#include <algorithm>

namespace ocean
{
    void RecordFoamReplay(
        rhi::CommandBuffer& command_buffer,
        const OceanSimulation& simulation,
        OceanFoam& foam,
        uint32_t frame_index,
        double time_now,
        std::array<float, 2> camera_xz,
        float wind_direction_radians,
        float wind_speed)
    {
        const FoamSettings& settings = foam.Settings();
        const int32_t stepCount = std::max(settings.ReplaySteps, 1);
        const double duration = 3.0 * static_cast<double>(settings.DecayTime);
        const double stepSeconds = duration / static_cast<double>(stepCount);
        const std::array<double, 2> driftNow = foam.Drift();
        const std::array<double, 2> velocity = foam.DriftVelocity(wind_direction_radians, wind_speed);

        foam.SetDrift({ driftNow[0] - velocity[0] * duration, driftNow[1] - velocity[1] * duration });
        foam.Invalidate();

        for (int32_t step = 1; step <= stepCount; ++step)
        {
            // the last step uses time_now itself, so that rounding cannot leave the images a hair off this frame
            const double stepTime = step == stepCount ? time_now : time_now - duration + static_cast<double>(step) * stepSeconds;
            simulation.RecordTick(command_buffer, static_cast<float>(stepTime));
            foam.Advance(stepSeconds, camera_xz, wind_direction_radians, wind_speed);
            foam.RecordStep(command_buffer, frame_index);
        }

        // the sum of the steps differs from driftNow by rounding only. Setting it keeps the error from accumulating
        foam.SetDrift(driftNow);
    }
}
