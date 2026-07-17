//
// File: SkullbonezTests/TestQuaternion.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Quaternion.
//
// Summary:
//   Quaternion behavior is a small but engine-specific contract: component
//   signs, multiplication order, and normalization all feed physics and camera
//   orientation. These tests describe the current API before later math library
//   extraction changes the build shape around it.
//
// Glossary:
//   Identity quaternion: No-rotation value (0,0,0,1).
//   Axis-angle rotation: Rotation described by one axis vector and one angle.
//   Drift: Floating-point length error accumulated by repeated multiplication.
//
// Invariants:
//   - Normalise() resets a zero quaternion to identity.
//   - RotateAboutAxis() uses the current engine component sign convention.
//   - Repeated multiplication must stay close enough to unit length for a later
//     Normalise() pass to repair it deterministically.
//
// Related:
//   - SkullbonezSource/Maths/Quaternion.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Maths/Quaternion.h"

#include <cmath>

using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr float kEpsilon = 0.00001f;
constexpr float kHalfPi = 1.57079632679f;

struct QuaternionComponents
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

QuaternionComponents ComponentsOf( const Quaternion& q )
{
    QuaternionComponents components;
    q.GetComponents( components.x, components.y, components.z, components.w );
    return components;
}

float MagnitudeSquared( const Quaternion& q )
{
    const QuaternionComponents c = ComponentsOf( q );
    return c.x * c.x + c.y * c.y + c.z * c.z + c.w * c.w;
}

void CheckQuaternionNear( const Quaternion& value, const QuaternionComponents& expected, float epsilon = kEpsilon )
{
    const QuaternionComponents actual = ComponentsOf( value );
    CHECK( actual.x == doctest::Approx( expected.x ).epsilon( epsilon ) );
    CHECK( actual.y == doctest::Approx( expected.y ).epsilon( epsilon ) );
    CHECK( actual.z == doctest::Approx( expected.z ).epsilon( epsilon ) );
    CHECK( actual.w == doctest::Approx( expected.w ).epsilon( epsilon ) );
}
} // namespace


TEST_CASE( "Quaternion: Normalise is idempotent for a non-zero quaternion" )
{
    Quaternion value( 0.2f, -0.3f, 0.4f, 0.5f );

    value.Normalise();
    const QuaternionComponents once = ComponentsOf( value );
    value.Normalise();

    CheckQuaternionNear( value, once );
    CHECK( MagnitudeSquared( value ) == doctest::Approx( 1.0f ).epsilon( kEpsilon ) );
}


TEST_CASE( "Quaternion: Normalise resets a zero quaternion to identity" )
{
    Quaternion value( 0.0f, 0.0f, 0.0f, 0.0f );

    value.Normalise();

    CheckQuaternionNear( value, QuaternionComponents{} );
}


TEST_CASE( "Quaternion: axis-angle round trip returns to identity" )
{
    Quaternion value;
    value.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), kHalfPi );
    CheckQuaternionNear( value, QuaternionComponents{ 0.0f, 0.0f, -0.70710677f, 0.70710677f } );

    value.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), -kHalfPi );

    CheckQuaternionNear( value, QuaternionComponents{} );
}


TEST_CASE( "Quaternion: repeated multiply drift stays bounded and normalizable" )
{
    Quaternion delta;
    delta.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.01f );

    Quaternion value;
    for ( int i = 0; i < 720; ++i )
    {
        value *= delta;
    }

    CHECK( MagnitudeSquared( value ) == doctest::Approx( 1.0f ).epsilon( 0.001f ) );
    value.Normalise();
    CHECK( MagnitudeSquared( value ) == doctest::Approx( 1.0f ).epsilon( kEpsilon ) );
}


TEST_CASE( "Property invariant: quaternion normalization and matrix orthonormality [seed 0x16C0FFEE]" )
{
    SkullbonezTests::FixedSeed random( 0x16C0FFEEu );
    const Vector3 basisX( 1.0f, 0.0f, 0.0f );
    const Vector3 basisY( 0.0f, 1.0f, 0.0f );
    const Vector3 basisZ( 0.0f, 0.0f, 1.0f );

    // Invariant: normalization is idempotent, and a normalized composition
    // chain maps the three unit basis vectors to another orthonormal basis.
    for ( int sample = 0; sample < 64; ++sample )
    {
        Quaternion value;
        for ( int link = 0; link < 24; ++link )
        {
            Vector3 axis( random.Float( -1.0f, 1.0f ),
                          random.Float( -1.0f, 1.0f ),
                          random.Float( -1.0f, 1.0f ) );
            const float axisMagnitude = SkullbonezCore::Math::Vector::VectorMag( axis );
            if ( axisMagnitude <= TOLERANCE )
            {
                axis = basisX;
            }
            else
            {
                axis /= axisMagnitude;
            }
            Quaternion delta;
            delta.RotateAboutAxis( axis, random.Float( -0.35f, 0.35f ) );
            value *= delta;
        }

        value.Normalise();
        const QuaternionComponents once = ComponentsOf( value );
        value.Normalise();
        CheckQuaternionNear( value, once, 0.000002f );

        const auto matrix = value.GetOrientationMatrix();
        const Vector3 x = matrix * basisX;
        const Vector3 y = matrix * basisY;
        const Vector3 z = matrix * basisZ;
        CHECK( SkullbonezCore::Math::Vector::VectorMag( x ) == doctest::Approx( 1.0f ).epsilon( 0.00002f ) );
        CHECK( SkullbonezCore::Math::Vector::VectorMag( y ) == doctest::Approx( 1.0f ).epsilon( 0.00002f ) );
        CHECK( SkullbonezCore::Math::Vector::VectorMag( z ) == doctest::Approx( 1.0f ).epsilon( 0.00002f ) );
        CHECK( fabsf( x * y ) <= 0.00002f );
        CHECK( fabsf( x * z ) <= 0.00002f );
        CHECK( fabsf( y * z ) <= 0.00002f );
    }
}


TEST_CASE( "Quaternion: XYZ vector and scalar overloads encode the same angular displacement" )
{
    Quaternion vectorForm;
    vectorForm.RotateAboutXYZ( Vector3( 0.2f, -0.3f, 0.4f ) );
    Quaternion scalarForm;
    scalarForm.RotateAboutXYZ( 0.2f, -0.3f, 0.4f );

    CheckQuaternionNear( vectorForm, ComponentsOf( scalarForm ) );
    const Vector3 probe( 1.0f, 2.0f, -3.0f );
    const Vector3 rotated = vectorForm.GetOrientationMatrix() * probe;
    const Vector3 recovered = vectorForm.GetOrientationMatrix().TransposeMultiply( rotated );
    CHECK( recovered.x == doctest::Approx( probe.x ).epsilon( kEpsilon ) );
    CHECK( recovered.y == doctest::Approx( probe.y ).epsilon( kEpsilon ) );
    CHECK( recovered.z == doctest::Approx( probe.z ).epsilon( kEpsilon ) );
}


TEST_CASE( "Quaternion: sub-tolerance XYZ displacement is an identity-preserving no-op" )
{
    Quaternion value( 0.2f, -0.3f, 0.4f, 0.5f );
    value.Normalise();
    const QuaternionComponents before = ComponentsOf( value );

    value.RotateAboutXYZ( TOLERANCE * 0.25f, 0.0f, 0.0f );

    CheckQuaternionNear( value, before );
}


TEST_CASE( "Quaternion: orientation matrix exposes identity support extent and arbitrary rotation" )
{
    Quaternion identity;
    const auto identityMatrix = identity.GetOrientationMatrix();
    CHECK( identityMatrix.SupportExtentY( Vector3( 2.0f, 3.0f, 4.0f ) ) == doctest::Approx( 3.0f ) );

    const Vector3 rotated = SkullbonezCore::Math::Transformation::RotatePointAboutArbitrary(
        kHalfPi,
        Vector3( 0.0f, 0.0f, 1.0f ),
        Vector3( 1.0f, 0.0f, 0.0f ) );
    CHECK( rotated.x == doctest::Approx( 0.0f ).epsilon( kEpsilon ) );
    CHECK( rotated.y == doctest::Approx( -1.0f ).epsilon( kEpsilon ) );
    CHECK( rotated.z == doctest::Approx( 0.0f ).epsilon( kEpsilon ) );
}


TEST_CASE( "RotationMatrix: identity reset and legacy multiply spelling preserve a vector" )
{
    SkullbonezCore::Math::Transformation::RotationMatrix matrix( 2.0f,
                                                                  3.0f,
                                                                  4.0f,
                                                                  5.0f,
                                                                  6.0f,
                                                                  7.0f,
                                                                  8.0f,
                                                                  9.0f,
                                                                  10.0f );
    matrix.Identity();
    const Vector3 probe( -2.0f, 3.0f, 4.0f );

    const Vector3 ordinary = matrix * probe;
    const Vector3 legacy = matrix *= probe;
    CHECK( ordinary == probe );
    CHECK( legacy == probe );
}
