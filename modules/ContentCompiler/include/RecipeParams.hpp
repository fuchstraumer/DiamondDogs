#pragma once
#ifndef DIAMOND_DOGS_CC_RECIPE_PARAMS_HPP
#define DIAMOND_DOGS_CC_RECIPE_PARAMS_HPP

#include "ContentTypes.hpp"
#include "reflect/Describe.hpp"

#include <string_view>
#include <tuple>

namespace cc
{
    inline constexpr std::string_view k_RecipeParamsName = "cc.RecipeParams";

    // What a recipe key encodes as its parameters: the quality, then the kind's own parameters. The recipe
    // table writes this same stream through a type-erased path, and the tests compare the two.
    template<typename P>
    struct RecipeParams
    {
        Quality Level{ Quality::Invalid };
        P Params;
    };
}

namespace foundation::reflect
{
    template<typename P>
    struct Describe<cc::RecipeParams<P>>
    {
        using T = cc::RecipeParams<P>;

        static constexpr std::string_view Name = cc::k_RecipeParamsName;
        static constexpr std::tuple Fields
        {
            Field<T, cc::Quality>{ "Quality", &T::Level },
            Field<T, P>{ "Params", &T::Params },
        };
    };
}

#endif // !DIAMOND_DOGS_CC_RECIPE_PARAMS_HPP
