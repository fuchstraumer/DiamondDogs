#pragma once
// DirectXMath backend for math::Vector / math::Matrix. Included by Math.inl
// when VX_MATH_BACKEND_WASM is 0 (native builds, or a forced override).
// Requires <DirectXMath.h>, already included by Math.hpp.

namespace velox::math
{
// ================================
// VectorMask Implementation (DirectXMath)
// ================================

VX_MATH_FORCEINLINE VectorMask::VectorMask() noexcept
    : data{ DirectX::XMVectorZero() }
{
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator&(VectorMask rhs) const noexcept
{
    return VectorMask{ DirectX::XMVectorAndInt(data, rhs.data) };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator|(VectorMask rhs) const noexcept
{
    return VectorMask{ DirectX::XMVectorOrInt(data, rhs.data) };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::operator^(VectorMask rhs) const noexcept
{
    return VectorMask{ DirectX::XMVectorXorInt(data, rhs.data) };
}

// no XMVectorNotInt exists; NOR against itself is ~(v | v) == ~v
VX_MATH_FORCEINLINE VectorMask VectorMask::operator~() const noexcept
{
    return VectorMask{ DirectX::XMVectorNorInt(data, data) };
}

VX_MATH_FORCEINLINE VectorMask& VectorMask::operator&=(VectorMask rhs) noexcept
{
    data = DirectX::XMVectorAndInt(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE VectorMask& VectorMask::operator|=(VectorMask rhs) noexcept
{
    data = DirectX::XMVectorOrInt(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE VectorMask& VectorMask::operator^=(VectorMask rhs) noexcept
{
    data = DirectX::XMVectorXorInt(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE uint32_t VectorMask::LaneBits() const noexcept
{
    return static_cast<uint32_t>(_mm_movemask_ps(data));
}

namespace detail
{
    template<int N>
    constexpr uint32_t LaneBitsFor() noexcept
    {
        static_assert(N >= 2 && N <= 4, "Mask width must be 2, 3, or 4");
        return (1u << N) - 1u;
    }
} // namespace detail

template<int N>
VX_MATH_FORCEINLINE bool VectorMask::AllTrue() const noexcept
{
    return (LaneBits() & detail::LaneBitsFor<N>()) == detail::LaneBitsFor<N>();
}

template<int N>
VX_MATH_FORCEINLINE bool VectorMask::AnyTrue() const noexcept
{
    return (LaneBits() & detail::LaneBitsFor<N>()) != 0u;
}

VX_MATH_FORCEINLINE VectorMask VectorMask::AllSet() noexcept
{
    return VectorMask{ DirectX::XMVectorTrueInt() };
}

VX_MATH_FORCEINLINE VectorMask VectorMask::AllClear() noexcept
{
    return VectorMask{ DirectX::XMVectorFalseInt() };
}

// ================================
// Vector SIMD Implementation (DirectXMath)
// ================================

VX_MATH_FORCEINLINE Vector::Vector() noexcept
    : data{ DirectX::XMVectorZero() }
{
}
VX_MATH_FORCEINLINE Vector::Vector(float x, float y, float z, float w) noexcept
    : data{ DirectX::XMVectorSet(x, y, z, w) }
{
}
VX_MATH_FORCEINLINE Vector::Vector(float x, float y, float z) noexcept
    : data{ DirectX::XMVectorSet(x, y, z, 0.0f) }
{
}
VX_MATH_FORCEINLINE Vector::Vector(float x, float y) noexcept
    : data{ DirectX::XMVectorSet(x, y, 0.0f, 0.0f) }
{
}
VX_MATH_FORCEINLINE Vector::Vector(float scalar) noexcept
    : data{ DirectX::XMVectorReplicate(scalar) }
{
}

// Component accessors
VX_MATH_FORCEINLINE float Vector::x() const noexcept
{
    return DirectX::XMVectorGetX(data);
}
VX_MATH_FORCEINLINE float Vector::y() const noexcept
{
    return DirectX::XMVectorGetY(data);
}
VX_MATH_FORCEINLINE float Vector::z() const noexcept
{
    return DirectX::XMVectorGetZ(data);
}
VX_MATH_FORCEINLINE float Vector::w() const noexcept
{
    return DirectX::XMVectorGetW(data);
}

// Static factory methods
VX_MATH_FORCEINLINE Vector Vector::Zero() noexcept
{
    return Vector{ DirectX::XMVectorZero() };
}
VX_MATH_FORCEINLINE Vector Vector::Replicate(float scalar) noexcept
{
    return Vector{ DirectX::XMVectorReplicate(scalar) };
}
VX_MATH_FORCEINLINE Vector Vector::Identity() noexcept
{
    return Vector{ DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f) };
}

// Arithmetic operators
VX_MATH_FORCEINLINE Vector Vector::operator+(Vector rhs) const noexcept
{
    return Vector{ DirectX::XMVectorAdd(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator-(Vector rhs) const noexcept
{
    return Vector{ DirectX::XMVectorSubtract(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator*(Vector rhs) const noexcept
{
    return Vector{ DirectX::XMVectorMultiply(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator/(Vector rhs) const noexcept
{
    return Vector{ DirectX::XMVectorDivide(data, rhs.data) };
}
VX_MATH_FORCEINLINE Vector Vector::operator*(float scalar) const noexcept
{
    return Vector{ DirectX::XMVectorScale(data, scalar) };
}
VX_MATH_FORCEINLINE Vector Vector::operator/(float scalar) const noexcept
{
    return Vector{ DirectX::XMVectorDivide(data, DirectX::XMVectorReplicate(scalar)) };
}
VX_MATH_FORCEINLINE Vector Vector::operator-() const noexcept
{
    return Vector{ DirectX::XMVectorNegate(data) };
}

// VX_MATH_RELAXED_SIMD does nothing here: XMVectorMultiplyAdd already lowers to a hardware FMA
VX_MATH_FORCEINLINE Vector Vector::MultiplyAdd(Vector factor, Vector addend) const noexcept
{
    return Vector{ DirectX::XMVectorMultiplyAdd(data, factor.data, addend.data) };
}

// Compound assignment operators
VX_MATH_FORCEINLINE Vector& Vector::operator+=(Vector rhs) noexcept
{
    data = DirectX::XMVectorAdd(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator-=(Vector rhs) noexcept
{
    data = DirectX::XMVectorSubtract(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator*=(Vector rhs) noexcept
{
    data = DirectX::XMVectorMultiply(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator/=(Vector rhs) noexcept
{
    data = DirectX::XMVectorDivide(data, rhs.data);
    return *this;
}
VX_MATH_FORCEINLINE Vector& Vector::operator*=(float scalar) noexcept
{
    data = DirectX::XMVectorScale(data, scalar);
    return *this;
}
// True divide, not a reciprocal multiply: wasm has no scale-by-reciprocal, and matching keeps the
// two backends agreeing to the last bit.
VX_MATH_FORCEINLINE Vector& Vector::operator/=(float scalar) noexcept
{
    data = DirectX::XMVectorDivide(data, DirectX::XMVectorReplicate(scalar));
    return *this;
}

VX_MATH_FORCEINLINE Vector Vector::Reciprocal() const noexcept
{
    return Vector{ DirectX::XMVectorReciprocal(data) };
}
VX_MATH_FORCEINLINE Vector Vector::Sqrt() const noexcept
{
    return Vector{ DirectX::XMVectorSqrt(data) };
}
VX_MATH_FORCEINLINE Vector Vector::ReciprocalSqrt() const noexcept
{
    return Vector{ DirectX::XMVectorReciprocalSqrt(data) };
}

// Raw divide, not XMVector{2,3,4}Normalize: those map zero length to zero and infinite to QNaN,
// which wasm does not. A consistent infinity beats a silent per-backend difference. Check the length
// yourself if it matters.
template<int N>
VX_MATH_FORCEINLINE Vector Vector::Normalize() const noexcept
{
    static_assert(N >= 2 && N <= 4, "Normalize dimensionality must be 2, 3, or 4");
    return Vector{ DirectX::XMVectorDivide(data, DirectX::XMVectorSqrt(DotVec<N>(*this).Data())) };
}

template<int N>
VX_MATH_FORCEINLINE float Vector::Length() const noexcept
{
    static_assert(N >= 2 && N <= 4, "Length dimensionality must be 2, 3, or 4");
    if constexpr (N == 2)
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2Length(data));
    }
    else if constexpr (N == 3)
    {
        return DirectX::XMVectorGetX(DirectX::XMVector3Length(data));
    }
    else
    {
        return DirectX::XMVectorGetX(DirectX::XMVector4Length(data));
    }
}

template<int N>
VX_MATH_FORCEINLINE float Vector::LengthSq() const noexcept
{
    static_assert(N >= 2 && N <= 4, "LengthSq dimensionality must be 2, 3, or 4");
    if constexpr (N == 2)
    {
        return DirectX::XMVectorGetX(DirectX::XMVector2LengthSq(data));
    }
    else if constexpr (N == 3)
    {
        return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(data));
    }
    else
    {
        return DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(data));
    }
}

// Cross product only makes sense for 3D vectors
VX_MATH_FORCEINLINE Vector Vector::Cross(Vector other) const noexcept
{
    return Vector{ DirectX::XMVector3Cross(data, other.data) };
}

// XMVector{2,3,4}Dot already replicate across every lane, so DotVec is the primitive
template<int N>
VX_MATH_FORCEINLINE Vector Vector::DotVec(Vector other) const noexcept
{
    static_assert(N >= 2 && N <= 4, "DotVec dimensionality must be 2, 3, or 4");
    if constexpr (N == 2)
    {
        return Vector{ DirectX::XMVector2Dot(data, other.data) };
    }
    else if constexpr (N == 3)
    {
        return Vector{ DirectX::XMVector3Dot(data, other.data) };
    }
    else
    {
        return Vector{ DirectX::XMVector4Dot(data, other.data) };
    }
}

template<int N>
VX_MATH_FORCEINLINE float Vector::Dot(Vector other) const noexcept
{
    static_assert(N >= 2 && N <= 4, "Dot dimensionality must be 2, 3, or 4");
    return DirectX::XMVectorGetX(DotVec<N>(other).Data());
}

VX_MATH_FORCEINLINE Vector Vector::Lerp(Vector target, float t) const noexcept
{
    return Vector{ DirectX::XMVectorLerp(data, target.data, t) };
}

template<int N>
VX_MATH_FORCEINLINE Vector Vector::Reflect(Vector normal) const noexcept
{
    static_assert(N >= 2 && N <= 4, "Reflect dimensionality must be 2, 3, or 4");
    if constexpr (N == 2)
    {
        return Vector{ DirectX::XMVector2Reflect(data, normal.data) };
    }
    else if constexpr (N == 3)
    {
        return Vector{ DirectX::XMVector3Reflect(data, normal.data) };
    }
    else
    {
        return Vector{ DirectX::XMVector4Reflect(data, normal.data) };
    }
}

VX_MATH_FORCEINLINE Vector Vector::Clamp(Vector min, Vector max) const noexcept
{
    return Vector{ DirectX::XMVectorClamp(data, min.data, max.data) };
}
VX_MATH_FORCEINLINE Vector Vector::Saturate() const noexcept
{
    return Vector{ DirectX::XMVectorSaturate(data) };
}
VX_MATH_FORCEINLINE Vector Vector::Abs() const noexcept
{
    return Vector{ DirectX::XMVectorAbs(data) };
}
VX_MATH_FORCEINLINE Vector Vector::Min(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorMin(data, other.data) };
}
VX_MATH_FORCEINLINE Vector Vector::Max(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorMax(data, other.data) };
}
VX_MATH_FORCEINLINE Vector Vector::Pow(float exponent) const noexcept
{
    return Vector{ DirectX::XMVectorPow(data, DirectX::XMVectorReplicate(exponent)) };
}
VX_MATH_FORCEINLINE Vector Vector::Pow(Vector exponent) const noexcept
{
    return Vector{ DirectX::XMVectorPow(data, exponent.data) };
}

VX_MATH_FORCEINLINE Vector Vector::Abs(Vector vec) noexcept
{
    return Vector{ DirectX::XMVectorAbs(vec.data) };
}
VX_MATH_FORCEINLINE Vector Vector::Pow(Vector base, float exponent) noexcept
{
    return Vector{ DirectX::XMVectorPow(base.data, DirectX::XMVectorReplicate(exponent)) };
}
VX_MATH_FORCEINLINE Vector Vector::Pow(Vector base, Vector exponent) noexcept
{
    return Vector{ DirectX::XMVectorPow(base.data, exponent.data) };
}

VX_MATH_FORCEINLINE Vector Vector::Infinity() noexcept
{
    return Vector{ DirectX::XMVectorSplatInfinity() };
}

VX_MATH_FORCEINLINE Vector Vector::QuietNaN() noexcept
{
    return Vector{ DirectX::XMVectorSplatQNaN() };
}

VX_MATH_FORCEINLINE Vector Vector::Epsilon() noexcept
{
    return Vector{ DirectX::XMVectorSplatEpsilon() };
}


VX_MATH_FORCEINLINE VectorMask Vector::CompareEqual(Vector other) const noexcept
{
    return VectorMask{ DirectX::XMVectorEqual(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareNotEqual(Vector other) const noexcept
{
    return VectorMask{ DirectX::XMVectorNotEqual(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareLess(Vector other) const noexcept
{
    return VectorMask{ DirectX::XMVectorLess(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareLessOrEqual(Vector other) const noexcept
{
    return VectorMask{ DirectX::XMVectorLessOrEqual(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareGreater(Vector other) const noexcept
{
    return VectorMask{ DirectX::XMVectorGreater(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareGreaterOrEqual(Vector other) const noexcept
{
    return VectorMask{ DirectX::XMVectorGreaterOrEqual(data, other.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::CompareNearEqual(Vector other, Vector epsilon) const noexcept
{
    return VectorMask{ DirectX::XMVectorNearEqual(data, other.data, epsilon.data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::IsNaN() const noexcept
{
    return VectorMask{ DirectX::XMVectorIsNaN(data) };
}

VX_MATH_FORCEINLINE VectorMask Vector::IsInfinite() const noexcept
{
    return VectorMask{ DirectX::XMVectorIsInfinite(data) };
}


VX_MATH_FORCEINLINE Vector Vector::AndInt(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorAndInt(data, other.data) };
}

// ~other & this; DirectXMath spells it AndCInt
VX_MATH_FORCEINLINE Vector Vector::AndNotInt(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorAndCInt(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::OrInt(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorOrInt(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::XorInt(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorXorInt(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::NorInt(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorNorInt(data, other.data) };
}


VX_MATH_FORCEINLINE Vector Vector::Round() const noexcept
{
    return Vector{ DirectX::XMVectorRound(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Truncate() const noexcept
{
    return Vector{ DirectX::XMVectorTruncate(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Floor() const noexcept
{
    return Vector{ DirectX::XMVectorFloor(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Ceil() const noexcept
{
    return Vector{ DirectX::XMVectorCeiling(data) };
}

VX_MATH_FORCEINLINE Vector Vector::Mod(Vector divisor) const noexcept
{
    return Vector{ DirectX::XMVectorMod(data, divisor.data) };
}

VX_MATH_FORCEINLINE Vector Vector::ModAngles() const noexcept
{
    return Vector{ DirectX::XMVectorModAngles(data) };
}


VX_MATH_FORCEINLINE Vector Vector::SplatX() const noexcept
{
    return Vector{ DirectX::XMVectorSplatX(data) };
}

VX_MATH_FORCEINLINE Vector Vector::SplatY() const noexcept
{
    return Vector{ DirectX::XMVectorSplatY(data) };
}

VX_MATH_FORCEINLINE Vector Vector::SplatZ() const noexcept
{
    return Vector{ DirectX::XMVectorSplatZ(data) };
}

VX_MATH_FORCEINLINE Vector Vector::SplatW() const noexcept
{
    return Vector{ DirectX::XMVectorSplatW(data) };
}

VX_MATH_FORCEINLINE Vector Vector::MergeXY(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorMergeXY(data, other.data) };
}

VX_MATH_FORCEINLINE Vector Vector::MergeZW(Vector other) const noexcept
{
    return Vector{ DirectX::XMVectorMergeZW(data, other.data) };
}

template<int X, int Y, int Z, int W>
VX_MATH_FORCEINLINE Vector Vector::Swizzle() const noexcept
{
    static_assert(X >= 0 && X <= 3 && Y >= 0 && Y <= 3 && Z >= 0 && Z <= 3 && W >= 0 && W <= 3,
                  "Swizzle lane indices must be 0-3");
    return Vector{ DirectX::XMVectorSwizzle<X, Y, Z, W>(data) };
}

template<int X, int Y, int Z, int W>
VX_MATH_FORCEINLINE Vector Vector::Permute(Vector other) const noexcept
{
    static_assert(X >= 0 && X <= 7 && Y >= 0 && Y <= 7 && Z >= 0 && Z <= 7 && W >= 0 && W <= 7,
                  "Permute lane indices must be 0-7 (0-3 select this vector, 4-7 select the other)");
    return Vector{ DirectX::XMVectorPermute<X, Y, Z, W>(data, other.data) };
}

// ================================
// Free Function Implementations (Vector)
// ================================

// XMVectorSelect takes its first argument where the control bit is clear - note the operand swap
VX_MATH_FORCEINLINE Vector Select(VectorMask mask, Vector when_clear, Vector when_set) noexcept
{
    return Vector{ DirectX::XMVectorSelect(when_clear.Data(), when_set.Data(), mask.Data()) };
}

VX_MATH_FORCEINLINE Vector operator*(float scalar, Vector vec) noexcept
{
    return vec * scalar;
}

VX_MATH_FORCEINLINE Vector ToVector(const Float2& in) noexcept
{
    return Vector{ DirectX::XMVectorSet(in.x, in.y, 0.0f, 0.0f) };
}
VX_MATH_FORCEINLINE Vector ToVector(const Float3& in) noexcept
{
    return Vector{ DirectX::XMVectorSet(in.x, in.y, in.z, 0.0f) };
}
VX_MATH_FORCEINLINE Vector ToVector(const Float4& in) noexcept
{
    return Vector{ DirectX::XMVectorSet(in.x, in.y, in.z, in.w) };
}

namespace detail
{
    VX_MATH_FORCEINLINE FromVectorProxy::operator Float2() const noexcept
    {
        DirectX::XMFLOAT2 result;
        DirectX::XMStoreFloat2(&result, vec.Data());
        return Float2(result.x, result.y);
    }

    VX_MATH_FORCEINLINE FromVectorProxy::operator Float3() const noexcept
    {
        DirectX::XMFLOAT3 result;
        DirectX::XMStoreFloat3(&result, vec.Data());
        return Float3(result.x, result.y, result.z);
    }

    VX_MATH_FORCEINLINE FromVectorProxy::operator Float4() const noexcept
    {
        DirectX::XMFLOAT4 result;
        DirectX::XMStoreFloat4(&result, vec.Data());
        return Float4(result.x, result.y, result.z, result.w);
    }
} // namespace detail

// ================================
// SIMD Matrix Implementation (DirectXMath)
// ================================

VX_MATH_FORCEINLINE Matrix::Matrix() noexcept
    : data(DirectX::XMMatrixIdentity())
{
}

VX_MATH_FORCEINLINE Matrix::Matrix(float m00,
                                   float m01,
                                   float m02,
                                   float m03,
                                   float m10,
                                   float m11,
                                   float m12,
                                   float m13,
                                   float m20,
                                   float m21,
                                   float m22,
                                   float m23,
                                   float m30,
                                   float m31,
                                   float m32,
                                   float m33) noexcept
    : data{ DirectX::XMMatrixSet(
          m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33) }
{
}

VX_MATH_FORCEINLINE Matrix::Matrix(Vector row0, Vector row1, Vector row2, Vector row3) noexcept
    : data{ DirectX::XMMATRIX{ row0.Data(), row1.Data(), row2.Data(), row3.Data() } }
{
}


VX_MATH_FORCEINLINE Vector Matrix::GetRow(size_t index) const noexcept
{
    return Vector{ data.r[index] };
}
VX_MATH_FORCEINLINE void Matrix::SetRow(size_t index, Vector row) noexcept
{
    data.r[index] = row.Data();
}

VX_MATH_FORCEINLINE Vector Matrix::GetColumn(size_t index) const noexcept
{
    return Vector{ DirectX::XMVectorGetByIndex(data.r[0], index),
                   DirectX::XMVectorGetByIndex(data.r[1], index),
                   DirectX::XMVectorGetByIndex(data.r[2], index),
                   DirectX::XMVectorGetByIndex(data.r[3], index) };
}

VX_MATH_FORCEINLINE void Matrix::SetColumn(size_t index, Vector column) noexcept
{
    data.r[0] = DirectX::XMVectorSetByIndex(data.r[0], DirectX::XMVectorGetX(column.Data()), index);
    data.r[1] = DirectX::XMVectorSetByIndex(data.r[1], DirectX::XMVectorGetY(column.Data()), index);
    data.r[2] = DirectX::XMVectorSetByIndex(data.r[2], DirectX::XMVectorGetZ(column.Data()), index);
    data.r[3] = DirectX::XMVectorSetByIndex(data.r[3], DirectX::XMVectorGetW(column.Data()), index);
}

VX_MATH_FORCEINLINE float Matrix::operator[](size_t row, size_t col) const noexcept
{
    const float* matrix_data = reinterpret_cast<const float*>(&data);
    return matrix_data[row * 4 + col];
}

VX_MATH_FORCEINLINE void Matrix::SetElement(size_t row, size_t col, float value) noexcept
{
    float* matrix_data = reinterpret_cast<float*>(&data);
    matrix_data[row * 4 + col] = value;
}

VX_MATH_FORCEINLINE Matrix Matrix::operator+(const Matrix& rhs) const noexcept
{
    return Matrix{ DirectX::XMMATRIX{ DirectX::XMVectorAdd(data.r[0], rhs.data.r[0]),
                                      DirectX::XMVectorAdd(data.r[1], rhs.data.r[1]),
                                      DirectX::XMVectorAdd(data.r[2], rhs.data.r[2]),
                                      DirectX::XMVectorAdd(data.r[3], rhs.data.r[3]) } };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator-(const Matrix& rhs) const noexcept
{
    return Matrix{ DirectX::XMMATRIX{ DirectX::XMVectorSubtract(data.r[0], rhs.data.r[0]),
                                      DirectX::XMVectorSubtract(data.r[1], rhs.data.r[1]),
                                      DirectX::XMVectorSubtract(data.r[2], rhs.data.r[2]),
                                      DirectX::XMVectorSubtract(data.r[3], rhs.data.r[3]) } };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator*(const Matrix& rhs) const noexcept
{
    return Matrix{ DirectX::XMMatrixMultiply(data, rhs.data) };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator*(float scalar) const noexcept
{
    return Matrix{ DirectX::XMMATRIX{ DirectX::XMVectorScale(data.r[0], scalar),
                                      DirectX::XMVectorScale(data.r[1], scalar),
                                      DirectX::XMVectorScale(data.r[2], scalar),
                                      DirectX::XMVectorScale(data.r[3], scalar) } };
}

VX_MATH_FORCEINLINE Matrix Matrix::operator-() const noexcept
{
    return Matrix{ DirectX::XMMATRIX{ DirectX::XMVectorNegate(data.r[0]),
                                      DirectX::XMVectorNegate(data.r[1]),
                                      DirectX::XMVectorNegate(data.r[2]),
                                      DirectX::XMVectorNegate(data.r[3]) } };
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator+=(const Matrix& rhs) noexcept
{
    data.r[0] = DirectX::XMVectorAdd(data.r[0], rhs.data.r[0]);
    data.r[1] = DirectX::XMVectorAdd(data.r[1], rhs.data.r[1]);
    data.r[2] = DirectX::XMVectorAdd(data.r[2], rhs.data.r[2]);
    data.r[3] = DirectX::XMVectorAdd(data.r[3], rhs.data.r[3]);
    return *this;
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator-=(const Matrix& rhs) noexcept
{
    data.r[0] = DirectX::XMVectorSubtract(data.r[0], rhs.data.r[0]);
    data.r[1] = DirectX::XMVectorSubtract(data.r[1], rhs.data.r[1]);
    data.r[2] = DirectX::XMVectorSubtract(data.r[2], rhs.data.r[2]);
    data.r[3] = DirectX::XMVectorSubtract(data.r[3], rhs.data.r[3]);
    return *this;
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator*=(const Matrix& rhs) noexcept
{
    data = DirectX::XMMatrixMultiply(data, rhs.data);
    return *this;
}

VX_MATH_FORCEINLINE Matrix& Matrix::operator*=(float scalar) noexcept
{
    data.r[0] = DirectX::XMVectorScale(data.r[0], scalar);
    data.r[1] = DirectX::XMVectorScale(data.r[1], scalar);
    data.r[2] = DirectX::XMVectorScale(data.r[2], scalar);
    data.r[3] = DirectX::XMVectorScale(data.r[3], scalar);
    return *this;
}

VX_MATH_FORCEINLINE Vector Matrix::operator*(Vector vec) const noexcept
{
    return Vector{ DirectX::XMVector4Transform(vec.Data(), data) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Transpose() const noexcept
{
    return Matrix{ DirectX::XMMatrixTranspose(data) };
}

// Same cofactor expansion the WASM backend ports by hand; neither special-cases a singular matrix
VX_MATH_FORCEINLINE Matrix Matrix::Inverse() const noexcept
{
    return Matrix{ DirectX::XMMatrixInverse(nullptr, data) };
}

VX_MATH_FORCEINLINE float Matrix::Determinant() const noexcept
{
    return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(data));
}

// Static factory methods for transformations
VX_MATH_FORCEINLINE Matrix Matrix::Translation(Vector translation) noexcept
{
    return Matrix{ DirectX::XMMatrixTranslationFromVector(translation.Data()) };
}
VX_MATH_FORCEINLINE Matrix Matrix::Translation(float x, float y, float z) noexcept
{
    return Matrix{ DirectX::XMMatrixTranslation(x, y, z) };
}
VX_MATH_FORCEINLINE Matrix Matrix::Scale(Vector scale) noexcept
{
    return Matrix{ DirectX::XMMatrixScalingFromVector(scale.Data()) };
}
VX_MATH_FORCEINLINE Matrix Matrix::Scale(float x, float y, float z) noexcept
{
    return Matrix{ DirectX::XMMatrixScaling(x, y, z) };
}
VX_MATH_FORCEINLINE Matrix Matrix::Scale(float uniform_scale) noexcept
{
    return Matrix{ DirectX::XMMatrixScaling(uniform_scale, uniform_scale, uniform_scale) };
}

VX_MATH_FORCEINLINE Matrix Matrix::RotationX(float radians) noexcept
{
    return Matrix{ DirectX::XMMatrixRotationX(radians) };
}
VX_MATH_FORCEINLINE Matrix Matrix::RotationY(float radians) noexcept
{
    return Matrix{ DirectX::XMMatrixRotationY(radians) };
}
VX_MATH_FORCEINLINE Matrix Matrix::RotationZ(float radians) noexcept
{
    return Matrix{ DirectX::XMMatrixRotationZ(radians) };
}
VX_MATH_FORCEINLINE Matrix Matrix::RotationAxis(Vector axis, float radians) noexcept
{
    return Matrix{ DirectX::XMMatrixRotationAxis(axis.Data(), radians) };
}
VX_MATH_FORCEINLINE Matrix Matrix::RotationQuaternion(Quaternion rotation) noexcept
{
    return Matrix{ DirectX::XMMatrixRotationQuaternion(rotation.Data()) };
}

// Forwards to the quaternion form so the Euler composition order is defined in exactly one place
VX_MATH_FORCEINLINE Matrix Matrix::RotationRollPitchYaw(float pitch, float yaw, float roll) noexcept
{
    return Matrix::RotationQuaternion(Quaternion::RotationRollPitchYaw(pitch, yaw, roll));
}

// Written out rather than forwarded to XMMatrixAffineTransformation, so both backends do the same
// operations in the same order. S is diagonal and rows 0-2 of a rotation have w == 0, so S * R * T
// reduces to scaling each rotation row and writing the translation into row 3.
VX_MATH_FORCEINLINE Matrix Matrix::TRS(Vector translation, Quaternion rotation_quaternion, Vector scale) noexcept
{
    const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(rotation_quaternion.Data());
    const DirectX::XMVECTOR scaleData = scale.Data();

    return Matrix{ DirectX::XMMATRIX{
        DirectX::XMVectorMultiply(rotation.r[0], DirectX::XMVectorSplatX(scaleData)),
        DirectX::XMVectorMultiply(rotation.r[1], DirectX::XMVectorSplatY(scaleData)),
        DirectX::XMVectorMultiply(rotation.r[2], DirectX::XMVectorSplatZ(scaleData)),
        DirectX::XMVectorSetW(translation.Data(), 1.0f) } };
}

// A rotation origin leaves the linear part identical, shifting only the translation row to
// t + Ro - Ro * R, with Ro through the *unscaled* rotation.
VX_MATH_FORCEINLINE Matrix Matrix::TRS(Vector translation,
                                       Quaternion rotation_quaternion,
                                       Vector scale,
                                       Vector rotation_origin) noexcept
{
    const DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(rotation_quaternion.Data());
    const DirectX::XMVECTOR scaleData = scale.Data();
    const DirectX::XMVECTOR originData = DirectX::XMVectorSetW(rotation_origin.Data(), 0.0f);
    const DirectX::XMVECTOR rotatedOrigin = DirectX::XMVector4Transform(originData, rotation);

    const DirectX::XMVECTOR translationRow = DirectX::XMVectorAdd(
        translation.Data(), DirectX::XMVectorSubtract(originData, rotatedOrigin));

    return Matrix{ DirectX::XMMATRIX{
        DirectX::XMVectorMultiply(rotation.r[0], DirectX::XMVectorSplatX(scaleData)),
        DirectX::XMVectorMultiply(rotation.r[1], DirectX::XMVectorSplatY(scaleData)),
        DirectX::XMVectorMultiply(rotation.r[2], DirectX::XMVectorSplatZ(scaleData)),
        DirectX::XMVectorSetW(translationRow, 1.0f) } };
}

VX_MATH_FORCEINLINE Matrix Matrix::LookAt(Vector eye, Vector target, Vector up) noexcept
{
    return Matrix{ DirectX::XMMatrixLookAtRH(eye.Data(), target.Data(), up.Data()) };
}
VX_MATH_FORCEINLINE Matrix Matrix::LookTo(Vector eye, Vector direction, Vector up) noexcept
{
    return Matrix{ DirectX::XMMatrixLookToRH(eye.Data(), direction.Data(), up.Data()) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Perspective(float fov_y_radians,
                                               float aspect_ratio,
                                               float near_plane,
                                               float far_plane) noexcept
{
    return Matrix{ DirectX::XMMatrixPerspectiveFovLH(fov_y_radians, aspect_ratio, near_plane, far_plane) };
}
VX_MATH_FORCEINLINE Matrix Matrix::PerspectiveLH(float fov_y_radians,
                                                 float aspect_ratio,
                                                 float near_plane,
                                                 float far_plane) noexcept
{
    return Matrix{ DirectX::XMMatrixPerspectiveFovLH(fov_y_radians, aspect_ratio, near_plane, far_plane) };
}
VX_MATH_FORCEINLINE Matrix Matrix::PerspectiveRH(float fov_y_radians,
                                                 float aspect_ratio,
                                                 float near_plane,
                                                 float far_plane) noexcept
{
    return Matrix{ DirectX::XMMatrixPerspectiveFovRH(fov_y_radians, aspect_ratio, near_plane, far_plane) };
}
VX_MATH_FORCEINLINE Matrix Matrix::Orthographic(float width,
                                                float height,
                                                float near_plane,
                                                float far_plane) noexcept
{
    return Matrix{ DirectX::XMMatrixOrthographicLH(width, height, near_plane, far_plane) };
}
VX_MATH_FORCEINLINE Matrix Matrix::OrthographicLH(float width,
                                                  float height,
                                                  float near_plane,
                                                  float far_plane) noexcept
{
    return Matrix{ DirectX::XMMatrixOrthographicLH(width, height, near_plane, far_plane) };
}
VX_MATH_FORCEINLINE Matrix Matrix::OrthographicRH(float width,
                                                  float height,
                                                  float near_plane,
                                                  float far_plane) noexcept
{
    return Matrix{ DirectX::XMMatrixOrthographicRH(width, height, near_plane, far_plane) };
}

VX_MATH_FORCEINLINE Matrix Matrix::Identity() noexcept
{
    return Matrix{ DirectX::XMMatrixIdentity() };
}

VX_MATH_FORCEINLINE Matrix Matrix::Zero() noexcept
{
    return Matrix{ DirectX::XMMATRIX{ DirectX::XMVectorZero(),
                                      DirectX::XMVectorZero(),
                                      DirectX::XMVectorZero(),
                                      DirectX::XMVectorZero() } };
}

VX_MATH_FORCEINLINE bool Matrix::IsIdentity() const noexcept
{
    return DirectX::XMMatrixIsIdentity(data);
}

VX_MATH_FORCEINLINE bool Matrix::IsNearlyEqual(const Matrix& other, float epsilon) const noexcept
{
    DirectX::XMVECTOR epsilon_vec = DirectX::XMVectorReplicate(epsilon);
    return DirectX::XMVector4NearEqual(data.r[0], other.data.r[0], epsilon_vec) &&
           DirectX::XMVector4NearEqual(data.r[1], other.data.r[1], epsilon_vec) &&
           DirectX::XMVector4NearEqual(data.r[2], other.data.r[2], epsilon_vec) &&
           DirectX::XMVector4NearEqual(data.r[3], other.data.r[3], epsilon_vec);
}

// ================================
// Matrix Free Functions
// ================================

template<int N>
VX_MATH_FORCEINLINE Vector Transform(Vector vector, Matrix matrix) noexcept
{
    static_assert(N >= 2 && N <= 4, "Transform dimensionality must be 2, 3, or 4");
    if constexpr (N == 2)
    {
        return Vector{ DirectX::XMVector2TransformCoord(vector.Data(), matrix.Data()) };
    }
    else if constexpr (N == 3)
    {
        return Vector{ DirectX::XMVector3TransformCoord(vector.Data(), matrix.Data()) };
    }
    else
    {
        return Vector{ DirectX::XMVector4Transform(vector.Data(), matrix.Data()) };
    }
}

VX_MATH_FORCEINLINE Vector TransformNormal(Vector normal, Matrix matrix) noexcept
{
    return Vector{ DirectX::XMVector3TransformNormal(normal.Data(), matrix.Data()) };
}

VX_MATH_FORCEINLINE Matrix operator*(float scalar, const Matrix& mat) noexcept
{
    return mat * scalar;
}

// Matrix conversion functions
VX_MATH_FORCEINLINE Matrix ToMatrix(const Float3x3& storage) noexcept
{
    const auto& m = storage.Data();
    DirectX::XMFLOAT3X3 tmp(&m[0][0]);
    return Matrix{ DirectX::XMLoadFloat3x3(&tmp) };
}

VX_MATH_FORCEINLINE Matrix ToMatrix(const Float4x3& storage) noexcept
{
    const auto& m = storage.Data();
    DirectX::XMFLOAT4X3 tmp(&m[0][0]);
    return Matrix{ DirectX::XMLoadFloat4x3(&tmp) };
}

VX_MATH_FORCEINLINE Matrix ToMatrix(const Float4x4& storage) noexcept
{
    const auto& m = storage.Data();
    DirectX::XMFLOAT4X4 tmp(&m[0][0]);
    return Matrix{ DirectX::XMLoadFloat4x4(&tmp) };
}

template<>
VX_MATH_FORCEINLINE Float3x3 FromMatrix(const Matrix& mat) noexcept
{
    DirectX::XMFLOAT3X3 result;
    DirectX::XMStoreFloat3x3(&result, mat.Data());
    return Float3x3(result.m[0][0],
                    result.m[0][1],
                    result.m[0][2],
                    result.m[1][0],
                    result.m[1][1],
                    result.m[1][2],
                    result.m[2][0],
                    result.m[2][1],
                    result.m[2][2]);
}

template<>
VX_MATH_FORCEINLINE Float4x3 FromMatrix(const Matrix& mat) noexcept
{
    DirectX::XMFLOAT4X3 result;
    DirectX::XMStoreFloat4x3(&result, mat.Data());
    return Float4x3(result.m[0][0],
                    result.m[0][1],
                    result.m[0][2],
                    result.m[1][0],
                    result.m[1][1],
                    result.m[1][2],
                    result.m[2][0],
                    result.m[2][1],
                    result.m[2][2],
                    result.m[3][0],
                    result.m[3][1],
                    result.m[3][2]);
}

template<>
VX_MATH_FORCEINLINE Float4x4 FromMatrix(const Matrix& mat) noexcept
{
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(&result, mat.Data());
    return Float4x4(result.m[0][0],
                    result.m[0][1],
                    result.m[0][2],
                    result.m[0][3],
                    result.m[1][0],
                    result.m[1][1],
                    result.m[1][2],
                    result.m[1][3],
                    result.m[2][0],
                    result.m[2][1],
                    result.m[2][2],
                    result.m[2][3],
                    result.m[3][0],
                    result.m[3][1],
                    result.m[3][2],
                    result.m[3][3]);
}

} // namespace velox::math

// Quaternion implementations live in their own file to keep this one navigable
#include "math/QuaternionBackendDX.inl"
#include "math/TranscendentalBackendDX.inl"
