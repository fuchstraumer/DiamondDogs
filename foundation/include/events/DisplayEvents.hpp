#pragma once
#ifndef FOUNDATION_EVENTS_DISPLAY_EVENTS_HPP
#define FOUNDATION_EVENTS_DISPLAY_EVENTS_HPP
#include <cstdint>

namespace Events
{

    enum class SwapchainRecreateStage : uint8_t
    {
        Invalid,
        Creation,
        BeginResize,
        CompleteResize
    };

    /** @brief Informs about the recreation of a swapchain, typically due to window resizing or format changes.
     *
     * This event provides the new dimensions, HDR capability, color space, and the associated window handle.
    */
    struct SwapchainRecreateEventData
    {
        SwapchainRecreateStage Stage{ SwapchainRecreateStage::Invalid };
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
    using SwapchainRecreatedEvent = void(const Events::SwapchainRecreatedEvent&, void* userData);
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
}

#endif // !FOUNDATION_EVENTS_DISPLAY_EVENTS_HPP
