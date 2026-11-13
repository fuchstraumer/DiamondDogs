#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_SPINE_NEEDS_HPP
#define DIAMOND_DOGS_BOOTSTRAP_SPINE_NEEDS_HPP
#include "DeviceRequirements.hpp"
#include <span>
#include <vector>

namespace bootstrap
{
    class WindowStage;

    /** @brief The needs of the GPU stage's own systems (ResourceContext: timeline semaphores, synchronization2, and
     *  memory budget as Wanted). Constant data. */
    [[nodiscard]] std::span<const Need> GpuSpineNeeds() noexcept;
    /** @brief The needs of a windowed spine: GpuSpineNeeds, then the window's needs. An app appends its own needs,
     *  then passes the list to GatherDeviceRequirements. */
    [[nodiscard]] std::vector<Need> SpineNeeds(const WindowStage& window);
    /** @brief The needs of a headless spine: GpuSpineNeeds. */
    [[nodiscard]] std::vector<Need> HeadlessSpineNeeds();
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_SPINE_NEEDS_HPP
