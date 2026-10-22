#pragma once
#ifndef VELOX_RHI_MATH_POLYNOMIALS_HPP
#define VELOX_RHI_MATH_POLYNOMIALS_HPP
#include <cstdint>

// Coefficients shared by the scalar and SIMD transcendentals, in one place so they cannot drift. A
// coefficient differing between the two gives two plausible answers off in the fifth decimal, which
// only the accuracy sweeps in tests/unit_tests/MathTests.cpp would catch.
//
// Two kinds here. Derived series (powers of ln2 over factorials, odd reciprocals from atanh) are
// re-derivable from their own comment. The minimax tables are DirectXMath's (MIT, Microsoft;
// g_XMSinCoefficients0/1 and g_XMCosCoefficients0/1) and are *not* Taylor values - they are perturbed
// to flatten worst-case error, which buys the same accuracy two degrees lower. Their resemblance to
// 1/6 and 1/120 is why an edit here is easy to make and hard to notice.
namespace velox::math::detail
{

// 2^f on [-0.5, 0.5]: 2^f = e^(f ln2), so term n is ln2^n / n!. Six terms leaves
// (ln2^7 / 7!) * 0.5^7 ~= 1.2e-7, under one float ulp - a minimax fit would save an op, not accuracy.
constexpr float k_Exp2Poly1 = 0.693147181f;    // ln2
constexpr float k_Exp2Poly2 = 0.240226507f;    // ln2^2 / 2
constexpr float k_Exp2Poly3 = 0.0555041087f;   // ln2^3 / 6
constexpr float k_Exp2Poly4 = 0.00961812911f;  // ln2^4 / 24
constexpr float k_Exp2Poly5 = 0.00133335581f;  // ln2^5 / 120
constexpr float k_Exp2Poly6 = 0.000154035304f; // ln2^6 / 720

// log2(m) via atanh: t = (m - 1) / (m + 1), log2(m) = (2 / ln2) * (t + t^3/3 + t^5/5 + ...).
// Folding the mantissa into [1/sqrt2, sqrt2) bounds |t| at ~0.1716, so terms through t^7 leave ~4e-8.
// Costs one divide to form t; a minimax polynomial in (m - 1) would drop it.
// todo-perf: fit that polynomial if Log2 ever shows up in a profile.
constexpr float k_Log2Scale = 2.88539008f;      // 2 / ln2
constexpr float k_Log2Poly1 = 1.0f;             // 1
constexpr float k_Log2Poly3 = 0.333333333f;     // 1/3
constexpr float k_Log2Poly5 = 0.2f;             // 1/5
constexpr float k_Log2Poly7 = 0.142857143f;     // 1/7

// sin / cos on [-pi/2, pi/2], DirectXMath minimax. sin is odd: x^3, x^5, x^7, x^9, x^11
constexpr float k_SinPoly3 = -0.166666667f;
constexpr float k_SinPoly5 = 0.00833333310f;
constexpr float k_SinPoly7 = -0.000198408744f;
constexpr float k_SinPoly9 = 2.7525562e-06f;
constexpr float k_SinPoly11 = -2.3889859e-08f;

// cos is even: x^2, x^4, x^6, x^8, x^10
constexpr float k_CosPoly2 = -0.5f;
constexpr float k_CosPoly4 = 0.041666638f;
constexpr float k_CosPoly6 = -0.0013888378f;
constexpr float k_CosPoly8 = 2.4760495e-05f;
constexpr float k_CosPoly10 = -2.6051615e-07f;

// Estimates: three terms, ~1e-5. Different *numbers* from the set above, not a truncation of it - a
// shorter minimax fit is refitted, which is why k_SinEstPoly3 is not k_SinPoly3
constexpr float k_SinEstPoly3 = -0.16665852f;
constexpr float k_SinEstPoly5 = 0.0083139502f;
constexpr float k_SinEstPoly7 = -0.00018524670f;

constexpr float k_CosEstPoly2 = -0.49992746f;
constexpr float k_CosEstPoly4 = 0.041493919f;
constexpr float k_CosEstPoly6 = -0.0012712436f;

// atan on [-1, 1], DirectXMath minimax (g_XMATanCoefficients0/1). Odd: x * (1 + x^2 * (...)).
// |x| > 1 reduces via atan(x) = pi/2 - atan(1/x), so this table covers the whole line
constexpr float k_ATanPoly3 = -0.3333314528f;
constexpr float k_ATanPoly5 = 0.1999355085f;
constexpr float k_ATanPoly7 = -0.1420889944f;
constexpr float k_ATanPoly9 = 0.1065626393f;
constexpr float k_ATanPoly11 = -0.0752896400f;
constexpr float k_ATanPoly13 = 0.0429096138f;
constexpr float k_ATanPoly15 = -0.0161657367f;
constexpr float k_ATanPoly17 = 0.0028662257f;

// Base conversions, so Exp/Log/Exp10/Log10 are all one multiply away from Exp2/Log2
constexpr float k_Log2OfE = 1.44269504f;
constexpr float k_LnOf2 = 0.693147181f;
constexpr float k_Log2Of10 = 3.32192809f;
constexpr float k_Log10Of2 = 0.301029996f;

// Reciprocal-sqrt seed: Lomont's refinement of the Quake constant. One Newton-Raphson step off this
// lands near 2e-3 relative, which is what ReciprocalSqrtEst promises on wasm. Improved constants and
// modified NR corrections: Walczyk, Moroz & Cieslinski, Entropy 23(1), 2021
constexpr uint32_t k_ReciprocalSqrtSeed = 0x5F375A86u;

} // namespace velox::math::detail

#endif // !VELOX_RHI_MATH_POLYNOMIALS_HPP
