#include "PassStatements.hpp"

namespace loom
{

    ColorLoad ColorLoad::Keep() noexcept
    {
        ColorLoad load{};
        load.Kind = Kind::Keep;
        return load;
    }

    ColorLoad ColorLoad::Clear(float red, float green, float blue, float alpha) noexcept
    {
        ColorLoad load{};
        load.Kind = Kind::Clear;
        load.ClearColor = { red, green, blue, alpha };
        return load;
    }

    ColorLoad ColorLoad::DontCare() noexcept
    {
        ColorLoad load{};
        load.Kind = Kind::DontCare;
        return load;
    }

    DepthLoad DepthLoad::Keep() noexcept
    {
        DepthLoad load{};
        load.Kind = Kind::Keep;
        return load;
    }

    DepthLoad DepthLoad::Clear(float depth) noexcept
    {
        DepthLoad load{};
        load.Kind = Kind::Clear;
        load.ClearDepth = depth;
        return load;
    }

    DepthLoad DepthLoad::DontCare() noexcept
    {
        DepthLoad load{};
        load.Kind = Kind::DontCare;
        return load;
    }

}
