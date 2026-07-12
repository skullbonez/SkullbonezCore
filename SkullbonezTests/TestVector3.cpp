//
// File: SkullbonezTests/TestVector3.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Vector3.
//
// Summary:
//   These tests describe engine math behavior. Fatal-only preconditions are
//   source-documented and tested through caller-detectable guard states rather
//   than in-process fatal assertions.
//
// Glossary:
//   Basis vector: Unit-length axis vector such as +X, +Y, or +Z.
//
// Invariants:
//   - Vector3::Normalise() is only called on non-zero vectors in this runner.
//   - Dot/cross/magnitude identities should stay stable across math library
//     extraction and future standalone physics builds.
//
// Related:
//   - SkullbonezSource/Maths/Vector3.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/Vector3.h"

using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMag;
using SkullbonezCore::Math::Vector::VectorMagSquared;

namespace
{
constexpr float kEpsilon = 0.00001f;

void CheckVectorNear( const Vector3& value, const Vector3& expected )
{
    CHECK( value.x == doctest::Approx( expected.x ).epsilon( kEpsilon ) );
    CHECK( value.y == doctest::Approx( expected.y ).epsilon( kEpsilon ) );
    CHECK( value.z == doctest::Approx( expected.z ).epsilon( kEpsilon ) );
}
} // namespace


TEST_CASE( "Vector3: zero vector is detectable before fatal-only normalise" )
{
    Vector3 zero( 0.0f, 0.0f, 0.0f );

    CHECK( zero.IsCloseToZero() );
    CHECK( VectorMagSquared( zero ) == doctest::Approx( 0.0f ) );
}


TEST_CASE( "Vector3: normalise converts a non-zero vector to unit length" )
{
    Vector3 value( 3.0f, 4.0f, 0.0f );

    value.Normalise();

    CheckVectorNear( value, Vector3( 0.6f, 0.8f, 0.0f ) );
    CHECK( VectorMag( value ) == doctest::Approx( 1.0f ).epsilon( kEpsilon ) );
}


TEST_CASE( "Vector3: dot and cross product identities hold for basis vectors" )
{
    const Vector3 xAxis( 1.0f, 0.0f, 0.0f );
    const Vector3 yAxis( 0.0f, 1.0f, 0.0f );
    const Vector3 zAxis( 0.0f, 0.0f, 1.0f );

    CHECK( ( xAxis * yAxis ) == doctest::Approx( 0.0f ) );
    CHECK( ( xAxis * xAxis ) == doctest::Approx( 1.0f ) );
    CheckVectorNear( CrossProduct( xAxis, yAxis ), zAxis );
    CheckVectorNear( CrossProduct( yAxis, xAxis ), Vector3( 0.0f, 0.0f, -1.0f ) );
}


TEST_CASE( "Vector3: magnitude and squared magnitude are consistent" )
{
    const Vector3 value( -2.0f, 3.0f, 6.0f );
    const float magnitude = VectorMag( value );

    CHECK( VectorMagSquared( value ) == doctest::Approx( 49.0f ) );
    CHECK( magnitude * magnitude == doctest::Approx( VectorMagSquared( value ) ).epsilon( kEpsilon ) );
}
