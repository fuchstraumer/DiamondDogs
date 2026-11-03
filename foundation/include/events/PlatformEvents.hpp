#pragma once
#ifndef FOUNDATION_PLATFORM_EVENT_TYPES_HPP
#define FOUNDATION_PLATFORM_EVENT_TYPES_HPP

namespace Events
{

    /**
     * @file PlatformEvents.hpp
     * @brief Defines event message structs related to the platform-specific windowing system. Input events go through `InputFrame`.
     * To receive these events, implement a function that matches the signature `void(const EventDataType&, void* userData)`
     * and register it with the relevant platform event dispatcher.
     * @category Foundation
     */

    struct ShouldResizeEventData
    {
        int Width{ 0 };
        int Height{ 0 };
    };

    using ShouldResizeEventFn = void(const ShouldResizeEventData&, void* userData);

}

#endif //!FOUNDATION_PLATFORM_EVENT_TYPES_HPP
