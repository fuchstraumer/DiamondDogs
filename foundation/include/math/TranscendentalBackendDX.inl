#pragma once
#include "math/Math.hpp"
// DirectXMath transcendentals for math::Vector. Included from MathBackendDX.inl; include nothing.
//
// Forwarding: DirectXMath already handles the edge cases the WASM side spells out by hand. Results do
// not match that backend bit for bit; only the documented accuracy bounds are shared.
namespace math
{

DD_MATH_FORCEINLINE Vector Vector::Exp2() const noexcept
{
    return Vector{ DirectX::XMVectorExp2(data) };
}

// No XMVectorExp2Est exists. The estimate is for the WASM target's benefit; a speed divergence
// between backends does not matter the way a semantic one would
DD_MATH_FORCEINLINE Vector Vector::Exp2Est() const noexcept
{
    return Vector{ DirectX::XMVectorExp2(data) };
}

DD_MATH_FORCEINLINE Vector Vector::Log2() const noexcept
{
    return Vector{ DirectX::XMVectorLog2(data) };
}

DD_MATH_FORCEINLINE Vector Vector::Log2Est() const noexcept
{
    return Vector{ DirectX::XMVectorLog2(data) };
}

DD_MATH_FORCEINLINE SinCosResult Vector::SinCos() const noexcept
{
    DirectX::XMVECTOR sine;
    DirectX::XMVECTOR cosine;
    DirectX::XMVectorSinCos(&sine, &cosine, data);
    return SinCosResult{ Vector{ sine }, Vector{ cosine } };
}

DD_MATH_FORCEINLINE SinCosResult Vector::SinCosEst() const noexcept
{
    DirectX::XMVECTOR sine;
    DirectX::XMVECTOR cosine;
    DirectX::XMVectorSinCosEst(&sine, &cosine, data);
    return SinCosResult{ Vector{ sine }, Vector{ cosine } };
}

DD_MATH_FORCEINLINE Vector Vector::Sin() const noexcept
{
    return Vector{ DirectX::XMVectorSin(data) };
}

DD_MATH_FORCEINLINE Vector Vector::Cos() const noexcept
{
    return Vector{ DirectX::XMVectorCos(data) };
}

DD_MATH_FORCEINLINE Vector Vector::SinEst() const noexcept
{
    return Vector{ DirectX::XMVectorSinEst(data) };
}

DD_MATH_FORCEINLINE Vector Vector::CosEst() const noexcept
{
    return Vector{ DirectX::XMVectorCosEst(data) };
}

DD_MATH_FORCEINLINE Vector Vector::Tan() const noexcept
{
    return Vector{ DirectX::XMVectorTan(data) };
}

DD_MATH_FORCEINLINE Vector Vector::TanEst() const noexcept
{
    return Vector{ DirectX::XMVectorTanEst(data) };
}

DD_MATH_FORCEINLINE Vector Vector::ASin() const noexcept
{
    return Vector{ DirectX::XMVectorASin(data) };
}

DD_MATH_FORCEINLINE Vector Vector::ACos() const noexcept
{
    return Vector{ DirectX::XMVectorACos(data) };
}

DD_MATH_FORCEINLINE Vector Vector::ATan() const noexcept
{
    return Vector{ DirectX::XMVectorATan(data) };
}

DD_MATH_FORCEINLINE Vector Vector::ATan2(Vector y, Vector x) noexcept
{
    return Vector{ DirectX::XMVectorATan2(y.data, x.data) };
}

DD_MATH_FORCEINLINE Vector Vector::TanH() const noexcept
{
    return Vector{ DirectX::XMVectorTanH(data) };
}

DD_MATH_FORCEINLINE Vector Vector::SinH() const noexcept
{
    return Vector{ DirectX::XMVectorSinH(data) };
}

DD_MATH_FORCEINLINE Vector Vector::CosH() const noexcept
{
    return Vector{ DirectX::XMVectorCosH(data) };
}

DD_MATH_FORCEINLINE Vector Vector::Exp(Vector value) noexcept
{
    return Vector{ DirectX::XMVectorExpE(value.data) };
}

DD_MATH_FORCEINLINE Vector Vector::Exp10(Vector value) noexcept
{
    return Vector{ DirectX::XMVectorExp10(value.data) };
}

DD_MATH_FORCEINLINE Vector Vector::Log(Vector value) noexcept
{
    return Vector{ DirectX::XMVectorLogE(value.data) };
}

DD_MATH_FORCEINLINE Vector Vector::Log10(Vector value) noexcept
{
    return Vector{ DirectX::XMVectorLog10(value.data) };
}

// rcpps and rsqrtps, ~12 bits each - genuinely approximate here, unlike on wasm where div and sqrt
// are single instructions and the Est forms are exact
DD_MATH_FORCEINLINE Vector Vector::ReciprocalEst() const noexcept
{
    return Vector{ DirectX::XMVectorReciprocalEst(data) };
}

DD_MATH_FORCEINLINE Vector Vector::ReciprocalSqrtEst() const noexcept
{
    return Vector{ DirectX::XMVectorReciprocalSqrtEst(data) };
}

DD_MATH_FORCEINLINE Vector Vector::SqrtEst() const noexcept
{
    return Vector{ DirectX::XMVectorSqrtEst(data) };
}

template<int N>
DD_MATH_FORCEINLINE Vector Vector::NormalizeEst() const noexcept
{
    static_assert(N >= 2 && N <= 4, "NormalizeEst dimensionality must be 2, 3, or 4");
    return Vector{ DirectX::XMVectorMultiply(data, DotVec<N>(*this).ReciprocalSqrtEst().Data()) };
}

template<int N>
DD_MATH_FORCEINLINE float Vector::LengthEst() const noexcept
{
    static_assert(N >= 2 && N <= 4, "LengthEst dimensionality must be 2, 3, or 4");
    return DirectX::XMVectorGetX(DotVec<N>(*this).SqrtEst().Data());
}

template<int N>
DD_MATH_FORCEINLINE float Vector::ReciprocalLengthEst() const noexcept
{
    static_assert(N >= 2 && N <= 4, "ReciprocalLengthEst dimensionality must be 2, 3, or 4");
    return DirectX::XMVectorGetX(DotVec<N>(*this).ReciprocalSqrtEst().Data());
}

} // namespace velox::math
