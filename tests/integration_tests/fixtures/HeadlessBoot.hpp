#pragma once
#ifndef DIAMOND_DOGS_HEADLESS_BOOT_TEST_FIXTURE_HPP
#define DIAMOND_DOGS_HEADLESS_BOOT_TEST_FIXTURE_HPP
#include "ContentStage.hpp"
#include "CoreStage.hpp"
#include "DeviceStage.hpp"
#include "GpuStage.hpp"
#include "LaunchStage.hpp"
#include <span>

namespace bootstrap
{
    struct BundleRequest;
    struct DeviceConfig;
    struct Need;
}

/** The boot stages of a headless integration test. They are declared in start order, so they stop in reverse. */
struct HeadlessBoot
{
    bootstrap::LaunchStage Launch;
    bootstrap::CoreStage Core;
    bootstrap::ContentStage Content;
    bootstrap::DeviceStage Device;
    bootstrap::GpuStage Gpu;
};

/** @brief Starts every stage of `boot` with no window and no modules. The device gets the headless spine needs, then
 *  `app_needs`, then the needs of `bundles`. Prints the failure to stderr and returns false when a stage fails. */
[[nodiscard]] bool StartHeadlessBoot(HeadlessBoot& boot,
                                     std::span<char* const> arguments,
                                     std::span<const bootstrap::BundleRequest> bundles,
                                     std::span<const bootstrap::Need> app_needs,
                                     const bootstrap::DeviceConfig& config);

#endif // !DIAMOND_DOGS_HEADLESS_BOOT_TEST_FIXTURE_HPP
