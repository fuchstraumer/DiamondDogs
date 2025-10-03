// This file is kept for backward compatibility with build system
// Actual math tests are now in dedicated test files:
// - test_float2.cpp, test_float3.cpp, test_float4.cpp
// - test_integer_vectors.cpp
// - test_vector_simd.cpp
// - test_matrix_storage.cpp, test_matrix_simd.cpp
// - test_matrix_projection.cpp
// - test_math_integration.cpp

#include <gtest/gtest.h>

TEST(MathTest, TestsSplitIntoSeparateFiles)
{
    SUCCEED() << "Math tests are now implemented in separate files";
}
