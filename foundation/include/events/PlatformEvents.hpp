#pragma once
#ifndef FOUNDATION_PLATFORM_EVENT_TYPES_HPP
#define FOUNDATION_PLATFORM_EVENT_TYPES_HPP

namespace Events
{

    /**
     * @file PlatformEvents.hpp
     * @brief Defines event message structs related to the platform-specific windowing and input system.
     * To receive these events, implement a function that matches the signature `void(const EventDataType&, void* userData)`
     * and register it with the relevant platform event dispatcher.
     * @category Foundation
     */

    struct CursorPosEventData
    {
        double X{ 0.0 };
        double Y{ 0.0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using CursorPosEventFn = void(const CursorPosEventData&, void* userData);
    using CursorEnterEventFn = void(const int entered, void* userData);

    struct ScrollEventData
    {
        double OffsetX{ 0.0 };
        double OffsetY{ 0.0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using ScrollEventFn = void(const ScrollEventData&, void* userData);

    using CharEventFn = void(const unsigned int code_point, void* userData);

    struct PathDropEventData
    {
        int PathCount{ 0 };
        const char** Paths{ nullptr };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using PathDropEventFn = void(const PathDropEventData&, void* userData);

    struct MouseButtonEventData
    {
        int Button{ 0 };
        int Action{ 0 };
        int Mods{ 0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using MouseButtonEventFn = void(const MouseButtonEventData&, void* userData);

    struct KeyboardKeyEventData
    {
        int Key{ 0 };
        int Scancode{ 0 };
        int Action{ 0 };
        int Mods{ 0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using KeyboardKeyEventFn = void(const KeyboardKeyEventData&, void* userData);

    struct ShouldResizeEventData
    {
        int Width{ 0 };
        int Height{ 0 };
    };

    using ShouldResizeEventFn = void(const ShouldResizeEventData&, void* userData);

}

#endif //!FOUNDATION_PLATFORM_EVENT_TYPES_HPP
