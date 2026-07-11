//
// File: SkullbonezTests/TestQuaternion.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Quaternion.
//
// Mental model:
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
//   - Agentic/Plans/TODO/behavioral-test-depth.md
//

#include "../ThirdPtySource/doctest/doctest.h"

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
