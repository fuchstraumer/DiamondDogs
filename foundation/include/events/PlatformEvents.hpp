#pragma once
#ifndef FOUNDATION_PLATFORM_EVENT_TYPES_HPP
#define FOUNDATION_PLATFORM_EVENT_TYPES_HPP

namespace Foundation::Events
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

    using CursorPosEvent = void(const CursorPosEventData&, void* userData);
    using CursorEnterEvent = void(const int entered, void* userData);

    struct ScrollEventData
    {
        double OffsetX{ 0.0 };
        double OffsetY{ 0.0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using ScrollEvent = void(const ScrollEventData&, void* userData);

    using CharCallbackEvent = void(const unsigned int code_point, void* userData);

    struct PathDropEventData
    {
        const char** Paths{ nullptr };
        int PathCount{ 0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using PathDropEvent = void(const PathDropEventData&, void* userData);

    struct MouseButtonEventData
    {
        int Button{ 0 };
        int Action{ 0 };
        int Mods{ 0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using MouseButtonEvent = void(const MouseButtonEventData&, void* userData);

    struct KeyboardKeyEventData
    {
        int Key{ 0 };
        int Scancode{ 0 };
        int Action{ 0 };
        int Mods{ 0 };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using KeyboardKeyEvent = void(const KeyboardKeyEventData&, void* userData);
    
}

#endif //!FOUNDATION_PLATFORM_EVENT_TYPES_HPP
