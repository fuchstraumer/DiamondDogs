#pragma once
#ifndef DIAMOND_DOGS_OCEAN_SEA_STATE_REFLECT_HPP
#define DIAMOND_DOGS_OCEAN_SEA_STATE_REFLECT_HPP
#include "SeaState.hpp"
#include "reflect/Describe.hpp"
#include <string_view>
#include <tuple>

// The limits come from the clamps in the pipe and from the field comments. The pipe does not clamp Depth, and
// a negative depth makes a NaN in the depth correction, so the 0.1 m minimum is a chosen value. The UI sliders
// are often narrower than these limits: the Depth slider is 2 to 500.
template<>
struct foundation::reflect::Describe<ocean::SeaState>
{
    using SeaState = ocean::SeaState;

    static constexpr std::string_view Name{ "ocean.SeaState" };
    static constexpr std::string_view Doc{
        "The editor domain. Every value is in units an artist can judge by eye" };

    static constexpr std::tuple Fields
    {
        Field<SeaState, float>
        {
            "WindForce",
            &SeaState::WindForce,
            {
                .Unit = "Bft",
                .Range = FieldRange{ 0.0, 12.0 },
                .Doc =
                    "Beaufort force, 0 (calm) to 12 (hurricane), continuous",
            },
        },
        Field<SeaState, float>
        {
            "Openness",
            &SeaState::Openness,
            {
                .Range = FieldRange{ 0.0, 1.0 },
                .Doc =
                    "0: sheltered water, the wind has 5 km to work with. 1: open ocean, a fully developed "
                    "sea",
            },
        },
        Field<SeaState, float>
        {
            "WindDirection",
            &SeaState::WindDirection,
            {
                .Unit = "deg",
                .Doc =
                    "Where the wind sea travels towards, in degrees",
            },
        },
        Field<SeaState, float>
        {
            "Spread",
            &SeaState::Spread,
            {
                .Range = FieldRange{ 0.0, 1.0 },
                .Doc =
                    "0: waves line up with the wind. 1: a confused sea, waves from many directions",
            },
        },
        Field<SeaState, float>
        {
            "Regularity",
            &SeaState::Regularity,
            {
                .Range = FieldRange{ 0.0, 3.0 },
                .Doc =
                    "0: a confused, broad-banded sea. 1: clean, regular wave groups (JONSWAP gamma 1 to 7, "
                    "up to 19 at 3.0)",
            },
        },
        Field<SeaState, float>
        {
            "RippleDetail",
            &SeaState::RippleDetail,
            {
                .Range = FieldRange{ 0.0, 1.0 },
                .Doc =
                    "0: the short ripples are smoothed away. 1: every ripple down to the finest cascade",
            },
        },
        Field<SeaState, float>
        {
            "Depth",
            &SeaState::Depth,
            {
                .Unit = "m",
                .Range = FieldRange{ 0.1, k_NoLimit },
                .Doc =
                    "Water depth in meters. Shallow water slows and steepens the long waves",
            },
        },
        Field<SeaState, float>
        {
            "SwellHeight",
            &SeaState::SwellHeight,
            {
                .Unit = "m",
                .Range = FieldRange{ 0.0, k_NoLimit },
                .Doc =
                    "Significant height of the swell alone, in meters. 0 turns the swell off",
            },
        },
        Field<SeaState, float>
        {
            "SwellPeriod",
            &SeaState::SwellPeriod,
            {
                .Unit = "s",
                .Range = FieldRange{ 2.0, k_NoLimit },
                .Doc =
                    "Peak period of the swell, in seconds (8: short swell, 16: long ocean swell)",
            },
        },
        Field<SeaState, float>
        {
            "SwellDirection",
            &SeaState::SwellDirection,
            {
                .Unit = "deg",
                .Doc =
                    "Where the swell travels towards, in degrees",
            },
        },
        Field<SeaState, float>
        {
            "SwellOrder",
            &SeaState::SwellOrder,
            {
                .Range = FieldRange{ 0.0, 1.0 },
                .Doc =
                    "0: a loose swell, mixed directions and periods. 1: long, parallel, regular crests",
            },
        },
        Field<SeaState, float>
        {
            "HeightScale",
            &SeaState::HeightScale,
            {
                .Range = FieldRange{ 0.0, k_NoLimit },
                .Doc =
                    "The artist's multiplier on every wave height. 1 is physical",
            },
        },
        Field<SeaState, float>
        {
            "WhitecapThreshold",
            &SeaState::WhitecapThreshold,
            {
                .Range = FieldRange{ 0.0, 1.0 },
                .Doc =
                    "User-defined threshold for whitecap formation, in the range 0-1. Maps to [-0.2, 0.2] in "
                    "shader",
            },
        },
        Field<SeaState, float>
        {
            "FoamAmount",
            &SeaState::FoamAmount,
            {
                .Range = FieldRange{ 0.0, k_NoLimit },
                .Doc =
                    "Linear foam coverage multiplier. 0: none, 1: physical reference, 2+: heavy foam",
            },
        },
        Field<SeaState, float>
        {
            "CrestSharpness",
            &SeaState::CrestSharpness,
            {
                .Range = FieldRange{ 0.0, 1.0 },
                .Doc =
                    "Shape of the whitecap coverage, in the range 0-1. 0: Dupuy's Jacobian determinant "
                    "(blobs). 1: the smaller eigenvalue of the Jacobian tensor (crests as sharp lines). 0.2 "
                    "keeps the determinant's varied foam and pulls it onto the crest (the author's lookdev, "
                    "0.1 to 0.5 reads well)",
            },
        },
    };
};

// a new SeaState field changes the size, so this fails until the field is in the descriptor too
static_assert(sizeof(ocean::SeaState) == 15 * sizeof(float), "add the new SeaState field to its descriptor");

#endif //!DIAMOND_DOGS_OCEAN_SEA_STATE_REFLECT_HPP
