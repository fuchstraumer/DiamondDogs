#pragma once
#ifndef DIAMOND_DOGS_FRAME_CAPTURE_CAPTURE_CONVERSION_HPP
#define DIAMOND_DOGS_FRAME_CAPTURE_CAPTURE_CONVERSION_HPP
#include "FrameCapture.hpp"
#include "ImageDataFormats.hpp"
#include "math/Math.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The CPU side of a capture: the copied texels to linear BT.709 and to 8-bit sRGB. No GPU and no file. The writer
// thread calls these, and frame_capture_tests tests them against the double reference in ColorPipe.
namespace capture::conversion
{
    /** @brief The bytes of one texel that the PNG path converts, or 0 when it cannot convert the format */
    [[nodiscard]] uint32_t BytesPerPixel(ImageFormat format) noexcept;

    /** @brief Clamps to [0, 1] (NaN goes to 0), applies the sRGB curve, and rounds to 8 bits */
    [[nodiscard]] uint8_t EncodeSrgb(float linear) noexcept;
    [[nodiscard]] float DecodeSrgb(float encoded) noexcept;

    /** @brief The ST 2084 (PQ) signal to scRGB: nits / 80. The signal is clamped to [0, 1] */
    [[nodiscard]] float DecodePq(float encoded) noexcept;
    void Bt2020ToBt709(float& r, float& g, float& b) noexcept;

    /** @brief Returns texel `index` as linear BT.709, the same decode that ConvertToSrgb8 does before its encode. An
     *  sRGB source is decoded with the sRGB curve, and takes no LinearScale, as in ConvertToSrgb8 */
    [[nodiscard]] math::Float3 DecodeLinear(std::span<const std::byte> pixels, size_t index, ImageFormat format, CaptureEncoding encoding, float linear_scale);

    /** @brief Converts the copied texels to tightly packed 8-bit sRGB RGB, dropping alpha */
    [[nodiscard]] std::vector<uint8_t> ConvertToSrgb8(std::span<const std::byte> pixels, uint32_t width, uint32_t height, ImageFormat format, CaptureEncoding encoding, float linear_scale);

    /** @brief Averages each factor x factor block in linear light, and encodes the result as 8-bit sRGB RGB. Each value
     *  is clamped to [0, 1] before the average, as the encode would clamp it */
    [[nodiscard]] std::vector<uint8_t> DownsampleToSrgb8(std::span<const std::byte> pixels, uint32_t width, uint32_t height, uint32_t factor,
                                                         ImageFormat format, CaptureEncoding encoding, float linear_scale, uint32_t& out_width, uint32_t& out_height);

    /** @brief Upper-case hexadecimal, two digits per byte, in memory order */
    [[nodiscard]] std::string ToHex(std::span<const std::byte> bytes);
}

#endif // !DIAMOND_DOGS_FRAME_CAPTURE_CAPTURE_CONVERSION_HPP
