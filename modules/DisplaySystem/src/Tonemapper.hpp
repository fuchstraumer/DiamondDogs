#pragma once
#ifndef DIAMOND_DOGS_DISPLAY_SYSTEM_TONEMAPPER_HPP
#define DIAMOND_DOGS_DISPLAY_SYSTEM_TONEMAPPER_HPP
#include "Math.hpp"
#include <algorithm>
#include <cmath>

// tonemapper b,c term calculator from scene inputs
// "b" term controls the clipping point, set based on the maximum brightness of the scene currently
// "c" term sets how rapidly compression applies, especially in the shoulder of the curve
// Sourced from https://github.com/GPUOpen-LibrariesAndSDKs/Cauldron/blob/623a4b8eb587a8e08c9912dabe1a5b63e4ecc378/src/VK/shaders/tonemappers.glsl
// originally used T. Lottes GDC talk, but there appear to be some minor errors in the slide deck potentially? at least as presented.

constexpr float CalculateClippingPoint(const float hdrMax, 
                                       const float contrast, const float highlightContrast,
                                       const float midInput, const float midOutput) noexcept
{
    const float powMidInContrast = std::powf(midInput, contrast);
    const float powHdrMaxContrast = std::powf(hdrMax, contrast);
    const float powHdrMaxContrastHc = std::powf(hdrMax, contrast * highlightContrast);
    const float powMidInContrastHc = std::powf(midInput, contrast * highlightContrast);
    // don't look at me like that. you don't know what the reference implementation of this looked like.
    const float addendPt1 = midOutput * (powHdrMaxContrastHc * powMidInContrast);
    const float addendPt2 = powHdrMaxContrast * powMidInContrastHc * midOutput;

    const float dividend = -powMidInContrast + (addendPt1 - addendPt2);
    const float divisor = (powHdrMaxContrastHc * midOutput) - (powMidInContrastHc * midOutput);
    
    const float result = dividend / divisor;
    // but wait! there's another divide!
    const float finalResult = result / (powMidInContrastHc * midOutput);
    // and don't forget the negation
    return -finalResult;
}
constexpr float CalculateCompressionSlope(const float hdrMax,
                                          const float contrast, const float highlightContrast,
                                          const float midpointIn, const float midpointOut) noexcept
{
    const float powMidInContrast = std::powf(midpointIn, contrast);
    const float powHdrMaxContrast = std::powf(hdrMax, contrast);
    const float powMidInContrastHC = std::powf(midpointIn, contrast * highlightContrast);
    const float powHdrMaxContrastHC = std::powf(hdrMax, contrast * highlightContrast);
    // breaking this down into inanely small steps to make sure I don't mess the math up in translating it
    const float dividendPt1 = powHdrMaxContrastHC * powMidInContrast;
    const float dividendPt2 = powHdrMaxContrast * powMidInContrastHC * midpointOut;
    const float divisorPt1 = powHdrMaxContrastHC * midpointOut;
    const float divisorPt2 = powMidInContrastHC * midpointOut;
    const float dividend = dividendPt1 - dividendPt2;
    const float divisor = divisorPt1 - divisorPt2;

    return dividend / divisor;
}

constexpr float LottesTonemapOperator(const float x, const math::Float4& p)
{
    const float z = std::powf(x, p.r);
    return z / (std::powf(z, p.g) * p.b + p.a);
}

/**
 * @brief Applies a tonemapping curve as explained by T. Lottes in his 2017 GDC presentation.
 * This is the CPU side version of the algorithm, mostly here for debugging and parity reasons.
 * @param color The input HDR color to be tonemapped.
 * @param hdrMax How much HDR range before clipping. average for HDR ~25, SDR ~10-15. Default: 16.0f
 * @param contrast Baseline parameter to tune to adjust tonemapper contrast. Default: 2.0f
 * @param highlightContrast Adjusts contrast in the "highlight" zone at the peak of tone curve. Default: 1.0f
 * @param midpointIn What to consider as the midpoint of our input range, i.e. the greypoint? Default: 0.18f
 * @param midpointOut What to use as the output midpoint, setting the range of our output tonemapped values. for LDR this is just 1:1 Default: 0.18f
 * @param channelCrosstalk Amount of color channel crosstalk to apply, to avoid desaturation at high brightness. Default: 4.0f
 * @return The tonemapped color.
 */
math::Float3 TimothyTonemapper(const math::Float3& color,
                               const float hdrMax = 16.0f,
                               const float contrast = 2.0f,
                               const float highlightContrast = 1.0f,
                               const float midpointIn = 0.18f,
                               const float midpointOut = 0.18f,
                               const float channelCrosstalk = 4.0f) noexcept
{
    using namespace math;
    const float clippingPoint = CalculateClippingPoint(hdrMax, contrast, highlightContrast, midpointIn, midpointOut);
    const float compressionSlope = CalculateCompressionSlope(hdrMax, contrast, highlightContrast, midpointIn, midpointOut);

    // find peak channel, that's what we use to se the ratio
    const float peakChannel = std::max(std::numeric_limits<float>::epsilon(), std::max(color.r, std::max(color.g, color.b)));

    const Float3 ratio = color / peakChannel;
    const Float4 pParam{ contrast, highlightContrast, clippingPoint, compressionSlope };
    const float tonemappedPeak = LottesTonemapOperator(peakChannel, pParam);

    // precalculate some terms and broadcast to vectors
    const float crossSaturation = contrast * 16.0f;
    const Vector crossSaturationVector = Vector::Replicate(crossSaturation);
    // is used in a divide in the reference math, so pre-invert it to turn it into a multiply
    const Vector crossSaturationInvVector = Vector::Replicate(1.0f / crossSaturation);
    const float crossTalkTerm = std::powf(peakChannel, channelCrosstalk);
    
    const Vector White{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vector RatioVec = ToVector(ratio);
    // pow(abs(ratio), vec3(contrast / crossSaturation))
    RatioVec = Vector::Pow(Vector::Abs(RatioVec), Vector::Replicate(contrast) * crossSaturationInvVector);
    // mix(ratio, vec3(1.0f), vec3(pow(peak, crosstalk)))
    RatioVec = RatioVec.Lerp(White, crossTalkTerm);
    // pow(abs(ratio), vec3(crossSaturation))
    RatioVec = Vector::Pow(Vector::Abs(RatioVec), crossSaturationVector);
    const Float3 finalRatio = FromVector<Float3>(RatioVec);

    const Float3 finalColor = peakChannel * finalRatio;
    return finalColor;
}

#endif //!DIAMOND_DOGS_DISPLAY_SYSTEM_TONEMAPPER_HPP
