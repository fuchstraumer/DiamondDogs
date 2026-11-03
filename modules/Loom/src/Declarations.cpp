#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <string>
#include <string_view>

namespace loom
{

    AxisValue AxisValue::Unsigned(uint32_t value) noexcept
    {
        return AxisValue{ .Value = value };
    }

    AxisValue AxisValue::Boolean(bool value) noexcept
    {
        return AxisValue{ .Value = value };
    }

    AxisValue AxisValue::EnumCase(std::string_view value) noexcept
    {
        return AxisValue{ .Value = EnumName{ std::string{ value } } };
    }

    AxisValue AxisValue::Interface(std::string_view value) noexcept
    {
        return AxisValue{ .Value = InterfaceName{ std::string{ value } } };
    }

    SizeSource SizeSource::Derived() noexcept
    {
        SizeSource source{};
        source.Kind = Kind::Derived;
        return source;
    }

    SizeSource SizeSource::Bytes(int64_t byte_count) noexcept
    {
        SizeSource source{};
        source.Kind = Kind::Bytes;
        source.Size = byte_count;
        return source;
    }

    ExtentSource ExtentSource::Derived() noexcept
    {
        ExtentSource source{};
        source.Kind = Kind::Derived;
        return source;
    }

    ExtentSource ExtentSource::Fixed(int32_t width, int32_t height, int32_t depth) noexcept
    {
        return ExtentSource
        {
            .Kind = Kind::Fixed,
            .Width = width,
            .Height = height,
            .Depth = depth,
            .Target = {},
            .Scale = 1.0f
        };
    }

    ExtentSource ExtentSource::OfTarget(FrameTargetKey target, float scale) noexcept
    {
        ExtentSource source{};
        source.Kind = Kind::Target;
        source.Target = target.ToRef();
        source.Scale = scale;
        return source;
    }

    MipSource MipSource::One() noexcept
    {
        MipSource source{};
        source.Kind = Kind::None;
        source.Count = 1;
        return source;
    }

    MipSource MipSource::Full() noexcept
    {
        // -1: the whole chain, down to 1x1. Size works out the count once the extent is known.
        MipSource source{};
        source.Kind = Kind::Generated;
        source.Count = -1;
        return source;
    }

    MipSource MipSource::Levels(int32_t count) noexcept
    {
        MipSource source{};
        source.Kind = Kind::Generated;
        source.Count = count;
        return source;
    }

    ImageExpectation ImageExpectation::Exact(ImageFormat format, int32_t width, int32_t height, int32_t depth,
        int32_t mip_levels, int32_t layers) noexcept
    {
        return ImageExpectation
        {
            .Kind = Kind::Exact,
            .Format = format,
            .Width = width,
            .Height = height,
            .Depth = depth,
            .MipLevels = mip_levels,
            .Layers = layers
        };
    }

    ImageExpectation ImageExpectation::AnyExtent(ImageFormat format, int32_t layers) noexcept
    {
        ImageExpectation expectation{};
        expectation.Kind = Kind::AnyExtent;
        expectation.Format = format;
        expectation.Layers = layers;
        return expectation;
    }

    OutputFallback OutputFallback::Clear(float red, float green, float blue, float alpha) noexcept
    {
        OutputFallback fallback{};
        fallback.Kind = Kind::Clear;
        fallback.ClearColor = { red, green, blue, alpha };
        return fallback;
    }

    OutputFallback OutputFallback::KeepLast() noexcept
    {
        OutputFallback fallback{};
        fallback.Kind = Kind::KeepLast;
        return fallback;
    }

}
