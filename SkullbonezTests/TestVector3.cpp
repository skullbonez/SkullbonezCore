//
// File: SkullbonezTests/TestVector3.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Vector3.
//
// Summary:
//   These tests describe engine math behavior. Plain operations retain Debug
//   misuse assertions, while Try operations make caller-reachable degeneracy
//   testable without process termination.
//
// Glossary:
//   Basis vector: Unit-length axis vector such as +X, +Y, or +Z.
//
// Invariants:
//   - Vector3::Normalise() is only called on non-zero vectors in this runner.
//   - Dot/cross/magnitude identities should stay stable across math library
//     extraction and future standalone physics builds.
//   - Defaulted copies retain all three components independently of the source.
//   - Try operations leave their input and output values untouched on failure.
//   - Surface-plane reflection preserves tangent components and reverses the
//     component parallel to a normalized surface normal.
//
// Related:
//   - SkullbonezSource/Maths/Vector3.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/Vector3.h"

#include <cmath>

using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Dot;
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


TEST_CASE( "Vector3: zero vector is detectable before plain normalise" )
{
    Vector3 zero( 0.0f, 0.0f, 0.0f );

    CHECK( zero.IsCloseToZero() );
    CHECK( VectorMagSquared( zero ) == doctest::Approx( 0.0f ) );
}


TEST_CASE( "Vector3: plain zero normalise asserts in Debug and propagates IEEE values otherwise" )
{
#if defined( _DEBUG )
    // Hazard: doctest cannot intercept the CRT assert in Normalise(). The
    // source tripwire owns interactive misuse detection; do not invoke this
    // branch in-process because an assertion dialog would stall the gate.
    CHECK( true );
#else
    Vector3 zero = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    zero.Normalise();
    CHECK_FALSE( std::isfinite( zero.x ) );
    CHECK_FALSE( std::isfinite( zero.y ) );
    CHECK_FALSE( std::isfinite( zero.z ) );
#endif
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

    CHECK( ( Dot( xAxis, yAxis ) ) == doctest::Approx( 0.0f ) );
    CHECK( ( Dot( xAxis, xAxis ) ) == doctest::Approx( 1.0f ) );
    CheckVectorNear( CrossProduct( xAxis, yAxis ), zAxis );
    CheckVectorNear( CrossProduct( yAxis, xAxis ), Vector3( 0.0f, 0.0f, -1.0f ) );
}


TEST_CASE( "Vector3: Dot preserves x then y then z evaluation through mixed-sign cancellation" )
{
    const Vector3 lhs( 1.0e10f, 1.0e10f, 3.25f );
    const Vector3 rhs( 1.0e10f, -1.0e10f, 1.0f );

    // Invariant: the x and y products cancel before z is added. Reassociating
    // x + ( y + z ) loses z at the magnitude of the y product and yields zero.
    CHECK( Dot( lhs, rhs ) == 3.25f );
    CHECK( Dot( rhs, lhs ) == 3.25f );
}


TEST_CASE( "Vector3: magnitude and squared magnitude are consistent" )
{
    const Vector3 value( -2.0f, 3.0f, 6.0f );
    const float magnitude = VectorMag( value );

    CHECK( VectorMagSquared( value ) == doctest::Approx( 49.0f ) );
    CHECK( magnitude * magnitude == doctest::Approx( VectorMagSquared( value ) ).epsilon( kEpsilon ) );
}


TEST_CASE( "Vector3: defaulted copy construction and assignment preserve components" )
{
    Vector3 source( 1.25f, -2.5f, 7.75f );
    const Vector3 constructed( source );
    Vector3 assigned( 0.0f, 0.0f, 0.0f );
    assigned = source;

    source.SetAll( 9.0f, 8.0f, 7.0f );
    CheckVectorNear( constructed, Vector3( 1.25f, -2.5f, 7.75f ) );
    CheckVectorNear( assigned, Vector3( 1.25f, -2.5f, 7.75f ) );
}


TEST_CASE( "Vector3: TryNormalise reports zero without modifying values" )
{
    Vector3 zero( 0.0f, 0.0f, 0.0f );
    Vector3 output( 8.0f, 9.0f, 10.0f );

    CHECK_FALSE( zero.TryNormalise() );
    CHECK( zero == Vector3( 0.0f, 0.0f, 0.0f ) );
    CHECK_FALSE( zero.TryNormalised( output ) );
    CHECK( output == Vector3( 8.0f, 9.0f, 10.0f ) );
}


TEST_CASE( "Vector3: TryNormalise accepts a non-zero value below the engine tolerance" )
{
    Vector3 small( TOLERANCE * 0.5f, 0.0f, 0.0f );

    REQUIRE( small.TryNormalise() );
    CHECK( small == Vector3( 1.0f, 0.0f, 0.0f ) );
}


TEST_CASE( "Vector3: Try divide APIs reject zero divisors without partial writes" )
{
    Vector3 scalarValue( 2.0f, 4.0f, 6.0f );
    Vector3 componentValue = scalarValue;
    Vector3 output( 7.0f, 8.0f, 9.0f );

    CHECK_FALSE( scalarValue.TryDivide( 0.0f ) );
    CHECK( scalarValue == Vector3( 2.0f, 4.0f, 6.0f ) );
    CHECK_FALSE( componentValue.TryDivide( Vector3( 1.0f, 0.0f, 3.0f ) ) );
    CHECK( componentValue == Vector3( 2.0f, 4.0f, 6.0f ) );
    CHECK_FALSE( scalarValue.TryDivided( 0.0f, output ) );
    CHECK( output == Vector3( 7.0f, 8.0f, 9.0f ) );
    CHECK_FALSE( scalarValue.TryDivided( Vector3( 1.0f, 2.0f, 0.0f ), output ) );
    CHECK( output == Vector3( 7.0f, 8.0f, 9.0f ) );

    REQUIRE( scalarValue.TryDivide( 2.0f ) );
    CHECK( scalarValue == Vector3( 1.0f, 2.0f, 3.0f ) );
    REQUIRE( componentValue.TryDivided( Vector3( 2.0f, 2.0f, 2.0f ), output ) );
    CHECK( output == Vector3( 1.0f, 2.0f, 3.0f ) );
}


TEST_CASE( "Vector3: tolerance boundaries are strict and Simplify uses the same interval" )
{
    Vector3 inside( TOLERANCE * 0.5f, ZERO_TAKE_TOLERANCE * 0.5f, 0.0f );
    Vector3 boundary( TOLERANCE, ZERO_TAKE_TOLERANCE, 0.0f );

    CHECK( inside.IsCloseToZero() );
    CHECK_FALSE( boundary.IsCloseToZero() );

    inside.Simplify();
    boundary.Simplify();
    CHECK( inside == Vector3( 0.0f, 0.0f, 0.0f ) );
    CHECK( boundary == Vector3( TOLERANCE, ZERO_TAKE_TOLERANCE, 0.0f ) );
}


TEST_CASE( "Vector3: surface-plane reflection preserves tangent and reverses normal components" )
{
    using SkullbonezCore::Math::Vector::VectorReflect;

    const Vector3 normal( 0.0f, 1.0f, 0.0f );
    const Vector3 obliqueIncident( 3.0f, -4.0f, 5.0f );
    const Vector3 normalIncident( 0.0f, -4.0f, 0.0f );

    const Vector3 obliqueReflected = VectorReflect( obliqueIncident, normal );
    CHECK( Dot( obliqueReflected, normal ) == doctest::Approx( -Dot( obliqueIncident, normal ) ) );
    CheckVectorNear( obliqueReflected, Vector3( 3.0f, 4.0f, 5.0f ) );

    const Vector3 normalReflected = VectorReflect( normalIncident, normal );
    CheckVectorNear( normalReflected, Vector3( 0.0f, 4.0f, 0.0f ) );
}
