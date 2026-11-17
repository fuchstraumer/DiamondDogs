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
    EXPECT_EQ(sizeof(Float3x3), 36u);
    EXPECT_EQ(sizeof(Float4x3), 48u);
    EXPECT_EQ(sizeof(Float4x4), 64u);
}

TEST(MathStorage, AlignmentIsFourBytes)
{
    EXPECT_EQ(alignof(Float2), 4u);
    EXPECT_EQ(alignof(Float3), 4u);
    EXPECT_EQ(alignof(Float4), 4u);
    EXPECT_EQ(alignof(Float3x3), 4u);
    EXPECT_EQ(alignof(Float4x3), 4u);
    EXPECT_EQ(alignof(Float4x4), 4u);
}

TEST(MathStorage, TypesAreStandardLayoutAndTriviallyCopyable)
{
    EXPECT_TRUE(IsPlainStorage<Float2>);
    EXPECT_TRUE(IsPlainStorage<Float3>);
    EXPECT_TRUE(IsPlainStorage<Float4>);
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
