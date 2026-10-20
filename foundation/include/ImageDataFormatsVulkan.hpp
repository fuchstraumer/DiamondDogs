#pragma once
#ifndef FOUNDATION_IMAGE_DATA_FORMATS_VULKAN_HPP
#define FOUNDATION_IMAGE_DATA_FORMATS_VULKAN_HPP
#include "ImageDataFormats.hpp"
#include <vulkan/vulkan_core.h>

/** @brief Converts between ImageFormat and VkFormat. Split from ImageDataFormats.hpp, so that only Vulkan code
 *  includes the Vulkan headers. */

/**
 * @brief Convert ImageFormat to VkFormat
 * @param format ImageFormat to convert
 * @return Corresponding VkFormat value
 */
VkFormat ToVkFormat(const ImageFormat& format) noexcept;

/**
 * @brief Convert VkFormat to ImageFormat
 * @param format VkFormat to convert
 * @return Corresponding ImageFormat struct
*/
ImageFormat FromVkFormat(VkFormat format) noexcept;

#endif // !FOUNDATION_IMAGE_DATA_FORMATS_VULKAN_HPP