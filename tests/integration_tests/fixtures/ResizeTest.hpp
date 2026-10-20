#pragma once
#ifndef DIAMOND_DOGS_INTEGRATION_TESTS_RESIZE_TEST_HPP
#define DIAMOND_DOGS_INTEGRATION_TESTS_RESIZE_TEST_HPP
#include <cstdint>

class PlatformWindowSystem;

/** @brief Returns true when `--resize` is on the command line. Windowed tests then resize their own window on a fixed
 *  schedule, so that an unattended run exercises swapchain recreation. Run at least 120 frames with it. */
[[nodiscard]] bool ParseResizeTest(int argc, char* argv[]);

/** @brief Resizes the window when `frame` is one of the scheduled frames, and returns it to its first size last. */
void ApplyScheduledResize(PlatformWindowSystem& platform_system, int32_t frame);

/** @brief Returns 0 when the scene recreated the swapchain once per scheduled resize and the swapchain extent matches
 *  the framebuffer, and 1 with a message otherwise. Use it as the exit code of the test. */
[[nodiscard]] int VerifyScheduledResize(const PlatformWindowSystem& platform_system, uint32_t swapchain_recreation_count);

#endif // !DIAMOND_DOGS_INTEGRATION_TESTS_RESIZE_TEST_HPP
