// This file includes only MathStorage.hpp, so that the compiler proves the storage types need no SIMD
// backend. Do not include Math.hpp here.
#include "math/MathStorage.hpp"

#if defined(DIRECTX_MATH_VERSION) || defined(__wasm_simd128__) || defined(DD_MATH_BACKEND_WASM) || \
    defined(VX_MATH_BACKEND_WASM)
#error "MathStorage.hpp pulled in a SIMD backend"
#endif

#include <gtest/gtest.h>
#include <cstddef>
#include <type_traits>

namespace
{

using namespace math;

template<typename T>
constexpr bool IsPlainStorage = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

} // namespace

TEST(MathStorage, SizesMatchTheShaderLayout)
{
    EXPECT_EQ(sizeof(Float2), 8u);
    EXPECT_EQ(sizeof(Float3), 12u);
    EXPECT_EQ(sizeof(Float4), 16u);
    EXPECT_EQ(sizeof(Int3), 12u);
    EXPECT_EQ(sizeof(Int4), 16u);
    EXPECT_EQ(sizeof(UInt3), 12u);
    EXPECT_EQ(sizeof(UInt4), 16u);
    EXPECT_EQ(sizeof(Float3x3), 36u);
    EXPECT_EQ(sizeof(Float4x3), 48u);
    EXPECT_EQ(sizeof(Float4x4), 64u);
}

TEST(MathStorage, AlignmentIsFourBytes)
{
    EXPECT_EQ(alignof(Float2), 4u);
    EXPECT_EQ(alignof(Float3), 4u);
    EXPECT_EQ(alignof(Float4), 4u);
    EXPECT_EQ(alignof(Int3), 4u);
    EXPECT_EQ(alignof(Int4), 4u);
    EXPECT_EQ(alignof(UInt3), 4u);
    EXPECT_EQ(alignof(UInt4), 4u);
    EXPECT_EQ(alignof(Float3x3), 4u);
    EXPECT_EQ(alignof(Float4x3), 4u);
    EXPECT_EQ(alignof(Float4x4), 4u);
}

TEST(MathStorage, TypesAreStandardLayoutAndTriviallyCopyable)
{
    EXPECT_TRUE(IsPlainStorage<Float2>);
    EXPECT_TRUE(IsPlainStorage<Float3>);
    EXPECT_TRUE(IsPlainStorage<Float4>);
    EXPECT_TRUE(IsPlainStorage<Int3>);
    EXPECT_TRUE(IsPlainStorage<Int4>);
    EXPECT_TRUE(IsPlainStorage<UInt3>);
    EXPECT_TRUE(IsPlainStorage<UInt4>);
    EXPECT_TRUE(IsPlainStorage<Float3x3>);
    EXPECT_TRUE(IsPlainStorage<Float4x3>);
    EXPECT_TRUE(IsPlainStorage<Float4x4>);
}

TEST(MathStorage, Float3x3RowsAndColumns)
{
    const Float3x3 matrix{
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f
    };

    EXPECT_EQ(matrix.Row(0), (Float3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_EQ(matrix.Row(2), (Float3{ 7.0f, 8.0f, 9.0f }));
    EXPECT_EQ(matrix.Column(1), (Float3{ 2.0f, 5.0f, 8.0f }));
    EXPECT_EQ(matrix.Column(2), (Float3{ 3.0f, 6.0f, 9.0f }));
}

TEST(MathStorage, Float4x3RowsAndColumns)
{
    const Float4x3 matrix{
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f,
        10.0f, 11.0f, 12.0f
    };

    EXPECT_EQ(matrix.Row(3), (Float3{ 10.0f, 11.0f, 12.0f }));
    EXPECT_EQ(matrix.Row(1), (Float3{ 4.0f, 5.0f, 6.0f }));
    EXPECT_EQ(matrix.Column(0), (Float4{ 1.0f, 4.0f, 7.0f, 10.0f }));
    EXPECT_EQ(matrix.Column(2), (Float4{ 3.0f, 6.0f, 9.0f, 12.0f }));
}

TEST(MathStorage, Float4x4RowsAndColumns)
{
    const Float4x4 matrix{
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    };

    EXPECT_EQ(matrix.Row(0), (Float4{ 1.0f, 2.0f, 3.0f, 4.0f }));
    EXPECT_EQ(matrix.Row(3), (Float4{ 13.0f, 14.0f, 15.0f, 16.0f }));
    EXPECT_EQ(matrix.Column(1), (Float4{ 2.0f, 6.0f, 10.0f, 14.0f }));
    EXPECT_EQ(matrix.Column(3), (Float4{ 4.0f, 8.0f, 12.0f, 16.0f }));
}

TEST(MathStorage, ConstantsAndScalarOperators)
{
    EXPECT_EQ(Float4x4::Identity().Row(2), (Float4{ 0.0f, 0.0f, 1.0f, 0.0f }));
    EXPECT_EQ(Float4x4::Zero().Row(2), Float4{});
    EXPECT_EQ(Float3x3::Identity().Column(1), (Float3{ 0.0f, 1.0f, 0.0f }));
    EXPECT_EQ(Float4x3::Identity().Row(3), Float3{});

    const Float3 scaled = 2.0f * Float3{ 1.0f, 2.0f, 3.0f };
    EXPECT_EQ(scaled, (Float3{ 2.0f, 4.0f, 6.0f }));
}

TEST(MathStorage, Int3OperationsAndSwizzles)
{
    constexpr Int3 a{ 2, -4, 6 };
    constexpr Int3 b{ 1, 2, 3 };
    constexpr Int3 splat{ 5 };

    EXPECT_EQ(Int3{}, (Int3{ 0, 0, 0 }));
    EXPECT_EQ(splat, (Int3{ 5, 5, 5 }));
    EXPECT_EQ(a + b, (Int3{ 3, -2, 9 }));
    EXPECT_EQ(a - b, (Int3{ 1, -6, 3 }));
    EXPECT_EQ(a * b, (Int3{ 2, -8, 18 }));
    EXPECT_EQ(a / b, (Int3{ 2, -2, 2 }));
    EXPECT_EQ((Int3{ 7, -7, 8 } % Int3{ 3, 4, 5 }), (Int3{ 1, -3, 3 }));
    EXPECT_EQ(2 * a, (Int3{ 4, -8, 12 }));
    EXPECT_EQ(a / 2, (Int3{ 1, -2, 3 }));
    EXPECT_EQ(a % 4, (Int3{ 2, 0, 2 }));
    EXPECT_EQ(-a, (Int3{ -2, 4, -6 }));
    EXPECT_EQ(a.Dot(b), 12);
    EXPECT_EQ(b.LengthSquared(), 14);
    EXPECT_FLOAT_EQ((Int3{ 3, 4, 0 }).Length(), 5.0f);
    EXPECT_TRUE(b.AnyOf(2));
    EXPECT_TRUE(b.NoneOf(9));
    EXPECT_TRUE(splat.AllOf(5));
    EXPECT_LT(b, a);
    EXPECT_NE(a, b);

    Int3 m = b;
    m += a;
    m -= b;
    m *= 2;
    m /= 2;
    m[1] = 10;
    EXPECT_EQ(m, (Int3{ 2, 10, 6 }));

    const std::array<int32_t, 3> arr = a;
    EXPECT_EQ(Int3{ arr }, a);

    EXPECT_EQ(a.zyx(), (Int3{ 6, -4, 2 }));
    EXPECT_EQ(a.yyy(), (Int3{ -4, -4, -4 }));
    EXPECT_EQ(a.Min(b), (Int3{ 1, -4, 3 }));
    EXPECT_EQ(a.Max(b), (Int3{ 2, 2, 6 }));
    EXPECT_EQ(Min(a, b), (Int3{ 1, -4, 3 }));
    EXPECT_EQ(Max(a, b), (Int3{ 2, 2, 6 }));
}

TEST(MathStorage, Int4OperationsAndSwizzles)
{
    constexpr Int3 v3{ 1, -2, 3 };
    constexpr Int4 a{ v3, 4 };
    constexpr Int4 b{ 1, Int3{ 2, 3, 4 } };

    EXPECT_EQ(Int4{}, (Int4{ 0, 0, 0, 0 }));
    EXPECT_EQ(Int4{ 3 }, (Int4{ 3, 3, 3, 3 }));
    EXPECT_EQ(a + b, (Int4{ 2, 0, 6, 8 }));
    EXPECT_EQ(a - b, (Int4{ 0, -4, 0, 0 }));
    EXPECT_EQ(a * b, (Int4{ 1, -4, 9, 16 }));
    EXPECT_EQ(a / b, (Int4{ 1, -1, 1, 1 }));
    EXPECT_EQ(3 * b, (Int4{ 3, 6, 9, 12 }));
    EXPECT_EQ(-a, (Int4{ -1, 2, -3, -4 }));
    EXPECT_EQ(a.Dot(b), 22);
    EXPECT_EQ(b.LengthSquared(), 30);
    EXPECT_FLOAT_EQ((Int4{ 0, 3, 4, 0 }).Length(), 5.0f);
    EXPECT_TRUE(a.AnyOf(-2));
    EXPECT_TRUE(a.NoneOf(99));
    EXPECT_TRUE(Int4{ -7 }.AllOf(-7));
    EXPECT_LT(a, b);

    const std::array<int32_t, 4> arr = a;
    EXPECT_EQ(Int4{ arr }, a);

    EXPECT_EQ(a.xyz(), v3);
    EXPECT_EQ(a.rgb(), v3);
    EXPECT_EQ(a.bgra(), (Int4{ 3, -2, 1, 4 }));
    EXPECT_EQ(a.wxyz(), (Int4{ 4, 1, -2, 3 }));
    EXPECT_EQ(a.Min(Int4{ 5, -5, 2, 4 }), (Int4{ 1, -5, 2, 4 }));
    EXPECT_EQ(a.Max(Int4{ 5, -5, 2, 4 }), (Int4{ 5, -2, 3, 4 }));
    EXPECT_EQ(Min(a, b), (Int4{ 1, -2, 3, 4 }));
    EXPECT_EQ(Max(a, b), (Int4{ 1, 2, 3, 4 }));
}

TEST(MathStorage, UInt3OperationsAndSwizzles)
{
    constexpr UInt3 a{ 6u, 8u, 10u };
    constexpr UInt3 b{ 2u, 4u, 5u };

    EXPECT_EQ(UInt3{}, (UInt3{ 0u, 0u, 0u }));
    EXPECT_EQ(UInt3{ 7u }, (UInt3{ 7u, 7u, 7u }));
    EXPECT_EQ(a + b, (UInt3{ 8u, 12u, 15u }));
    EXPECT_EQ(a - b, (UInt3{ 4u, 4u, 5u }));
    EXPECT_EQ(a * b, (UInt3{ 12u, 32u, 50u }));
    EXPECT_EQ(a / b, (UInt3{ 3u, 2u, 2u }));
    EXPECT_EQ((a % UInt3{ 4u, 3u, 6u }), (UInt3{ 2u, 2u, 4u }));
    EXPECT_EQ(3u * b, (UInt3{ 6u, 12u, 15u }));
    EXPECT_EQ(a / 2u, (UInt3{ 3u, 4u, 5u }));
    EXPECT_EQ(a % 4u, (UInt3{ 2u, 0u, 2u }));
    EXPECT_EQ(a.Dot(b), 94u);
    EXPECT_EQ(b.LengthSquared(), 45u);
    EXPECT_FLOAT_EQ((UInt3{ 3u, 4u, 0u }).Length(), 5.0f);
    EXPECT_TRUE(b.AnyOf(4u));
    EXPECT_TRUE(b.NoneOf(9u));
    EXPECT_TRUE(UInt3{ 4u }.AllOf(4u));
    EXPECT_GT(a, b);

    const std::array<uint32_t, 3> arr = b;
    EXPECT_EQ(UInt3{ arr }, b);

    EXPECT_EQ(a.xzy(), (UInt3{ 6u, 10u, 8u }));
    EXPECT_EQ(a.zzz(), (UInt3{ 10u, 10u, 10u }));
    EXPECT_EQ(a.Min(UInt3{ 9u, 4u, 12u }), (UInt3{ 6u, 4u, 10u }));
    EXPECT_EQ(a.Max(UInt3{ 9u, 4u, 12u }), (UInt3{ 9u, 8u, 12u }));
    EXPECT_EQ(Min(a, b), b);
    EXPECT_EQ(Max(a, b), a);
}

TEST(MathStorage, UInt4OperationsAndSwizzles)
{
    constexpr UInt3 v3{ 2u, 4u, 6u };
    constexpr UInt4 a{ v3, 8u };
    constexpr UInt4 b{ 1u, UInt3{ 2u, 3u, 4u } };

    EXPECT_EQ(UInt4{}, (UInt4{ 0u, 0u, 0u, 0u }));
    EXPECT_EQ(UInt4{ 9u }, (UInt4{ 9u, 9u, 9u, 9u }));
    EXPECT_EQ(a + b, (UInt4{ 3u, 6u, 9u, 12u }));
    EXPECT_EQ(a - b, (UInt4{ 1u, 2u, 3u, 4u }));
    EXPECT_EQ(a * b, (UInt4{ 2u, 8u, 18u, 32u }));
    EXPECT_EQ(a / b, (UInt4{ 2u, 2u, 2u, 2u }));
    EXPECT_EQ(a % 3u, (UInt4{ 2u, 1u, 0u, 2u }));
    EXPECT_EQ(2u * b, a);
    EXPECT_EQ(a.Dot(b), 60u);
    EXPECT_EQ(b.LengthSquared(), 30u);
    EXPECT_FLOAT_EQ((UInt4{ 0u, 0u, 3u, 4u }).Length(), 5.0f);
    EXPECT_TRUE(a.AnyOf(8u));
    EXPECT_TRUE(a.NoneOf(1u));
    EXPECT_TRUE(UInt4{ 5u }.AllOf(5u));
    EXPECT_GT(a, b);

    const std::array<uint32_t, 4> arr = a;
    EXPECT_EQ(UInt4{ arr }, a);

    EXPECT_EQ(a.xyz(), v3);
    EXPECT_EQ(a.rgb(), v3);
    EXPECT_EQ(a.argb(), (UInt4{ 8u, 2u, 4u, 6u }));
    EXPECT_EQ(a.bgra(), (UInt4{ 6u, 4u, 2u, 8u }));
    EXPECT_EQ(a.Min(UInt4{ 5u, 2u, 9u, 4u }), (UInt4{ 2u, 2u, 6u, 4u }));
    EXPECT_EQ(a.Max(UInt4{ 5u, 2u, 9u, 4u }), (UInt4{ 5u, 4u, 9u, 8u }));
    EXPECT_EQ(Min(a, b), b);
    EXPECT_EQ(Max(a, b), a);
}
