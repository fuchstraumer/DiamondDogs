#pragma once
#ifndef DIAMOND_DOGS_OCEAN_FOAM_REPLAY_HPP
#define DIAMOND_DOGS_OCEAN_FOAM_REPLAY_HPP
#include <array>
#include <cstdint>

namespace rhi
{
    struct CommandBuffer;
}

namespace ocean
{
    class OceanFoam;
    class OceanSimulation;

    /** @brief Rebuilds the foam for `time_now` from nothing. The spectrum is analytic in time, so the foam is the
     *  only state, and its history can be replayed exactly: this records ReplaySteps steps over three decay times
     *  (a tick of the simulation, then a foam step), with the drift moved back by the wind. The last step lands at
     *  `time_now` with the drift at its current value, so the Displacement and Whitecaps images match the frame, and
     *  the caller records no tick or foam step after it. The replay records no GPU timer scopes */
    void RecordFoamReplay(
        rhi::CommandBuffer& command_buffer,
        const OceanSimulation& simulation,
        OceanFoam& foam,
        uint32_t frame_index,
        double time_now,
        std::array<float, 2> camera_xz,
        float wind_direction_radians,
        float wind_speed);
}

#endif //!DIAMOND_DOGS_OCEAN_FOAM_REPLAY_HPP
