//
// File: SkullbonezTests/TestMatrix4.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Matrix4.
//
// Summary:
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
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/Matrix4.h"
#include "../SkullbonezSource/Maths/Quaternion.h"

#include <cmath>

using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;

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


TEST_CASE( "Matrix4: coincident look-at falls back to identity and zero up falls back to world Y" )
{
    const Vector3 eye( 3.0f, -2.0f, 7.0f );
    CheckIdentity( Matrix4::LookAt( eye, eye, Vector3( 0.0f, 1.0f, 0.0f ) ) );

    const Matrix4 zeroUp = Matrix4::LookAt( Vector3( 0.0f, 0.0f, 5.0f ),
                                            Vector3( 0.0f, 0.0f, 0.0f ),
                                            Vector3( 0.0f, 0.0f, 0.0f ) );
    const Matrix4 worldUp = Matrix4::LookAt( Vector3( 0.0f, 0.0f, 5.0f ),
                                             Vector3( 0.0f, 0.0f, 0.0f ),
                                             Vector3( 0.0f, 1.0f, 0.0f ) );
    CheckMatrixNear( zeroUp, worldUp.m );
}


TEST_CASE( "Matrix4: parallel look-at up vectors choose a finite perpendicular basis" )
{
    const Matrix4 alongY = Matrix4::LookAt( Vector3( 0.0f, 0.0f, 0.0f ),
                                            Vector3( 0.0f, 1.0f, 0.0f ),
                                            Vector3( 0.0f, 1.0f, 0.0f ) );
    const Matrix4 alongX = Matrix4::LookAt( Vector3( 0.0f, 0.0f, 0.0f ),
                                            Vector3( 1.0f, 0.0f, 0.0f ),
                                            Vector3( 1.0f, 0.0f, 0.0f ) );

    CheckIdentity( alongY.Inverse() * alongY, 0.0001f );
    CheckIdentity( alongX.Inverse() * alongX, 0.0001f );
}


TEST_CASE( "Matrix4: quaternion and axis-angle conversions agree within the rotation epsilon" )
{
    // Why: 2e-5 covers the two independent float/trigonometric evaluation
    // paths while remaining far below any visible transform discrepancy.
    constexpr float kRotationEpsilon = 0.00002f;
    Quaternion quarterTurn;
    quarterTurn.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 1.57079632679f );

    const Matrix4 fromQuaternion = Matrix4::FromQuaternion( quarterTurn );
    const Matrix4 fromAxis = Matrix4::RotateAxis( 90.0f, 0.0f, 0.0f, 1.0f );
    CheckMatrixNear( fromQuaternion, fromAxis.m, kRotationEpsilon );
    CheckIdentity( fromQuaternion.Inverse() * fromQuaternion, kRotationEpsilon );
}

TEST_CASE( "Matrix4: fully inverted shadow normal produces a finite deterministic transform" )
{
    const Matrix4 inverted =
        Matrix4::ShadowFromNormal( 3.0f, -2.0f, 5.0f, Vector3( 0.0f, -1.0f, 0.0f ), 2.0f );
    const float expected[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -2.0f, 0.0f,
        3.0f, -2.0f, 5.0f, 1.0f
    };

    for ( float value : inverted.m )
    {
        CHECK( std::isfinite( value ) );
    }
    CheckMatrixNear( inverted, expected, 0.00001f );
}


TEST_CASE( "Matrix4: legacy and DX12 projection helpers pin their depth identities" )
{
    const Matrix4 perspective = Matrix4::Perspective( 90.0f, 2.0f, 1.0f, 11.0f );
    const Matrix4 perspectiveDx = Matrix4::PerspectiveZeroToOne( 90.0f, 2.0f, 1.0f, 11.0f );
    CheckNear( perspective.m[0], 0.5f );
    CheckNear( perspective.m[5], 1.0f );
    CheckNear( perspective.m[10], -1.2f );
    CheckNear( perspective.m[14], -2.2f );
    CheckNear( perspectiveDx.m[10], -1.1f );
    CheckNear( perspectiveDx.m[14], -1.1f );

    const Matrix4 ortho = Matrix4::Ortho( -2.0f, 6.0f, -3.0f, 5.0f, 1.0f, 11.0f );
    const Matrix4 orthoDx = Matrix4::OrthoZeroToOne( -2.0f, 6.0f, -3.0f, 5.0f, 1.0f, 11.0f );
    CheckNear( ortho.m[0], 0.25f );
    CheckNear( ortho.m[5], 0.25f );
    CheckNear( ortho.m[10], -0.2f );
    CheckNear( ortho.m[12], -0.5f );
    CheckNear( ortho.m[13], -0.25f );
    CheckNear( ortho.m[14], -1.2f );
    CheckNear( orthoDx.m[10], -0.1f );
    CheckNear( orthoDx.m[14], -0.1f );
}


TEST_CASE( "Matrix4: vector overloads and uniform scale preserve TRS composition" )
{
    const Matrix4 vectorForm = Matrix4::Translate( Vector3( 2.0f, 3.0f, 4.0f ) ) *
                               Matrix4::Scale( Vector3( 5.0f, 5.0f, 5.0f ) );
    const Matrix4 scalarForm = Matrix4::Translate( 2.0f, 3.0f, 4.0f ) * Matrix4::Scale( 5.0f );
    CheckMatrixNear( vectorForm, scalarForm.m );

    Matrix4 inPlace;
    inPlace *= scalarForm;
    CheckMatrixNear( inPlace, scalarForm.m );
}


TEST_CASE( "Matrix4: singular inverse returns the documented identity fallback" )
{
    CheckIdentity( Matrix4::Scale( 1.0f, 0.0f, 1.0f ).Inverse() );
}
