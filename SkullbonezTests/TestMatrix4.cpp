//
// File: SkullbonezTests/TestMatrix4.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Matrix4.
//
// Mental model:
//   Matrix4 is the shared transform format used by rendering-facing and
//   physics-facing code. The tests use raw column-major values because that is
//   the public contract consumed by shader uploads and transform composition.
//
// Glossary:
//   Column-major matrix: Memory layout where each group of four floats stores
//     one matrix column.
//   Identity matrix: Transform that leaves positions and directions unchanged.
//   TRS: Translate * Rotate * Scale composition order for model transforms.
//
// Invariants:
//   - Data() exposes the same column-major memory as the public m array.
//   - Translation lives in column 3: m[12], m[13], and m[14].
//   - Inverse() of a stable affine transform composes back to identity within
//     floating-point tolerance.
//
// Related:
//   - SkullbonezSource/Maths/Matrix4.h
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/Matrix4.h"

#include <cmath>

using SkullbonezCore::Math::Transformation::Matrix4;

namespace
{
constexpr float kEpsilon = 0.00001f;

void CheckNear( float actual, float expected, float epsilon = kEpsilon )
{
    CHECK( std::fabs( actual - expected ) <= epsilon );
}

void CheckMatrixNear( const Matrix4& actual, const float ( &expected )[16], float epsilon = kEpsilon )
{
    const float* values = actual.Data();
    CHECK( values == actual.m );
    for ( int i = 0; i < 16; ++i )
    {
        CheckNear( values[i], expected[i], epsilon );
    }
}

void CheckIdentity( const Matrix4& value, float epsilon = kEpsilon )
{
    const float expected[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    CheckMatrixNear( value, expected, epsilon );
}
} // namespace


TEST_CASE( "Matrix4: inverse of identity is identity" )
{
    const Matrix4 identity;

    CheckIdentity( identity.Inverse() );
}


TEST_CASE( "Matrix4: TRS composition matches column-major manual values" )
{
    const Matrix4 composed =
        Matrix4::Translate( 3.0f, -2.0f, 5.0f ) * Matrix4::RotateAxis( 90.0f, 0.0f, 0.0f, 1.0f ) *
        Matrix4::Scale( 2.0f, 3.0f, 4.0f );
    const float expected[16] = {
        0.0f, 2.0f, 0.0f, 0.0f,
        -3.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        3.0f, -2.0f, 5.0f, 1.0f
    };

    CheckMatrixNear( composed, expected );
}


TEST_CASE( "Matrix4: inverse times original returns identity" )
{
    const Matrix4 original =
        Matrix4::Translate( -4.0f, 7.0f, 1.5f ) * Matrix4::RotateAxis( 37.0f, 1.0f, 2.0f, 3.0f ) *
        Matrix4::Scale( 2.0f, 0.5f, 3.0f );

    CheckIdentity( original.Inverse() * original, 0.0001f );
}
