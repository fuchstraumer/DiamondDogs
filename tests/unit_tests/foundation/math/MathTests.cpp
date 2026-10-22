#include "Math.hpp"
#include "math/MathHashes.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <format>
#include <numbers>
#include <string_view>
#include <unordered_map>

// Identities and known-answer cases rather than golden values, because the failures worth catching
// look plausible: a transposed shuffle index, a dropped cofactor sign, a coefficient off in the fifth
// decimal. A wrong dot product still returns a number of roughly the right size.
//
// Ported from VeloxRhi's MathTests.cpp. Only the harness changed: each Test function is now a gtest case,
// and the two check macros report through ADD_FAILURE_AT so that the failing line stays visible.
namespace
{

using namespace velox::math;

constexpr float k_Tight = 2e-4f;
constexpr float k_Loose = 1e-3f;
constexpr float k_Pi = std::numbers::pi_v<float>;

bool MatrixNear(const Matrix& lhs, const Matrix& rhs, float tolerance) noexcept
{
    for (size_t row = 0u; row < 4u; ++row)
    {
        for (size_t column = 0u; column < 4u; ++column)
        {
            const float difference = std::fabs(lhs[row, column] - rhs[row, column]);
            const float scale = 1.0f + std::fabs(lhs[row, column]) + std::fabs(rhs[row, column]);
            if (difference > tolerance * scale)
            {
                return false;
            }
        }
    }
    return true;
}

bool VectorNear(Vector actual, Vector expected, float tolerance) noexcept
{
    return actual.CompareNearEqual(expected, Vector::Replicate(tolerance)).AllTrue<4>();
}

// Not a similarity transform, so a wrong cofactor lane cannot cancel out by luck
Matrix MakeAwkwardMatrix() noexcept
{
    return Matrix{ 2.0f,  3.0f,  1.0f,  0.5f,
                   -1.0f, 4.0f,  2.0f,  0.25f,
                   3.0f,  1.0f,  -5.0f, 0.75f,
                   1.5f,  -2.0f, 0.5f,  2.0f };
}

void CheckImpl(const char* file, int line, bool condition, std::string_view description)
{
    if (condition)
    {
        return;
    }

    ADD_FAILURE_AT(file, line) << description;
}

// Relative, scaled by operand magnitude; degrades to absolute when expected is zero
void CheckNearImpl(const char* file,
                   int line,
                   float actual,
                   float expected,
                   float tolerance,
                   std::string_view description)
{
    const float difference = std::fabs(actual - expected);
    const float scale = 1.0f + std::fabs(actual) + std::fabs(expected);
    const bool withinTolerance = difference <= tolerance * scale;

    if (withinTolerance)
    {
        return;
    }

    ADD_FAILURE_AT(file, line) << description << "\n          expected " << expected << ", got " << actual
                               << " (difference " << difference << ")";
}

#define VX_CHECK(...) CheckImpl(__FILE__, __LINE__, __VA_ARGS__)
#define VX_CHECK_NEAR(...) CheckNearImpl(__FILE__, __LINE__, __VA_ARGS__)
#define VX_CHECK_SWEEP(...) CheckSweepImpl(__FILE__, __LINE__, __VA_ARGS__)

TEST(MathTests, MasksAndSelect)
{
    const Vector ascending{ 1.0f, 2.0f, 3.0f, 4.0f };
    const Vector descending{ 4.0f, 3.0f, 2.0f, 1.0f };

    VX_CHECK(ascending.CompareLess(descending).LaneBits() == 0x3u, "CompareLess lane bits");
    VX_CHECK(ascending.CompareGreater(descending).LaneBits() == 0xCu, "CompareGreater lane bits");
    VX_CHECK(ascending.CompareEqual(ascending).LaneBits() == 0xFu, "CompareEqual with self");
    VX_CHECK(ascending.CompareNotEqual(ascending).LaneBits() == 0x0u, "CompareNotEqual with self");
    VX_CHECK(ascending.CompareLessOrEqual(ascending).AllTrue<4>(), "CompareLessOrEqual with self");
    VX_CHECK(ascending.CompareGreaterOrEqual(ascending).AllTrue<4>(),
             "CompareGreaterOrEqual with self");

    // the width parameter has to actually restrict which lanes are consulted
    const VectorMask lowTwo = ascending.CompareLess(descending);
    VX_CHECK(lowTwo.AllTrue<2>(), "AllTrue<2> consults only lanes 0 and 1");
    VX_CHECK(!lowTwo.AllTrue<3>(), "AllTrue<3> catches lane 2");
    VX_CHECK(!lowTwo.AllTrue<4>(), "AllTrue<4> catches lanes 2 and 3");
    VX_CHECK(lowTwo.AnyTrue<4>(), "AnyTrue<4> sees lanes 0 and 1");
    VX_CHECK(!ascending.CompareGreater(descending).AnyTrue<2>(),
             "AnyTrue<2> does not see lanes 2 and 3");

    VX_CHECK(VectorMask::AllSet().AllTrue<4>(), "AllSet is all true");
    VX_CHECK(!VectorMask::AllClear().AnyTrue<4>(), "AllClear is none true");

    VX_CHECK((lowTwo & ~lowTwo).LaneBits() == 0x0u, "mask and complement is empty");
    VX_CHECK((lowTwo | ~lowTwo).LaneBits() == 0xFu, "mask or complement is full");
    VX_CHECK((lowTwo ^ lowTwo).LaneBits() == 0x0u, "mask xor itself is empty");

    // the backends spell this in opposite orders underneath, so pin the operand order down
    const Vector picked = Select(lowTwo, descending, ascending);
    VX_CHECK_NEAR(picked.x(), 1.0f, k_Tight, "Select takes when_set where the mask is set");
    VX_CHECK_NEAR(picked.z(), 2.0f, k_Tight, "Select takes when_clear where the mask is clear");

    VX_CHECK(Vector::QuietNaN().IsNaN().AllTrue<4>(), "QuietNaN is NaN");
    VX_CHECK(!ascending.IsNaN().AnyTrue<4>(), "a finite vector is not NaN");
    VX_CHECK(Vector::Infinity().IsInfinite().AllTrue<4>(), "Infinity is infinite");
    VX_CHECK((-Vector::Infinity()).IsInfinite().AllTrue<4>(), "negative infinity is infinite");
    VX_CHECK(!ascending.IsInfinite().AnyTrue<4>(), "a finite vector is not infinite");

    const Vector nudged{ 1.0f + 1e-6f, 2.0f, 3.0f, 4.0f };
    VX_CHECK(ascending.CompareNearEqual(nudged, Vector::Replicate(1e-4f)).AllTrue<4>(),
             "CompareNearEqual inside tolerance");
    VX_CHECK(!ascending.CompareNearEqual(descending, Vector::Replicate(1e-4f)).AnyTrue<4>(),
             "CompareNearEqual outside tolerance");
}

TEST(MathTests, RoundingAndLanes)
{
    // both backends round halves to even
    const Vector halves{ 0.5f, 1.5f, 2.5f, -0.5f };
    const Vector rounded = halves.Round();
    VX_CHECK_NEAR(rounded.x(), 0.0f, k_Tight, "Round(0.5) ties to even");
    VX_CHECK_NEAR(rounded.y(), 2.0f, k_Tight, "Round(1.5) ties to even");
    VX_CHECK_NEAR(rounded.z(), 2.0f, k_Tight, "Round(2.5) ties to even");

    const Vector mixed{ 1.7f, -1.7f, 1.2f, -1.2f };
    VX_CHECK_NEAR(mixed.Truncate().y(), -1.0f, k_Tight, "Truncate goes toward zero");
    VX_CHECK_NEAR(mixed.Floor().y(), -2.0f, k_Tight, "Floor goes toward negative infinity");
    VX_CHECK_NEAR(mixed.Ceil().y(), -1.0f, k_Tight, "Ceil goes toward positive infinity");

    // truncating, so the remainder keeps the dividend's sign
    const Vector modded = Vector{ 7.0f, -7.0f, 5.5f, 1.0f }.Mod(Vector::Replicate(3.0f));
    VX_CHECK_NEAR(modded.x(), 1.0f, k_Tight, "Mod of a positive dividend");
    VX_CHECK_NEAR(modded.y(), -1.0f, k_Tight, "Mod keeps the dividend's sign");
    VX_CHECK_NEAR(modded.z(), 2.5f, k_Tight, "Mod of a fractional dividend");

    // must wrap into [-pi, pi] including the negative half, which truncation would get wrong
    const Vector angles{ 2.0f * k_Pi + 1.0f, -2.0f * k_Pi - 1.0f, 0.5f, 4.0f * k_Pi };
    const Vector wrapped = angles.ModAngles();
    VX_CHECK_NEAR(wrapped.x(), 1.0f, k_Loose, "ModAngles wraps 2pi+1 down to 1");
    VX_CHECK_NEAR(wrapped.y(), -1.0f, k_Loose, "ModAngles wraps -2pi-1 up to -1");
    VX_CHECK_NEAR(wrapped.z(), 0.5f, k_Loose, "ModAngles leaves an in-range angle alone");
    VX_CHECK_NEAR(wrapped.w(), 0.0f, k_Loose, "ModAngles wraps 4pi to 0");

    const Vector ascending{ 1.0f, 2.0f, 3.0f, 4.0f };
    const Vector descending{ 4.0f, 3.0f, 2.0f, 1.0f };

    VX_CHECK_NEAR(ascending.SplatX().w(), 1.0f, k_Tight, "SplatX reaches every lane");
    VX_CHECK_NEAR(ascending.SplatY().x(), 2.0f, k_Tight, "SplatY");
    VX_CHECK_NEAR(ascending.SplatZ().x(), 3.0f, k_Tight, "SplatZ");
    VX_CHECK_NEAR(ascending.SplatW().x(), 4.0f, k_Tight, "SplatW");

    VX_CHECK(VectorNear(ascending.MergeXY(descending), Vector{ 1.0f, 4.0f, 2.0f, 3.0f }, k_Tight),
             "MergeXY interleaves the x and y lanes");
    VX_CHECK(VectorNear(ascending.MergeZW(descending), Vector{ 3.0f, 2.0f, 4.0f, 1.0f }, k_Tight),
             "MergeZW interleaves the z and w lanes");
    VX_CHECK(VectorNear(ascending.Swizzle<3, 2, 1, 0>(), descending, k_Tight),
             "Swizzle reverses the lanes");
    // 0-3 index this vector, 4-7 the argument
    VX_CHECK(VectorNear(ascending.Permute<0, 4, 1, 5>(descending),
                        Vector{ 1.0f, 4.0f, 2.0f, 3.0f },
                        k_Tight),
             "Permute indexes the second vector with 4-7");

    VX_CHECK_NEAR(ascending.AndInt(ascending).x(), 1.0f, k_Tight, "AndInt with self is identity");
    VX_CHECK_NEAR(ascending.XorInt(ascending).x(), 0.0f, k_Tight, "XorInt with self is zero");
    VX_CHECK_NEAR(ascending.OrInt(Vector::Zero()).x(), 1.0f, k_Tight, "OrInt with zero is identity");
}

TEST(MathTests, DotProducts)
{
    const Vector lhs{ 1.0f, 2.0f, 3.0f, 4.0f };
    const Vector rhs{ 5.0f, 6.0f, 7.0f, 8.0f };

    VX_CHECK_NEAR(lhs.Dot<2>(rhs), 17.0f, k_Tight, "Dot<2>");
    VX_CHECK_NEAR(lhs.Dot<3>(rhs), 38.0f, k_Tight, "Dot<3>");
    VX_CHECK_NEAR(lhs.Dot<4>(rhs), 70.0f, k_Tight, "Dot<4>");

    // narrow widths must ignore unused lanes, not assume they are zeroed - huge values there would
    // swamp the result if they leaked in
    const Vector dirty{ 1.0f, 2.0f, 1e30f, -1e30f };
    const Vector clean{ 5.0f, 6.0f, 1e30f, 1e30f };
    VX_CHECK_NEAR(dirty.Dot<2>(clean), 17.0f, k_Tight, "Dot<2> ignores lanes 2 and 3");
    VX_CHECK_NEAR(Vector{ 1.0f, 2.0f, 3.0f, 1e30f }.Dot<3>(Vector{ 5.0f, 6.0f, 7.0f, 1e30f }),
                  38.0f,
                  k_Tight,
                  "Dot<3> ignores lane 3");

    // if DotVec stopped splatting, everything built on it would still "work" but only in lane 0
    const Vector broadcast = lhs.DotVec<4>(rhs);
    VX_CHECK(VectorNear(broadcast, Vector::Replicate(70.0f), k_Tight),
             "DotVec<4> splats to all four lanes");
    VX_CHECK(VectorNear(lhs.DotVec<3>(rhs), Vector::Replicate(38.0f), k_Tight),
             "DotVec<3> splats to all four lanes");
}

TEST(MathTests, VectorGeometry)
{
    const Vector unitX{ 1.0f, 0.0f, 0.0f };
    const Vector unitY{ 0.0f, 1.0f, 0.0f };
    const Vector unitZ{ 0.0f, 0.0f, 1.0f };

    VX_CHECK(VectorNear(unitX.Cross(unitY), unitZ, k_Tight), "Cross(x, y) is z");
    VX_CHECK(VectorNear(unitY.Cross(unitZ), unitX, k_Tight), "Cross(y, z) is x");
    VX_CHECK(VectorNear(unitZ.Cross(unitX), unitY, k_Tight), "Cross(z, x) is y");
    VX_CHECK(VectorNear(unitY.Cross(unitX), -unitZ, k_Tight), "Cross anticommutes");

    const Vector threeFour{ 3.0f, 4.0f, 0.0f };
    VX_CHECK_NEAR(threeFour.Length<3>(), 5.0f, k_Tight, "Length<3> of a 3-4-5 triangle");
    VX_CHECK_NEAR(threeFour.LengthSq<3>(), 25.0f, k_Tight, "LengthSq<3>");
    VX_CHECK_NEAR(threeFour.Normalize<3>().Length<3>(), 1.0f, k_Tight, "Normalize<3> gives unit length");

    // reflecting off +Y flips only y
    const Vector reflected = Vector{ 1.0f, -1.0f, 0.0f }.Reflect<3>(unitY);
    VX_CHECK_NEAR(reflected.x(), 1.0f, k_Tight, "Reflect<3> preserves the tangential component");
    VX_CHECK_NEAR(reflected.y(), 1.0f, k_Tight, "Reflect<3> flips the normal component");

    const Vector wild{ -1.0f, 0.25f, 5.0f, 0.5f };
    VX_CHECK_NEAR(wild.Saturate().x(), 0.0f, k_Tight, "Saturate clamps below zero");
    VX_CHECK_NEAR(wild.Saturate().y(), 0.25f, k_Tight, "Saturate leaves in-range values alone");
    VX_CHECK_NEAR(wild.Saturate().z(), 1.0f, k_Tight, "Saturate clamps above one");
    const Vector clamped = wild.Clamp(Vector::Zero(), Vector::Replicate(2.0f));
    VX_CHECK_NEAR(clamped.z(), 2.0f, k_Tight, "Clamp respects the upper bound");
    VX_CHECK_NEAR(wild.Abs().x(), 1.0f, k_Tight, "Abs");
    VX_CHECK_NEAR(wild.Min(Vector::Zero()).z(), 0.0f, k_Tight, "Min");
    VX_CHECK_NEAR(wild.Max(Vector::Zero()).x(), 0.0f, k_Tight, "Max");
    VX_CHECK_NEAR(unitX.Lerp(unitY, 0.5f).x(), 0.5f, k_Tight, "Lerp midpoint");
}

TEST(MathTests, MatrixCore)
{
    const Matrix identity = Matrix::Identity();
    const Matrix awkward = MakeAwkwardMatrix();

    VX_CHECK(MatrixNear(awkward.Transpose().Transpose(), awkward, k_Tight),
             "Transpose is an involution");
    VX_CHECK(MatrixNear(identity * awkward, awkward, k_Tight), "Identity is a left identity");
    VX_CHECK(MatrixNear(awkward * identity, awkward, k_Tight), "Identity is a right identity");
    VX_CHECK(identity.IsIdentity(), "IsIdentity on the identity");
    VX_CHECK(!awkward.IsIdentity(), "IsIdentity rejects a general matrix");

    // one wrong lane index still produces something that looks like a matrix, so check both sides
    VX_CHECK(MatrixNear(awkward * awkward.Inverse(), identity, k_Tight),
             "M times its inverse is the identity");
    VX_CHECK(MatrixNear(awkward.Inverse() * awkward, identity, k_Tight),
             "the inverse times M is the identity");
    VX_CHECK(MatrixNear(identity.Inverse(), identity, k_Tight), "the identity is its own inverse");

    const Matrix rotation = Matrix::RotationAxis(Vector{ 0.3f, -0.5f, 0.8f }, 1.1f);
    VX_CHECK(MatrixNear(rotation.Inverse(), rotation.Transpose(), k_Loose),
             "a rotation inverts to its transpose");
    VX_CHECK_NEAR(rotation.Determinant(), 1.0f, k_Loose, "a rotation has unit determinant");
    VX_CHECK_NEAR(Matrix::Scale(2.0f, 3.0f, 4.0f).Determinant(),
                  24.0f,
                  k_Tight,
                  "the determinant of a scale is the product of its factors");

    VX_CHECK_NEAR(awkward[2, 1], 1.0f, k_Tight, "operator[] reads the right element");
    Matrix mutated = awkward;
    mutated.SetElement(2, 1, 42.0f);
    VX_CHECK_NEAR(mutated[2, 1], 42.0f, k_Tight, "SetElement writes the right element");
    VX_CHECK_NEAR(awkward.GetColumn(2).x(), awkward[0, 2], k_Tight, "GetColumn reads down a column");
    VX_CHECK_NEAR(awkward.GetColumn(2).w(), awkward[3, 2], k_Tight, "GetColumn reaches the last row");
    Matrix columnWritten = Matrix::Zero();
    columnWritten.SetColumn(1, Vector{ 7.0f, 8.0f, 9.0f, 10.0f });
    VX_CHECK_NEAR(columnWritten[0, 1], 7.0f, k_Tight, "SetColumn writes the first row");
    VX_CHECK_NEAR(columnWritten[3, 1], 10.0f, k_Tight, "SetColumn writes the last row");
    VX_CHECK(VectorNear(awkward.GetRow(1), Vector{ -1.0f, 4.0f, 2.0f, 0.25f }, k_Tight), "GetRow");

    // the eye lands at the view-space origin regardless of handedness conventions
    const Vector eye{ 4.0f, 5.0f, 6.0f };
    const Matrix view = Matrix::LookAt(eye, Vector::Zero(), Vector{ 0.0f, 1.0f, 0.0f });
    VX_CHECK(VectorNear(Transform<3>(eye, view), Vector{ 0.0f, 0.0f, 0.0f, 1.0f }, k_Loose),
             "LookAt maps the eye to the view-space origin");

    // zeroing w is what makes translation drop out
    const Matrix translated = Matrix::Translation(100.0f, 200.0f, 300.0f);
    VX_CHECK(VectorNear(TransformNormal(Vector{ 1.0f, 0.0f, 0.0f }, translated),
                        Vector{ 1.0f, 0.0f, 0.0f, 0.0f },
                        k_Tight),
             "TransformNormal ignores translation");
    VX_CHECK(VectorNear(Transform<3>(Vector{ 1.0f, 0.0f, 0.0f }, translated),
                        Vector{ 101.0f, 200.0f, 300.0f, 1.0f },
                        k_Tight),
             "Transform<3> applies translation");
}

TEST(MathTests, TRS)
{
    const Vector translation{ 5.0f, -3.0f, 2.0f };
    const Vector scale{ 2.0f, 0.5f, 1.5f };
    const Quaternion rotation = Quaternion::RotationAxis(Vector{ 1.0f, 2.0f, 3.0f }, 0.7f);

    // the analytical form has to agree with the general composition it replaces
    const Matrix composed = (Matrix::Scale(scale) * Matrix::RotationQuaternion(rotation)) *
                            Matrix::Translation(translation);
    const Matrix analytical = Matrix::TRS(translation, rotation, scale);
    VX_CHECK(MatrixNear(composed, analytical, k_Loose),
             "TRS equals Scale times RotationQuaternion times Translation");

    VX_CHECK(MatrixNear(analytical * analytical.Inverse(), Matrix::Identity(), k_Loose),
             "a TRS matrix inverts cleanly");

    // a rotation about a pivot leaves the pivot where it is
    const Vector pivot{ 3.0f, 4.0f, -1.0f };
    const Matrix aboutPivot =
        Matrix::TRS(Vector::Zero(), rotation, Vector{ 1.0f, 1.0f, 1.0f }, pivot);
    VX_CHECK(VectorNear(Transform<3>(pivot, aboutPivot), Vector{ 3.0f, 4.0f, -1.0f, 1.0f }, k_Loose),
             "TRS with a rotation origin leaves that point fixed");

    VX_CHECK(MatrixNear(Matrix::TRS(translation, rotation, scale, Vector::Zero()), analytical, k_Tight),
             "TRS with a zero pivot matches the three-argument form");

    // TRS promises scale-then-rotate, which differs from rotate-then-scale
    const Matrix scaleThenRotate = Matrix::Scale(scale) * Matrix::RotationQuaternion(rotation);
    const Matrix rotateThenScale = Matrix::RotationQuaternion(rotation) * Matrix::Scale(scale);
    VX_CHECK(!MatrixNear(scaleThenRotate, rotateThenScale, k_Loose),
             "non-uniform scale does not commute with rotation (guards the S*R convention)");
    VX_CHECK(MatrixNear(Matrix::TRS(Vector::Zero(), rotation, scale), scaleThenRotate, k_Loose),
             "TRS applies scale in the object's local frame");
}

TEST(MathTests, Quaternions)
{
    const Quaternion identity = Quaternion::Identity();
    VX_CHECK(identity.IsIdentity(), "the default quaternion is the identity");
    VX_CHECK(Quaternion{}.IsIdentity(), "the default constructor gives the identity");
    VX_CHECK_NEAR(identity.w(), 1.0f, k_Tight, "the identity has w == 1");
    VX_CHECK_NEAR(identity.x(), 0.0f, k_Tight, "the identity has a zero vector part");

    const Vector axis{ 1.0f, 2.0f, 3.0f };
    const Quaternion rotation = Quaternion::RotationAxis(axis, 0.9f);
    VX_CHECK_NEAR(rotation.Length(), 1.0f, k_Tight, "RotationAxis produces a unit quaternion");
    VX_CHECK_NEAR(rotation.Normalize().Length(), 1.0f, k_Tight, "Normalize gives unit length");
    VX_CHECK_NEAR(rotation.LengthSq(), 1.0f, k_Tight, "LengthSq of a unit quaternion");
    VX_CHECK_NEAR(rotation.Dot(rotation), 1.0f, k_Tight, "Dot with self is the squared length");

    // conjugate and inverse coincide for a unit quaternion
    VX_CHECK(VectorNear(rotation.Conjugate(), rotation.Inverse(), k_Tight),
             "conjugate and inverse agree for a unit quaternion");
    VX_CHECK(rotation.Multiply(rotation.Inverse()).IsIdentity() ||
                 VectorNear(rotation.Multiply(rotation.Inverse()), identity, k_Loose),
             "a rotation composed with its inverse is the identity");

    // the case Conjugate cannot cover
    const Quaternion scaled{ rotation.x() * 3.0f, rotation.y() * 3.0f, rotation.z() * 3.0f,
                             rotation.w() * 3.0f };
    VX_CHECK(VectorNear(scaled.Multiply(scaled.Inverse()), identity, k_Loose),
             "Inverse handles a non-unit quaternion");

    VX_CHECK(VectorNear(identity.Multiply(rotation), rotation, k_Tight),
             "the identity is a left identity under Multiply");
    VX_CHECK(VectorNear(rotation.Multiply(identity), rotation, k_Tight),
             "the identity is a right identity under Multiply");

    // Multiply applies *this first, so the row-vector matrix equivalent is thisMatrix * argMatrix.
    // This is the check that notices if that ever flips
    const Quaternion first = Quaternion::RotationAxis(Vector{ 1.0f, 0.0f, 0.0f }, 0.5f);
    const Quaternion second = Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 0.8f);
    VX_CHECK(MatrixNear(first.Multiply(second).ToMatrix(),
                        first.ToMatrix() * second.ToMatrix(),
                        k_Loose),
             "Multiply composes in the same order as the equivalent matrix product");
    VX_CHECK(!MatrixNear(first.Multiply(second).ToMatrix(),
                         second.ToMatrix() * first.ToMatrix(),
                         k_Loose),
             "composition is not commutative (so the previous check is meaningful)");

    // must agree with going through the matrix
    const Vector point{ 0.3f, -0.7f, 1.4f };
    VX_CHECK(VectorNear(rotation.RotateVector(point),
                        TransformNormal(point, rotation.ToMatrix()),
                        k_Loose),
             "RotateVector agrees with the matrix form");
    VX_CHECK_NEAR(rotation.RotateVector(point).Length<3>(),
                  point.Length<3>(),
                  k_Loose,
                  "rotation preserves length");
    // the axis is the one direction a rotation leaves alone
    const Vector normalizedAxis = axis.Normalize<3>();
    VX_CHECK(VectorNear(rotation.RotateVector(normalizedAxis), normalizedAxis, k_Loose),
             "rotating the axis itself is a no-op");

    // Shepperd's method picks one of four branches, so several rotations are needed to reach them
    const Quaternion samples[]{ Quaternion::RotationAxis(Vector{ 1.0f, 0.0f, 0.0f }, 0.4f),
                                Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 2.9f),
                                Quaternion::RotationAxis(Vector{ 0.0f, 0.0f, 1.0f }, -2.0f),
                                Quaternion::RotationAxis(Vector{ 1.0f, 1.0f, 1.0f }, 3.0f),
                                Quaternion::RotationAxis(Vector{ -2.0f, 0.5f, 1.0f }, 1.7f),
                                identity };
    for (const Quaternion& sample : samples)
    {
        const Quaternion roundTripped = Quaternion::FromMatrix(sample.ToMatrix());
        // q and -q are the same rotation, so compare matrices rather than components
        VX_CHECK(MatrixNear(roundTripped.ToMatrix(), sample.ToMatrix(), k_Loose),
                 "FromMatrix inverts ToMatrix across all Shepperd branches");
        VX_CHECK_NEAR(roundTripped.Length(), 1.0f, k_Loose, "FromMatrix returns a unit quaternion");
    }

    // axis-angle round trip
    Vector recoveredAxis{};
    float recoveredAngle = 0.0f;
    Quaternion::RotationAxis(axis, 1.2f).ToAxisAngle(recoveredAxis, recoveredAngle);
    VX_CHECK_NEAR(recoveredAngle, 1.2f, k_Loose, "ToAxisAngle recovers the angle");
    VX_CHECK(VectorNear(recoveredAxis.Normalize<3>(), axis.Normalize<3>(), k_Loose),
             "ToAxisAngle recovers the axis direction");

    // Euler composition order
    VX_CHECK(MatrixNear(Quaternion::RotationRollPitchYaw(0.3f, 0.7f, -0.4f).ToMatrix(),
                        Matrix::RotationX(0.3f) * Matrix::RotationY(0.7f) * Matrix::RotationZ(-0.4f),
                        k_Loose),
             "RotationRollPitchYaw composes X then Y then Z");
    VX_CHECK(MatrixNear(Matrix::RotationRollPitchYaw(0.3f, 0.7f, -0.4f),
                        Quaternion::RotationRollPitchYaw(0.3f, 0.7f, -0.4f).ToMatrix(),
                        k_Tight),
             "Matrix and Quaternion RotationRollPitchYaw agree");

    // single-axis quaternions match the dedicated matrix builders
    VX_CHECK(MatrixNear(Quaternion::RotationAxis(Vector{ 1.0f, 0.0f, 0.0f }, 0.6f).ToMatrix(),
                        Matrix::RotationX(0.6f),
                        k_Loose),
             "a quaternion about X matches Matrix::RotationX");
    VX_CHECK(MatrixNear(Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 0.6f).ToMatrix(),
                        Matrix::RotationY(0.6f),
                        k_Loose),
             "a quaternion about Y matches Matrix::RotationY");
    VX_CHECK(MatrixNear(Quaternion::RotationAxis(Vector{ 0.0f, 0.0f, 1.0f }, 0.6f).ToMatrix(),
                        Matrix::RotationZ(0.6f),
                        k_Loose),
             "a quaternion about Z matches Matrix::RotationZ");
    // RotationNormal skips the normalize, so it should agree when handed an already-unit axis
    VX_CHECK(VectorNear(Quaternion::RotationNormal(normalizedAxis, 0.9f), rotation, k_Tight),
             "RotationNormal matches RotationAxis for a unit axis");

    // two independent implementations of the same thing
    VX_CHECK(MatrixNear(Matrix::RotationAxis(axis, 1.1f),
                        Quaternion::RotationAxis(axis, 1.1f).ToMatrix(),
                        k_Loose),
             "Matrix::RotationAxis agrees with the quaternion path");
}

TEST(MathTests, StorageConversions)
{
    const Float4 asFloat4{ 1.0f, 2.0f, 3.0f, 4.0f };
    const Float4 roundTripped = FromVector(ToVector(asFloat4));
    VX_CHECK_NEAR(roundTripped.x, 1.0f, k_Tight, "Float4 round trip x");
    VX_CHECK_NEAR(roundTripped.w, 4.0f, k_Tight, "Float4 round trip w");

    const Float3 asFloat3{ 1.0f, 2.0f, 3.0f };
    const Float3 roundTripped3 = FromVector(ToVector(asFloat3));
    VX_CHECK_NEAR(roundTripped3.z, 3.0f, k_Tight, "Float3 round trip z");

    const Float2 asFloat2{ 1.0f, 2.0f };
    const Float2 roundTripped2 = FromVector(ToVector(asFloat2));
    VX_CHECK_NEAR(roundTripped2.y, 2.0f, k_Tight, "Float2 round trip y");

    const Matrix awkward = MakeAwkwardMatrix();
    VX_CHECK(MatrixNear(ToMatrix(FromMatrix<Float4x4>(awkward)), awkward, k_Tight),
             "Float4x4 storage round trip");

    const Float3x3 upperLeft = FromMatrix<Float3x3>(awkward);
    VX_CHECK_NEAR(upperLeft[0, 0], awkward[0, 0], k_Tight, "Float3x3 keeps the upper-left corner");
    VX_CHECK_NEAR(upperLeft[2, 2], awkward[2, 2], k_Tight, "Float3x3 keeps the lower-right corner");

    const Float4x3 affine = FromMatrix<Float4x3>(awkward);
    VX_CHECK_NEAR(affine[3, 0], awkward[3, 0], k_Tight, "Float4x3 keeps the translation row");

    // a Quaternion has to survive storage through the Vector path
    const Quaternion rotation = Quaternion::RotationAxis(Vector{ 1.0f, 2.0f, 3.0f }, 0.5f);
    const Float4 storedRotation = FromVector(static_cast<Vector>(rotation));
    const Quaternion restored{ ToVector(storedRotation) };
    VX_CHECK(VectorNear(restored, rotation, k_Tight), "Quaternion survives a Float4 round trip");

    // hashes need only be usable and self-consistent
    std::unordered_map<Float3, int> lookup;
    lookup[asFloat3] = 7;
    VX_CHECK(lookup.at(asFloat3) == 7, "Float3 works as an unordered_map key");
    VX_CHECK(std::hash<Float3>{}(asFloat3) == std::hash<Float3>{}(asFloat3),
             "Float3 hashing is deterministic");
    VX_CHECK(std::hash<Float3>{}(asFloat3) != std::hash<Float3>{}(Float3{ 3.0f, 2.0f, 1.0f }),
             "Float3 hashing distinguishes a permutation");
}

// Sweeps a domain for the worst error rather than checking a few points. The only thing that catches a
// mistyped coefficient: a wrong constant in the fifth decimal still gives a curve of the right shape
// and magnitude everywhere, so spot checks and identities both sail past it.
struct SweepResult
{
    double worstError;
    float worstInput;
};

template<typename ApproximateFn, typename ReferenceFn>
SweepResult SweepAbsolute(float first,
                          float last,
                          int steps,
                          ApproximateFn&& approximate,
                          ReferenceFn&& reference)
{
    SweepResult result{ 0.0, first };
    for (int step = 0; step <= steps; ++step)
    {
        const float input = first + (last - first) * (static_cast<float>(step) / static_cast<float>(steps));
        const double error = std::fabs(static_cast<double>(approximate(input)) - reference(input));
        if (error > result.worstError)
        {
            result.worstError = error;
            result.worstInput = input;
        }
    }
    return result;
}

template<typename ApproximateFn, typename ReferenceFn>
SweepResult SweepRelative(float first,
                          float last,
                          int steps,
                          ApproximateFn&& approximate,
                          ReferenceFn&& reference)
{
    SweepResult result{ 0.0, first };
    for (int step = 0; step <= steps; ++step)
    {
        const float input = first + (last - first) * (static_cast<float>(step) / static_cast<float>(steps));
        const double expected = reference(input);
        const double error =
            std::fabs((static_cast<double>(approximate(input)) - expected) / (expected == 0.0 ? 1.0 : expected));
        if (error > result.worstError)
        {
            result.worstError = error;
            result.worstInput = input;
        }
    }
    return result;
}

// The measured number distinguishes "the polynomial is wrong" from "the bound was optimistic"
void CheckSweepImpl(const char* file,
                    int line,
                    const SweepResult& result,
                    double bound,
                    std::string_view description)
{
    const bool withinBound = result.worstError < bound;
    CheckImpl(file, line, withinBound, description);
    if (!withinBound)
    {
        ADD_FAILURE_AT(file, line) << std::format("          worst error {:.3e} at input {}, bound {:.3e}",
                                                  result.worstError,
                                                  result.worstInput,
                                                  bound);
    }
}

// Lets one sweep drive both the vector and scalar forms
float VectorSin(float radians)
{
    return Vector::Replicate(radians).Sin().x();
}
float VectorCos(float radians)
{
    return Vector::Replicate(radians).Cos().x();
}
float VectorSinEst(float radians)
{
    return Vector::Replicate(radians).SinEst().x();
}
float VectorCosEst(float radians)
{
    return Vector::Replicate(radians).CosEst().x();
}
float VectorExp2(float value)
{
    return Vector::Replicate(value).Exp2().x();
}
float VectorLog2(float value)
{
    return Vector::Replicate(value).Log2().x();
}
float VectorExp2Est(float value)
{
    return Vector::Replicate(value).Exp2Est().x();
}
float VectorLog2Est(float value)
{
    return Vector::Replicate(value).Log2Est().x();
}

float VectorTan(float radians)
{
    return Vector::Replicate(radians).Tan().x();
}
float VectorATan(float value)
{
    return Vector::Replicate(value).ATan().x();
}
float VectorASin(float value)
{
    return Vector::Replicate(value).ASin().x();
}
float VectorACos(float value)
{
    return Vector::Replicate(value).ACos().x();
}
float VectorTanH(float value)
{
    return Vector::Replicate(value).TanH().x();
}
float VectorSinH(float value)
{
    return Vector::Replicate(value).SinH().x();
}
float VectorCosH(float value)
{
    return Vector::Replicate(value).CosH().x();
}

TEST(MathTests, TranscendentalAccuracy)
{
    const auto referenceSin = [](float x) { return std::sin(static_cast<double>(x)); };
    const auto referenceCos = [](float x) { return std::cos(static_cast<double>(x)); };
    const auto referenceExp2 = [](float x) { return std::exp2(static_cast<double>(x)); };
    const auto referenceLog2 = [](float x) { return std::log2(static_cast<double>(x)); };

    // one period, then far outside it so range reduction is exercised
    const SweepResult sinNear = SweepAbsolute(-k_Pi, k_Pi, 20000, VectorSin, referenceSin);
    VX_CHECK_SWEEP(sinNear, 3e-7, "Vector::Sin within 3e-7 over one period");
    const SweepResult cosNear = SweepAbsolute(-k_Pi, k_Pi, 20000, VectorCos, referenceCos);
    VX_CHECK_SWEEP(cosNear, 3e-7, "Vector::Cos within 3e-7 over one period");

    const SweepResult sinFar = SweepAbsolute(-40.0f, 40.0f, 20000, VectorSin, referenceSin);
    VX_CHECK_SWEEP(sinFar, 5e-6, "Vector::Sin survives range reduction out to +-40 radians");
    const SweepResult cosFar = SweepAbsolute(-40.0f, 40.0f, 20000, VectorCos, referenceCos);
    VX_CHECK_SWEEP(cosFar, 5e-6, "Vector::Cos survives range reduction out to +-40 radians");

    const SweepResult sinEst = SweepAbsolute(-k_Pi, k_Pi, 20000, VectorSinEst, referenceSin);
    VX_CHECK_SWEEP(sinEst, 2e-5, "Vector::SinEst within 2e-5");
    const SweepResult cosEst = SweepAbsolute(-k_Pi, k_Pi, 20000, VectorCosEst, referenceCos);
    VX_CHECK_SWEEP(cosEst, 2e-5, "Vector::CosEst within 2e-5");

    // Exp2 over the range that does not overflow float, relative because the magnitude spans 2^120.
    // The bound is set by DirectXMath: XMVectorExp2 measures ~1.0e-5, while the WASM six-term series
    // measures ~1.9e-7. Backend-fastest means the weaker one sets what may be documented.
    const SweepResult exp2Sweep = SweepRelative(-60.0f, 60.0f, 20000, VectorExp2, referenceExp2);
    VX_CHECK_SWEEP(exp2Sweep, 2e-5, "Vector::Exp2 relative error within 2e-5");
    const SweepResult exp2EstSweep = SweepRelative(-60.0f, 60.0f, 20000, VectorExp2Est, referenceExp2);
    VX_CHECK_SWEEP(exp2EstSweep, 1e-4, "Vector::Exp2Est relative error within 1e-4");
    // 2^n is exactly representable, so the polynomial should return 1 for integer input
    VX_CHECK_NEAR(VectorExp2(0.0f), 1.0f, 1e-6f, "Exp2(0) is 1");
    VX_CHECK_NEAR(VectorExp2(10.0f), 1024.0f, 1e-6f, "Exp2(10) is 1024");
    VX_CHECK_NEAR(VectorExp2(-3.0f), 0.125f, 1e-6f, "Exp2(-3) is 0.125");

    const SweepResult log2Sweep = SweepAbsolute(1e-4f, 1e4f, 20000, VectorLog2, referenceLog2);
    VX_CHECK_SWEEP(log2Sweep, 2e-6, "Vector::Log2 absolute error within 2e-6");
    const SweepResult log2EstSweep = SweepAbsolute(1e-4f, 1e4f, 20000, VectorLog2Est, referenceLog2);
    VX_CHECK_SWEEP(log2EstSweep, 1e-5, "Vector::Log2Est absolute error within 1e-5");
    VX_CHECK_NEAR(VectorLog2(1.0f), 0.0f, 1e-6f, "Log2(1) is 0");
    VX_CHECK_NEAR(VectorLog2(1024.0f), 10.0f, 1e-6f, "Log2(1024) is 10");
    VX_CHECK_NEAR(VectorLog2(0.25f), -2.0f, 1e-6f, "Log2(0.25) is -2");
    // either side of the sqrt2 fold in the mantissa reduction
    VX_CHECK_NEAR(VectorLog2(1.4f), std::log2(1.4f), 1e-5f, "Log2 just below the sqrt2 fold");
    VX_CHECK_NEAR(VectorLog2(1.45f), std::log2(1.45f), 1e-5f, "Log2 just above the sqrt2 fold");

    // sharing coefficient tables, these should track the vector forms closely
    const SweepResult scalarSin = SweepAbsolute(-k_Pi, k_Pi, 20000, Sin, referenceSin);
    VX_CHECK_SWEEP(scalarSin, 3e-7, "scalar Sin within 3e-7");
    const SweepResult scalarCos = SweepAbsolute(-k_Pi, k_Pi, 20000, Cos, referenceCos);
    VX_CHECK_SWEEP(scalarCos, 3e-7, "scalar Cos within 3e-7");
    const SweepResult scalarExp2 = SweepRelative(-60.0f, 60.0f, 20000, Exp2, referenceExp2);
    VX_CHECK_SWEEP(scalarExp2, 1e-6, "scalar Exp2 relative error within 1e-6");
    const SweepResult scalarLog2 = SweepAbsolute(1e-4f, 1e4f, 20000, Log2, referenceLog2);
    VX_CHECK_SWEEP(scalarLog2, 2e-6, "scalar Log2 absolute error within 2e-6");
}

TEST(MathTests, TranscendentalIdentities)
{
    // these hold regardless of what libm thinks
    for (float angle : { -7.3f, -1.2f, -0.4f, 0.0f, 0.4f, 1.2f, 3.9f, 7.3f, 100.0f })
    {
        const Vector broadcast = Vector::Replicate(angle);
        const auto [sine, cosine] = broadcast.SinCos();

        // disagreement here means the shared reduction has diverged
        VX_CHECK_NEAR(sine.x(), broadcast.Sin().x(), 1e-6f, "SinCos sine matches Sin");
        VX_CHECK_NEAR(cosine.x(), broadcast.Cos().x(), 1e-6f, "SinCos cosine matches Cos");

        // catches a wrong quadrant sign that a magnitude check would miss
        const float pythagorean = sine.x() * sine.x() + cosine.x() * cosine.x();
        VX_CHECK_NEAR(pythagorean, 1.0f, 1e-5f, "sin^2 + cos^2 is 1");

        const ScalarSinCos scalarPair = SinCos(angle);
        VX_CHECK_NEAR(scalarPair.sin, sine.x(), 1e-5f, "scalar SinCos tracks the vector form");
        VX_CHECK_NEAR(scalarPair.cos, cosine.x(), 1e-5f, "scalar SinCos cosine tracks the vector form");

        const ScalarSinCos estimatePair = SinCosEst(angle);
        VX_CHECK_NEAR(estimatePair.sin, sine.x(), 2e-4f, "scalar SinCosEst is within its bound");
    }

    // must invert each other
    for (float value : { 0.01f, 0.5f, 1.0f, 3.7f, 17.0f, 1000.0f })
    {
        const Vector broadcast = Vector::Replicate(value);
        VX_CHECK_NEAR(broadcast.Log2().Exp2().x(), value, 1e-5f, "Exp2(Log2(x)) is x");
    }
    for (float value : { -12.0f, -1.5f, 0.0f, 2.25f, 30.0f })
    {
        const Vector broadcast = Vector::Replicate(value);
        VX_CHECK_NEAR(broadcast.Exp2().Log2().x(), value, 1e-5f, "Log2(Exp2(x)) is x");
    }

    // odd/even symmetry
    VX_CHECK_NEAR(VectorSin(-1.3f), -VectorSin(1.3f), 1e-6f, "sine is odd");
    VX_CHECK_NEAR(VectorCos(-1.3f), VectorCos(1.3f), 1e-6f, "cosine is even");

    // quadrant boundaries, where the reflection takes over
    VX_CHECK_NEAR(VectorSin(k_Pi * 0.5f), 1.0f, 1e-5f, "sin(pi/2) is 1");
    VX_CHECK_NEAR(VectorCos(k_Pi * 0.5f), 0.0f, 1e-5f, "cos(pi/2) is 0");
    VX_CHECK_NEAR(VectorSin(k_Pi), 0.0f, 1e-5f, "sin(pi) is 0");
    VX_CHECK_NEAR(VectorCos(k_Pi), -1.0f, 1e-5f, "cos(pi) is -1");
    VX_CHECK_NEAR(VectorCos(k_Pi * 1.5f), 0.0f, 1e-5f, "cos(3pi/2) is 0");
    VX_CHECK_NEAR(VectorSin(k_Pi * 1.5f), -1.0f, 1e-5f, "sin(3pi/2) is -1");

    // four genuinely independent lanes, not lane 0 splattered everywhere
    const Vector distinct{ 0.0f, k_Pi * 0.5f, k_Pi, k_Pi * 1.5f };
    const auto [lanes, cosLanes] = distinct.SinCos();
    VX_CHECK_NEAR(lanes.x(), 0.0f, 1e-5f, "lane 0 sine");
    VX_CHECK_NEAR(lanes.y(), 1.0f, 1e-5f, "lane 1 sine");
    VX_CHECK_NEAR(lanes.z(), 0.0f, 1e-5f, "lane 2 sine");
    VX_CHECK_NEAR(lanes.w(), -1.0f, 1e-5f, "lane 3 sine");
    VX_CHECK_NEAR(cosLanes.x(), 1.0f, 1e-5f, "lane 0 cosine");
    VX_CHECK_NEAR(cosLanes.z(), -1.0f, 1e-5f, "lane 2 cosine");

    // conversion helpers
    VX_CHECK_NEAR(DegreesToRadians(180.0f), k_Pi, 1e-6f, "DegreesToRadians");
    VX_CHECK_NEAR(RadiansToDegrees(k_Pi), 180.0f, 1e-6f, "RadiansToDegrees");
    VX_CHECK_NEAR(Lerp(2.0f, 4.0f, 0.25f), 2.5f, 1e-6f, "scalar Lerp");
    VX_CHECK_NEAR(Clamp(5.0f, 0.0f, 1.0f), 1.0f, 1e-6f, "scalar Clamp");
    VX_CHECK_NEAR(Saturate(-3.0f), 0.0f, 1e-6f, "scalar Saturate");
    VX_CHECK_NEAR(ModAngles(2.0f * k_Pi + 0.5f), 0.5f, 1e-5f, "scalar ModAngles");

    // RotationX/Y/Z go through SinCos; confirm that did not shift them
    VX_CHECK(MatrixNear(Matrix::RotationX(0.7f).Inverse(), Matrix::RotationX(-0.7f), k_Loose),
             "RotationX(-a) inverts RotationX(a) after the SinCos rewrite");
    VX_CHECK_NEAR(Matrix::RotationY(0.7f).Determinant(), 1.0f, k_Loose, "RotationY stays orthonormal");
    VX_CHECK(MatrixNear(Matrix::RotationZ(0.0f), Matrix::Identity(), k_Tight),
             "RotationZ(0) is the identity");
}

TEST(MathTests, DerivedTranscendentals)
{
    const auto referenceATan = [](float x) { return std::atan(static_cast<double>(x)); };
    const auto referenceASin = [](float x) { return std::asin(static_cast<double>(x)); };
    const auto referenceACos = [](float x) { return std::acos(static_cast<double>(x)); };
    const auto referenceTanH = [](float x) { return std::tanh(static_cast<double>(x)); };
    const auto referenceSinH = [](float x) { return std::sinh(static_cast<double>(x)); };
    const auto referenceCosH = [](float x) { return std::cosh(static_cast<double>(x)); };

    // atan across the reciprocal-identity boundary at |x| == 1, and far beyond it
    VX_CHECK_SWEEP(SweepAbsolute(-1.0f, 1.0f, 20000, VectorATan, referenceATan), 2e-6,
                   "ATan within 2e-6 on [-1, 1]");
    VX_CHECK_SWEEP(SweepAbsolute(-100.0f, 100.0f, 20000, VectorATan, referenceATan), 2e-6,
                   "ATan survives the reciprocal reduction out to +-100");

    // asin/acos go through ATan2, so they are worst near +-1 where the sqrt collapses
    VX_CHECK_SWEEP(SweepAbsolute(-0.999f, 0.999f, 20000, VectorASin, referenceASin), 2e-5,
                   "ASin within 2e-5");
    VX_CHECK_SWEEP(SweepAbsolute(-0.999f, 0.999f, 20000, VectorACos, referenceACos), 2e-5,
                   "ACos within 2e-5");

    VX_CHECK_SWEEP(SweepAbsolute(-10.0f, 10.0f, 20000, VectorTanH, referenceTanH), 2e-5,
                   "TanH within 2e-5");
    // sinh needs both metrics. Near zero, sinh(x) -> x and the difference of two exponentials that are
    // both ~1 cancels, so relative error is poor while absolute stays tiny. Away from zero the value
    // grows exponentially, so absolute error grows with it while relative is excellent.
    VX_CHECK_SWEEP(SweepAbsolute(-0.5f, 0.5f, 20000, VectorSinH, referenceSinH), 1e-5,
                   "SinH absolute error within 1e-5 near zero");
    VX_CHECK_SWEEP(SweepRelative(0.5f, 10.0f, 20000, VectorSinH, referenceSinH), 2e-5,
                   "SinH relative error within 2e-5 away from zero");
    VX_CHECK_SWEEP(SweepRelative(-10.0f, 10.0f, 20000, VectorCosH, referenceCosH), 2e-5,
                   "CosH relative error within 2e-5");

    // Tan away from the poles, where sin/cos is well conditioned
    VX_CHECK_SWEEP(SweepAbsolute(-1.4f, 1.4f, 20000, VectorTan,
                                 [](float x) { return std::tan(static_cast<double>(x)); }),
                   1e-4, "Tan within 1e-4 away from the poles");

    // endpoints and identities
    VX_CHECK_NEAR(VectorASin(1.0f), k_Pi * 0.5f, 1e-4f, "asin(1) is pi/2");
    VX_CHECK_NEAR(VectorASin(-1.0f), -k_Pi * 0.5f, 1e-4f, "asin(-1) is -pi/2");
    VX_CHECK_NEAR(VectorACos(1.0f), 0.0f, 1e-4f, "acos(1) is 0");
    VX_CHECK_NEAR(VectorACos(-1.0f), k_Pi, 1e-4f, "acos(-1) is pi");
    VX_CHECK_NEAR(VectorACos(0.0f), k_Pi * 0.5f, 1e-4f, "acos(0) is pi/2");
    VX_CHECK_NEAR(VectorTanH(0.0f), 0.0f, 1e-6f, "tanh(0) is 0");
    // the saturating form must not produce NaN where the exponential overflows
    VX_CHECK_NEAR(VectorTanH(50.0f), 1.0f, 1e-6f, "TanH saturates to 1 rather than going NaN");
    VX_CHECK_NEAR(VectorTanH(-50.0f), -1.0f, 1e-6f, "TanH saturates to -1");
    VX_CHECK(!Vector::Replicate(90.0f).TanH().IsNaN().AnyTrue<4>(), "TanH stays finite past overflow");
    VX_CHECK_NEAR(VectorCosH(0.0f), 1.0f, 1e-6f, "cosh(0) is 1");
    VX_CHECK_NEAR(VectorSinH(0.0f), 0.0f, 1e-6f, "sinh(0) is 0");
    // cosh^2 - sinh^2 == 1
    const Vector hyperbolicArg = Vector::Replicate(1.3f);
    const float coshValue = hyperbolicArg.CosH().x();
    const float sinhValue = hyperbolicArg.SinH().x();
    VX_CHECK_NEAR(coshValue * coshValue - sinhValue * sinhValue, 1.0f, 1e-4f,
                  "cosh^2 - sinh^2 is 1");

    // ATan2 quadrants: all four, plus the axes
    VX_CHECK_NEAR(Vector::ATan2(Vector::Replicate(1.0f), Vector::Replicate(1.0f)).x(),
                  k_Pi * 0.25f, 1e-5f, "ATan2 first quadrant");
    VX_CHECK_NEAR(Vector::ATan2(Vector::Replicate(1.0f), Vector::Replicate(-1.0f)).x(),
                  k_Pi * 0.75f, 1e-5f, "ATan2 second quadrant");
    VX_CHECK_NEAR(Vector::ATan2(Vector::Replicate(-1.0f), Vector::Replicate(-1.0f)).x(),
                  -k_Pi * 0.75f, 1e-5f, "ATan2 third quadrant");
    VX_CHECK_NEAR(Vector::ATan2(Vector::Replicate(-1.0f), Vector::Replicate(1.0f)).x(),
                  -k_Pi * 0.25f, 1e-5f, "ATan2 fourth quadrant");
    VX_CHECK_NEAR(Vector::ATan2(Vector::Replicate(1.0f), Vector::Zero()).x(),
                  k_Pi * 0.5f, 1e-5f, "ATan2 on the +y axis");
    VX_CHECK_NEAR(Vector::ATan2(Vector::Zero(), Vector::Zero()).x(), 0.0f, 1e-6f,
                  "ATan2(0, 0) is 0 rather than NaN");

    // Pow, now off Exp2/Log2 rather than a per-lane std::pow loop
    VX_CHECK_NEAR(Vector::Replicate(2.0f).Pow(10.0f).x(), 1024.0f, 1e-4f, "2^10 is 1024");
    VX_CHECK_NEAR(Vector::Replicate(9.0f).Pow(0.5f).x(), 3.0f, 1e-4f, "Pow(x, 0.5) is sqrt(x)");
    VX_CHECK_NEAR(Vector::Replicate(7.0f).Pow(1.0f).x(), 7.0f, 1e-4f, "Pow(x, 1) is x");
    VX_CHECK_NEAR(Vector::Replicate(7.0f).Pow(0.0f).x(), 1.0f, 1e-4f, "Pow(x, 0) is 1");
    VX_CHECK_NEAR(Vector::Replicate(2.0f).Pow(-2.0f).x(), 0.25f, 1e-4f, "a negative exponent");
    VX_CHECK_NEAR(Pow(3.0f, 4.0f), 81.0f, 1e-4f, "scalar Pow");

    // base conversions
    VX_CHECK_NEAR(Vector::Exp(Vector::Replicate(1.0f)).x(), std::exp(1.0f), 1e-5f, "Exp(1) is e");
    VX_CHECK_NEAR(Vector::Log(Vector::Replicate(std::exp(1.0f))).x(), 1.0f, 1e-5f, "Log(e) is 1");
    VX_CHECK_NEAR(Vector::Exp10(Vector::Replicate(3.0f)).x(), 1000.0f, 1e-4f, "Exp10(3) is 1000");
    VX_CHECK_NEAR(Vector::Log10(Vector::Replicate(1000.0f)).x(), 3.0f, 1e-5f, "Log10(1000) is 3");

    // the estimate family: loose bounds on purpose, and backend-dependent
    for (float value : { 0.05f, 0.5f, 1.0f, 2.0f, 17.0f, 500.0f })
    {
        const Vector broadcast = Vector::Replicate(value);
        const float reciprocalSqrt = broadcast.ReciprocalSqrtEst().x();
        VX_CHECK_NEAR(reciprocalSqrt, 1.0f / std::sqrt(value), 3e-3f, "ReciprocalSqrtEst");
        VX_CHECK_NEAR(broadcast.SqrtEst().x(), std::sqrt(value), 3e-3f, "SqrtEst");
        VX_CHECK_NEAR(broadcast.ReciprocalEst().x(), 1.0f / value, 3e-3f, "ReciprocalEst");
    }
    const Vector toNormalize{ 3.0f, 4.0f, 0.0f, 0.0f };
    VX_CHECK_NEAR(toNormalize.NormalizeEst<3>().Length<3>(), 1.0f, 3e-3f, "NormalizeEst<3>");
    VX_CHECK_NEAR(toNormalize.LengthEst<3>(), 5.0f, 3e-3f, "LengthEst<3>");
    VX_CHECK_NEAR(toNormalize.ReciprocalLengthEst<3>(), 0.2f, 3e-3f, "ReciprocalLengthEst<3>");

    // scalar counterparts track the vector forms
    VX_CHECK_NEAR(ATan(0.7f), VectorATan(0.7f), 1e-5f, "scalar ATan tracks the vector form");
    VX_CHECK_NEAR(ATan(3.5f), VectorATan(3.5f), 1e-5f, "scalar ATan past the reduction boundary");
    VX_CHECK_NEAR(ASin(0.6f), VectorASin(0.6f), 1e-4f, "scalar ASin tracks the vector form");
    VX_CHECK_NEAR(ACos(0.6f), VectorACos(0.6f), 1e-4f, "scalar ACos tracks the vector form");
    VX_CHECK_NEAR(TanH(0.8f), VectorTanH(0.8f), 1e-4f, "scalar TanH tracks the vector form");
    VX_CHECK_NEAR(ATan2(1.0f, -1.0f), k_Pi * 0.75f, 1e-5f, "scalar ATan2 quadrant");
    VX_CHECK_NEAR(Tan(0.9f), VectorTan(0.9f), 1e-4f, "scalar Tan tracks the vector form");
}

TEST(MathTests, Slerp)
{
    const Quaternion from = Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 0.2f);
    const Quaternion to = Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 1.4f);

    VX_CHECK(MatrixNear(Quaternion::Slerp(from, to, 0.0f).ToMatrix(), from.ToMatrix(), k_Loose),
             "Slerp at t = 0 returns the first endpoint");
    VX_CHECK(MatrixNear(Quaternion::Slerp(from, to, 1.0f).ToMatrix(), to.ToMatrix(), k_Loose),
             "Slerp at t = 1 returns the second endpoint");

    // on a shared axis the interpolation is linear in angle, which gives an exact expectation
    const Quaternion midpoint = Quaternion::Slerp(from, to, 0.5f);
    VX_CHECK(MatrixNear(midpoint.ToMatrix(),
                        Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 0.8f).ToMatrix(),
                        k_Loose),
             "Slerp is linear in angle about a shared axis");

    // unit length has to survive, or repeated interpolation drifts
    for (float t : { 0.0f, 0.1f, 0.35f, 0.5f, 0.9f, 1.0f })
    {
        VX_CHECK_NEAR(Quaternion::Slerp(from, to, t).Length(), 1.0f, 1e-4f,
                      "Slerp stays unit length");
    }

    // shortest arc: negating an endpoint names the same rotation, so the path must not change
    const Quaternion negated{ -static_cast<Vector>(to) };
    VX_CHECK(MatrixNear(Quaternion::Slerp(from, negated, 0.5f).ToMatrix(), midpoint.ToMatrix(), k_Loose),
             "Slerp takes the shortest arc regardless of endpoint sign");

    // near-parallel endpoints go through the lerp fallback rather than dividing by ~0
    const Quaternion barelyApart = Quaternion::RotationAxis(Vector{ 0.0f, 1.0f, 0.0f }, 0.2001f);
    const Quaternion blended = Quaternion::Slerp(from, barelyApart, 0.5f);
    VX_CHECK_NEAR(blended.Length(), 1.0f, 1e-4f, "the near-parallel fallback stays unit length");
    VX_CHECK(!static_cast<Vector>(blended).IsNaN().AnyTrue<4>(),
             "the near-parallel fallback does not divide by zero");
    VX_CHECK(MatrixNear(Quaternion::Slerp(from, from, 0.5f).ToMatrix(), from.ToMatrix(), k_Loose),
             "Slerp between identical endpoints is a no-op");
}

} // namespace
