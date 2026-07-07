//
// File: SkullbonezTests/TestVector3.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Vector3.
//
// Mental model:
//   These tests describe the current engine math behavior, including legacy
//   exception contracts, before later error-handling and library layering plans
//   start changing mechanics around the math code.
//
// Glossary:
//   Legacy throwing contract: Current behavior that reports invalid math input
//     with std::runtime_error until fable-05 conversion reaches this lane.
//   Basis vector: Unit-length axis vector such as +X, +Y, or +Z.
//
// Invariants:
//   - Vector3::Normalise() currently throws for an exact zero vector.
//   - Dot/cross/magnitude identities should stay stable across math library
//     extraction and future standalone physics builds.
//
// Related:
//   - SkullbonezSource/Maths/Vector3.h
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/Vector3.h"

#include <stdexcept>

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


TEST_CASE( "Vector3: zero normalise keeps the legacy throwing contract" )
{
    Vector3 zero( 0.0f, 0.0f, 0.0f );

    CHECK_THROWS_AS( zero.Normalise(), std::runtime_error );
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
