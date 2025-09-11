#pragma once
#ifndef FOUNDATION_EVENTS_RHIEVENTS_HPP
#define FOUNDATION_EVENTS_RHIEVENTS_HPP
#include <cstdint>

/**
 * @file RhiEvents.hpp
 * @brief Defines event message structs related to the Rendering Hardware Interface (RHI) system. To receive these events,
 * implement a function that matches the signature `void(const EventDataType&, void* userData)` and register it with the relevant RHI system.
 * @category Foundation
*/

namespace Events
{
    /** @brief Informs about the recreation of a swapchain, typically due to window resizing or format changes.
     *
     * This event provides the new dimensions, HDR capability, color space, and the associated window handle.
    */
    struct SwapchainRecreatedEventData
    {
        uint64_t SwapchainHandle{ 0u }; // VkSwapchainKHR
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        bool HDRCapable{ false };
        uint32_t ColorSpace{ 0u }; // VkColorSpaceKHR
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    /**
     * @brief Event signature for swapchain recreation notifications. This type is used for creation, beginning resize, and completion of resize events.
     * @note Signature for destruction is `SwapchainDestroyedEvent`.
     */
    using SwapchainRecreatedEvent = void(const SwapchainRecreatedEventData&, void* userData);
    /** @brief Event signature for swapchain destruction notifications. */
    using SwapchainDestroyedEvent = void(const uint64_t swapchainHandle, void* userData);

    /** @brief Event type for window resize notifications.
     *
     * Listeners can register callbacks matching this signature to be informed
     * whenever a window is resized.
    */
    struct WindowResizeEventData
    {
        uint32_t Width{ 0u };
        uint32_t Height{ 0u };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    /** @brief Informs about a change in the High Dynamic Range (HDR) capabilities of a window's swapchain.
     *
     * This event is triggered when the HDR support status or related parameters change, allowing systems to
     * adjust their rendering and presentation logic accordingly.
    */
    struct HDRCapabilityChangeEventData
    {
        bool HDRCapable{ false };
        uint32_t ColorSpace{ 0u }; // VkColorSpaceKHR
        uint32_t MinLuminance{ 0u };
        uint32_t MaxLuminance{ 0u };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using HDRCapabilityChangeEvent = void(const HDRCapabilityChangeEventData&, void*);

    /** @brief Informs about a change in the Multi-Sample Anti-Aliasing (MSAA) settings for a window's swapchain.
     *
     * This event is triggered when the MSAA sample count is changed, allowing systems to adjust their rendering
     * pipelines accordingly.
     */
    struct MSAAChangeEventData
    {
        uint32_t SampleCount{ 1u };
        void* WindowHandle{ nullptr }; // GLFWwindow*, for now
    };

    using MSAAChangeEvent = void(const MSAAChangeEventData&, void*);

}

#endif //!FOUNDATION_EVENTS_RHIEVENTS_HPP
