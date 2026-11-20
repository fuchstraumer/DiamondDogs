#pragma once
#ifndef DIAMOND_DOGS_LOOM_FORMATS_HPP
#define DIAMOND_DOGS_LOOM_FORMATS_HPP
#include <cstdint>
#include <optional>
#include <string_view>

// Loom declares image formats in Lodestone's vocabulary (TextureFormat), because reflection reports a storage image's
// format in it: the check is then ==. The enum is declared opaque here, so the header stays light; code that names an
// enumerator includes Lodestone's ResourceFlags.hpp.
namespace lodestone
{
    enum class TextureFormat : uint8_t;
}

struct ImageFormat;

namespace loom
{
    // The value 0 of TextureFormat: no format.
    inline constexpr lodestone::TextureFormat k_NoTextureFormat{};

    // The shim from foundation's ImageFormat: content the compiler imports, or a format the app chooses at run time
    // from what the device supports. No value when Lodestone has no such format; that is a request to Lodestone.
    [[nodiscard]] std::optional<lodestone::TextureFormat> ToTextureFormat(const ImageFormat& format) noexcept;

    // The enumerator name, for diagnostics. Stages never format a string, so a sink prints this name.
    [[nodiscard]] std::string_view TextureFormatName(lodestone::TextureFormat format) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_FORMATS_HPP
