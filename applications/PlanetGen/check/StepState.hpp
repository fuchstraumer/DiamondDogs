#pragma once
#ifndef DIAMOND_DOGS_PLANET_STEP_STATE_HPP
#define DIAMOND_DOGS_PLANET_STEP_STATE_HPP

namespace planet
{
    class GpuBuffers;
    class PlanetShaders;
    class ReferenceSet;

    /** Whether the outputs of the step get a readback copy. The check reads them; the bench does not. */
    enum class StepReadback : bool
    {
        None = false,
        Outputs = true,
    };

    /** @brief Creates every buffer that PlanetStep binds, from the reference state at the start of the step: the inputs,
     *  the crust arrays and their initial copies, the uniform buffers, and the outputs and counters (zero). The crust
     *  flags use the storage of `shaders`. Prints the cause and returns false when a file or a buffer fails. */
    [[nodiscard]] bool AddStepBuffers(GpuBuffers& buffers, const PlanetShaders& shaders, const ReferenceSet& reference, StepReadback readback);
}

#endif // !DIAMOND_DOGS_PLANET_STEP_STATE_HPP
