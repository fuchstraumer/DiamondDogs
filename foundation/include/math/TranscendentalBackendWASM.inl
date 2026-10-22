#pragma once
// WASM SIMD128 transcendentals for math::Vector. Included from MathBackendWASM.inl; include nothing.
//
// All of these range-reduce, evaluate a Horner polynomial, then reapply what was factored out.
// Reduction uses VectorMask + Select rather than branching: four lanes can want different branches.
namespace velox::math
{
namespace detail
{
    // Explicit MulAdd, not a * x + b: strict FP rules forbid the compiler contracting that into an
    // FMA, so spelling it out is the only way to get one.

    // 2^f for f in [-0.5, 0.5]. Six-term exponential series, ~1.2e-7 absolute
    VX_MATH_FORCEINLINE v128_t Exp2Fraction(v128_t fraction) noexcept
    {
        v128_t polynomial = wasm_f32x4_const_splat(k_Exp2Poly6);
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly5));
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly4));
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly3));
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly2));
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly1));
        return MulAdd(polynomial, fraction, wasm_f32x4_const_splat(1.0f));
    }

    // Four terms; next term bounds error at (ln2^5 / 5!) * 0.5^5 ~= 4e-5
    VX_MATH_FORCEINLINE v128_t Exp2FractionEst(v128_t fraction) noexcept
    {
        v128_t polynomial = wasm_f32x4_const_splat(k_Exp2Poly4);
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly3));
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly2));
        polynomial = MulAdd(polynomial, fraction, wasm_f32x4_const_splat(k_Exp2Poly1));
        return MulAdd(polynomial, fraction, wasm_f32x4_const_splat(1.0f));
    }

    // 2^n straight into the IEEE-754 exponent field. No multiply, no table
    VX_MATH_FORCEINLINE v128_t Exp2FromIntegerPart(v128_t integer_part) noexcept
    {
        const v128_t biased = wasm_i32x4_add(integer_part, wasm_i32x4_const_splat(127));
        return wasm_i32x4_shl(biased, 23);
    }

    VX_MATH_FORCEINLINE v128_t TruncateToInt(v128_t value) noexcept
    {
#if defined(VX_MATH_RELAXED_SIMD)
        // out-of-range input is outside the documented domain, so saturation is not worth paying for
        return wasm_i32x4_relaxed_trunc_f32x4(value);
#else
        return wasm_i32x4_trunc_sat_f32x4(value);
#endif
    }

    // 2^x = 2^n * 2^f. Nearest, not floor: f lands in [-0.5, 0.5] rather than [0, 1), which is the
    // interval MathPolynomials.hpp's error bounds assume. Flooring costs two orders of magnitude
    template<bool Estimate>
    VX_MATH_FORCEINLINE v128_t Exp2Impl(v128_t value) noexcept
    {
        // Clamped before rounding: an unclamped biased exponent carries into the sign bit past ~127
        // and comes back negative instead of saturating. This saturates to the largest and smallest
        // normals rather than infinity and zero, which is benign and one instruction cheaper than the
        // select chain DirectXMath uses.
        const v128_t limited = FastMin(FastMax(value, wasm_f32x4_const_splat(-126.0f)),
                                       wasm_f32x4_const_splat(127.0f));
        const v128_t nearest = wasm_f32x4_nearest(limited);
        const v128_t fraction = wasm_f32x4_sub(limited, nearest);
        const v128_t mantissa = Estimate ? Exp2FractionEst(fraction) : Exp2Fraction(fraction);
        const v128_t scale = Exp2FromIntegerPart(TruncateToInt(nearest));
        return wasm_f32x4_mul(mantissa, scale);
    }

    // log2 of a positive finite value: exponent + atanh series in t = (m - 1) / (m + 1)
    template<bool Estimate>
    inline v128_t Log2Impl(v128_t value) noexcept
    {
        const v128_t bits = value;
        const v128_t exponentBits =
            wasm_u32x4_shr(wasm_v128_and(bits, wasm_i32x4_const_splat(0x7F800000)), 23);
        const v128_t exponent =
            wasm_f32x4_convert_i32x4(wasm_i32x4_sub(exponentBits, wasm_i32x4_const_splat(127)));

        // exponent field forced to biased 127, so this reads back as a value in [1, 2)
        const v128_t mantissaBits = wasm_v128_or(wasm_v128_and(bits, wasm_i32x4_const_splat(0x007FFFFF)),
                                                 wasm_i32x4_const_splat(0x3F800000));

        // folding [sqrt2, 2) down and carrying the factor bounds |t| at ~0.1716 instead of ~0.333,
        // which is why four series terms suffice
        constexpr float k_Sqrt2 = 1.41421356f;
        const VectorMask aboveSqrt2 =
            VectorMask{ wasm_f32x4_gt(mantissaBits, wasm_f32x4_const_splat(k_Sqrt2)) };
        const v128_t mantissa = Select(aboveSqrt2,
                                      Vector{ mantissaBits },
                                      Vector{ wasm_f32x4_mul(mantissaBits, wasm_f32x4_const_splat(0.5f)) })
                                    .Data();
        const v128_t adjustedExponent =
            Select(aboveSqrt2, Vector{ exponent }, Vector{ wasm_f32x4_add(exponent, wasm_f32x4_const_splat(1.0f)) })
                .Data();

        const v128_t one = wasm_f32x4_const_splat(1.0f);
        const v128_t ratio = wasm_f32x4_div(wasm_f32x4_sub(mantissa, one), wasm_f32x4_add(mantissa, one));
        const v128_t ratioSquared = wasm_f32x4_mul(ratio, ratio);

        v128_t series = wasm_f32x4_const_splat(Estimate ? k_Log2Poly5 : k_Log2Poly7);
        if constexpr (!Estimate)
        {
            series = MulAdd(series, ratioSquared, wasm_f32x4_const_splat(k_Log2Poly5));
        }
        series = MulAdd(series, ratioSquared, wasm_f32x4_const_splat(k_Log2Poly3));
        series = MulAdd(series, ratioSquared, wasm_f32x4_const_splat(k_Log2Poly1));

        const v128_t mantissaLog =
            wasm_f32x4_mul(wasm_f32x4_mul(series, ratio), wasm_f32x4_const_splat(k_Log2Scale));
        return wasm_f32x4_add(adjustedExponent, mantissaLog);
    }

    // ModAngles gives [-pi, pi]; the polynomials only hold on [-pi/2, pi/2]. Outer quadrants reflect
    // via sin(pi - x) == sin(x), signed pi so the result stays in range. cos(pi - x) == -cos(x), so
    // cosine picks up a sign flip there and sine does not.
    struct TrigReduction
    {
        v128_t angle;    // in [-pi/2, pi/2]
        v128_t cosSign;  // +-1 per lane, applied to cosine only
    };

    VX_MATH_FORCEINLINE TrigReduction ReduceForTrig(v128_t radians) noexcept
    {
        constexpr float k_Pi = std::numbers::pi_v<float>;
        constexpr float k_HalfPi = k_Pi * 0.5f;

        const v128_t wrapped = Vector{ radians }.ModAngles().Data();
        const v128_t signBit =
            wasm_v128_and(wrapped, wasm_i32x4_const_splat(static_cast<int32_t>(0x80000000)));
        // +pi where positive, -pi where negative
        const v128_t signedPi = wasm_v128_or(wasm_f32x4_const_splat(k_Pi), signBit);
        const v128_t reflected = wasm_f32x4_sub(signedPi, wrapped);

        const VectorMask inner =
            VectorMask{ wasm_f32x4_le(wasm_f32x4_abs(wrapped), wasm_f32x4_const_splat(k_HalfPi)) };

        TrigReduction reduction{};
        reduction.angle = Select(inner, Vector{ reflected }, Vector{ wrapped }).Data();
        reduction.cosSign = Select(inner,
                                   Vector{ wasm_f32x4_const_splat(-1.0f) },
                                   Vector{ wasm_f32x4_const_splat(1.0f) })
                                .Data();
        return reduction;
    }

    // odd by construction: x * (1 + x^2 * ...)
    template<bool Estimate>
    VX_MATH_FORCEINLINE v128_t SinPolynomial(v128_t angle) noexcept
    {
        const v128_t angleSquared = wasm_f32x4_mul(angle, angle);

        v128_t polynomial;
        if constexpr (Estimate)
        {
            polynomial = wasm_f32x4_const_splat(k_SinEstPoly7);
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_SinEstPoly5));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_SinEstPoly3));
        }
        else
        {
            polynomial = wasm_f32x4_const_splat(k_SinPoly11);
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_SinPoly9));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_SinPoly7));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_SinPoly5));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_SinPoly3));
        }
        polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(1.0f));
        return wasm_f32x4_mul(polynomial, angle);
    }

    // even by construction: 1 + x^2 * ...
    template<bool Estimate>
    VX_MATH_FORCEINLINE v128_t CosPolynomial(v128_t angle) noexcept
    {
        const v128_t angleSquared = wasm_f32x4_mul(angle, angle);

        v128_t polynomial;
        if constexpr (Estimate)
        {
            polynomial = wasm_f32x4_const_splat(k_CosEstPoly6);
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_CosEstPoly4));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_CosEstPoly2));
        }
        else
        {
            polynomial = wasm_f32x4_const_splat(k_CosPoly10);
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_CosPoly8));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_CosPoly6));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_CosPoly4));
            polynomial = MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(k_CosPoly2));
        }
        return MulAdd(polynomial, angleSquared, wasm_f32x4_const_splat(1.0f));
    }
} // namespace detail

VX_MATH_FORCEINLINE Vector Vector::Exp2() const noexcept
{
    return Vector{ detail::Exp2Impl<false>(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Exp2Est() const noexcept
{
    return Vector{ detail::Exp2Impl<true>(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Log2() const noexcept
{
    return Vector{ detail::Log2Impl<false>(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Log2Est() const noexcept
{
    return Vector{ detail::Log2Impl<true>(data) };
}

VX_MATH_FORCEINLINE SinCosResult Vector::SinCos() const noexcept
{
    const detail::TrigReduction reduction = detail::ReduceForTrig(data);
    return SinCosResult{ Vector{ detail::SinPolynomial<false>(reduction.angle) },
                         Vector{ wasm_f32x4_mul(detail::CosPolynomial<false>(reduction.angle),
                                                reduction.cosSign) } };
}

VX_MATH_FORCEINLINE SinCosResult Vector::SinCosEst() const noexcept
{
    const detail::TrigReduction reduction = detail::ReduceForTrig(data);
    return SinCosResult{ Vector{ detail::SinPolynomial<true>(reduction.angle) },
                         Vector{ wasm_f32x4_mul(detail::CosPolynomial<true>(reduction.angle),
                                                reduction.cosSign) } };
}

VX_MATH_FORCEINLINE Vector Vector::Sin() const noexcept
{
    return Vector{ detail::SinPolynomial<false>(detail::ReduceForTrig(data).angle) };
}

VX_MATH_FORCEINLINE Vector Vector::Cos() const noexcept
{
    const detail::TrigReduction reduction = detail::ReduceForTrig(data);
    return Vector{ wasm_f32x4_mul(detail::CosPolynomial<false>(reduction.angle), reduction.cosSign) };
}

VX_MATH_FORCEINLINE Vector Vector::SinEst() const noexcept
{
    return Vector{ detail::SinPolynomial<true>(detail::ReduceForTrig(data).angle) };
}

VX_MATH_FORCEINLINE Vector Vector::CosEst() const noexcept
{
    const detail::TrigReduction reduction = detail::ReduceForTrig(data);
    return Vector{ wasm_f32x4_mul(detail::CosPolynomial<true>(reduction.angle), reduction.cosSign) };
}

// Pow, the base conversions and the hyperbolics all reduce to Exp2/Log2 plus one multiply.
VX_MATH_FORCEINLINE Vector Vector::Pow(Vector exponent) const noexcept
{
    return Vector{ wasm_f32x4_mul(exponent.data, Log2().Data()) }.Exp2();
}

VX_MATH_FORCEINLINE Vector Vector::Pow(float exponent) const noexcept
{
    return Pow(Vector{ wasm_f32x4_splat(exponent) });
}

VX_MATH_FORCEINLINE Vector Vector::Pow(Vector base, float exponent) noexcept
{
    return base.Pow(exponent);
}

VX_MATH_FORCEINLINE Vector Vector::Pow(Vector base, Vector exponent) noexcept
{
    return base.Pow(exponent);
}

VX_MATH_FORCEINLINE Vector Vector::Exp(Vector value) noexcept
{
    return Vector{ wasm_f32x4_mul(value.data, wasm_f32x4_const_splat(detail::k_Log2OfE)) }.Exp2();
}

VX_MATH_FORCEINLINE Vector Vector::Exp10(Vector value) noexcept
{
    return Vector{ wasm_f32x4_mul(value.data, wasm_f32x4_const_splat(detail::k_Log2Of10)) }.Exp2();
}

VX_MATH_FORCEINLINE Vector Vector::Log(Vector value) noexcept
{
    return Vector{ wasm_f32x4_mul(value.Log2().Data(), wasm_f32x4_const_splat(detail::k_LnOf2)) };
}

VX_MATH_FORCEINLINE Vector Vector::Log10(Vector value) noexcept
{
    return Vector{ wasm_f32x4_mul(value.Log2().Data(), wasm_f32x4_const_splat(detail::k_Log10Of2)) };
}

VX_MATH_FORCEINLINE Vector Vector::Tan() const noexcept
{
    const SinCosResult pair = SinCos();
    return Vector{ wasm_f32x4_div(pair.sin.Data(), pair.cos.Data()) };
}

VX_MATH_FORCEINLINE Vector Vector::TanEst() const noexcept
{
    const SinCosResult pair = SinCosEst();
    return Vector{ wasm_f32x4_div(pair.sin.Data(), pair.cos.Data()) };
}

namespace detail
{
    // atan on [-1, 1]; larger magnitudes go through the reciprocal identity in ATanImpl
    VX_MATH_FORCEINLINE v128_t ATanPolynomial(v128_t value) noexcept
    {
        const v128_t squared = wasm_f32x4_mul(value, value);
        v128_t polynomial = wasm_f32x4_const_splat(k_ATanPoly17);
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly15));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly13));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly11));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly9));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly7));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly5));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(k_ATanPoly3));
        polynomial = MulAdd(polynomial, squared, wasm_f32x4_const_splat(1.0f));
        return wasm_f32x4_mul(polynomial, value);
    }

    // atan(x) = sign(x) * pi/2 - atan(1/x) for |x| > 1. Both branches are evaluated and selected, so
    // the reciprocal is taken in lanes that will discard it - substituting 1 there keeps a lane the
    // other branch owns from dividing by zero.
    inline v128_t ATanImpl(v128_t value) noexcept
    {
        constexpr float k_HalfPi = std::numbers::pi_v<float> * 0.5f;

        const v128_t one = wasm_f32x4_const_splat(1.0f);
        const VectorMask inside = VectorMask{ wasm_f32x4_le(wasm_f32x4_abs(value), one) };
        const v128_t safe = Select(inside, Vector{ value }, Vector{ one }).Data();
        const v128_t reciprocal = wasm_f32x4_div(one, safe);

        const v128_t inner = ATanPolynomial(value);
        const v128_t outer = ATanPolynomial(reciprocal);

        const v128_t signBit =
            wasm_v128_and(value, wasm_i32x4_const_splat(static_cast<int32_t>(0x80000000)));
        const v128_t signedHalfPi = wasm_v128_or(wasm_f32x4_const_splat(k_HalfPi), signBit);
        const v128_t reduced = wasm_f32x4_sub(signedHalfPi, outer);

        return Select(inside, Vector{ reduced }, Vector{ inner }).Data();
    }
} // namespace detail

VX_MATH_FORCEINLINE Vector Vector::ATan() const noexcept
{
    return Vector{ detail::ATanImpl(data) };
}

// Quadrant fix-up over atan(y/x): shift by pi with the sign of y where x is negative, keeping the
// result in [-pi, pi]. x == 0 needs no special case - the division gives +-infinity, whose atan is
// already +-pi/2. 0/0 does, since NaN is not a quadrant.
VX_MATH_FORCEINLINE Vector Vector::ATan2(Vector y, Vector x) noexcept
{
    constexpr float k_Pi = std::numbers::pi_v<float>;

    const v128_t base = detail::ATanImpl(wasm_f32x4_div(y.data, x.data));
    const v128_t signOfY =
        wasm_v128_and(y.data, wasm_i32x4_const_splat(static_cast<int32_t>(0x80000000)));
    const v128_t signedPi = wasm_v128_or(wasm_f32x4_const_splat(k_Pi), signOfY);

    const VectorMask negativeX = VectorMask{ wasm_f32x4_lt(x.data, wasm_f32x4_const_splat(0.0f)) };
    const v128_t shifted = wasm_f32x4_add(base, signedPi);
    const Vector quadrant = Select(negativeX, Vector{ base }, Vector{ shifted });

    const VectorMask bothZero = y.CompareEqual(Vector::Zero()) & x.CompareEqual(Vector::Zero());
    return Select(bothZero, quadrant, Vector::Zero());
}

// Through ATan2 rather than their own coefficient tables: the identities are exact, and ATan2 already
// handles the near-vertical case at |x| -> 1 that a direct fit struggles with.
VX_MATH_FORCEINLINE Vector Vector::ASin() const noexcept
{
    const v128_t cosine = wasm_f32x4_sqrt(detail::NegMulAdd(data, data, wasm_f32x4_const_splat(1.0f)));
    return ATan2(Vector{ data }, Vector{ cosine });
}

VX_MATH_FORCEINLINE Vector Vector::ACos() const noexcept
{
    const v128_t sine = wasm_f32x4_sqrt(detail::NegMulAdd(data, data, wasm_f32x4_const_splat(1.0f)));
    return ATan2(Vector{ sine }, Vector{ data });
}

// 1 - 2 / (e^2x + 1), not (e^2x - 1) / (e^2x + 1): the exponential overflows to infinity for large x,
// and this form saturates to 1 there where the difference form gives inf/inf == NaN.
VX_MATH_FORCEINLINE Vector Vector::TanH() const noexcept
{
    const v128_t one = wasm_f32x4_const_splat(1.0f);
    const v128_t two = wasm_f32x4_const_splat(2.0f);
    const v128_t exponential = Vector::Exp(Vector{ wasm_f32x4_mul(data, two) }).Data();
    return Vector{ wasm_f32x4_sub(one, wasm_f32x4_div(two, wasm_f32x4_add(exponential, one))) };
}

VX_MATH_FORCEINLINE Vector Vector::SinH() const noexcept
{
    const v128_t positive = Vector::Exp(Vector{ data }).Data();
    const v128_t negative = Vector::Exp(Vector{ wasm_f32x4_neg(data) }).Data();
    return Vector{ wasm_f32x4_mul(wasm_f32x4_sub(positive, negative), wasm_f32x4_const_splat(0.5f)) };
}

VX_MATH_FORCEINLINE Vector Vector::CosH() const noexcept
{
    const v128_t positive = Vector::Exp(Vector{ data }).Data();
    const v128_t negative = Vector::Exp(Vector{ wasm_f32x4_neg(data) }).Data();
    return Vector{ wasm_f32x4_mul(wasm_f32x4_add(positive, negative), wasm_f32x4_const_splat(0.5f)) };
}

// wasm has single-instruction div and sqrt, so these two are exact and there is nothing to estimate
VX_MATH_FORCEINLINE Vector Vector::ReciprocalEst() const noexcept
{
    return Reciprocal();
}

VX_MATH_FORCEINLINE Vector Vector::SqrtEst() const noexcept
{
    return Sqrt();
}

// Halving the exponent field via the seed constant gives ~3% relative error; one Newton-Raphson step,
// y * (1.5 - 0.5 * x * y^2), brings it to ~2e-3. Worth it only because wasm has no rsqrt instruction
// and both div and sqrt are slow here.
VX_MATH_FORCEINLINE Vector Vector::ReciprocalSqrtEst() const noexcept
{
    const v128_t seedConstant =
        wasm_i32x4_const_splat(static_cast<int32_t>(detail::k_ReciprocalSqrtSeed));
    const v128_t seed = wasm_i32x4_sub(seedConstant, wasm_i32x4_shr(data, 1));
    const v128_t half = wasm_f32x4_mul(data, wasm_f32x4_const_splat(0.5f));
    const v128_t squared = wasm_f32x4_mul(seed, seed);
    const v128_t correction = detail::NegMulAdd(half, squared, wasm_f32x4_const_splat(1.5f));
    return Vector{ wasm_f32x4_mul(seed, correction) };
}

template<int N>
VX_MATH_FORCEINLINE Vector Vector::NormalizeEst() const noexcept
{
    static_assert(N >= 2 && N <= 4, "NormalizeEst dimensionality must be 2, 3, or 4");
    return Vector{ wasm_f32x4_mul(data, DotVec<N>(*this).ReciprocalSqrtEst().Data()) };
}

template<int N>
VX_MATH_FORCEINLINE float Vector::LengthEst() const noexcept
{
    static_assert(N >= 2 && N <= 4, "LengthEst dimensionality must be 2, 3, or 4");
    return wasm_f32x4_extract_lane(DotVec<N>(*this).SqrtEst().Data(), 0);
}

template<int N>
VX_MATH_FORCEINLINE float Vector::ReciprocalLengthEst() const noexcept
{
    static_assert(N >= 2 && N <= 4, "ReciprocalLengthEst dimensionality must be 2, 3, or 4");
    return wasm_f32x4_extract_lane(DotVec<N>(*this).ReciprocalSqrtEst().Data(), 0);
}

} // namespace velox::math
