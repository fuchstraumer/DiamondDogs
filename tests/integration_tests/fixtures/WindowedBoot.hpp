#pragma once
#ifndef DIAMOND_DOGS_WINDOWED_BOOT_TEST_FIXTURE_HPP
#define DIAMOND_DOGS_WINDOWED_BOOT_TEST_FIXTURE_HPP
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "DeviceStage.hpp"
#include "GpuStage.hpp"
#include "LaunchStage.hpp"
#include "WindowStage.hpp"
#include <span>

struct PlatformWindowCreateInfo;

namespace bootstrap
{
    struct BundleRequest;
    struct DeviceConfig;
    struct Need;
}

/** The boot stages of a windowed integration test. They are declared in start order, so they stop in reverse: the GPU
 *  stage (the swapchain), then the device stage (the surface), then the window. */
struct WindowedBoot
{
    bootstrap::LaunchStage Launch;
    bootstrap::CoreStage Core;
    bootstrap::ContentStage Content;
    bootstrap::WindowStage Window;
    bootstrap::DeviceStage Device;
    bootstrap::GpuStage Gpu;
};

/** @brief Starts every stage of `boot` with a window and no modules. The device gets the windowed spine needs, then
 *  `app_needs`, then the needs of `bundles`. The GPU stage creates the default swapchain. Prints the failure to stderr
 *  and returns false when a stage fails. */
[[nodiscard]] bool StartWindowedBoot(WindowedBoot& boot,
                                     std::span<char* const> arguments,
                                     std::span<const bootstrap::BundleRequest> bundles,
                                     std::span<const bootstrap::Need> app_needs,
                                     const bootstrap::DeviceConfig& config,
                                     const PlatformWindowCreateInfo& window_info);

#endif // !DIAMOND_DOGS_WINDOWED_BOOT_TEST_FIXTURE_HPP
