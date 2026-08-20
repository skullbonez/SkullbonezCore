//
// File: SkullbonezTests/TestQuaternion.cpp
// Purpose:
//   Lock the first pure-math unit contracts for Quaternion.
//
// Summary:
//   Quaternion behavior is a small but engine-specific contract: component
//   signs, multiplication order, and normalization all feed physics and camera
//   orientation. These tests pin that public contract for every current caller.
//
// Glossary:
//   Identity quaternion: No-rotation value (0,0,0,1).
//   Axis-angle rotation: Rotation described by one axis vector and one angle.
//   Drift: Floating-point length error accumulated by repeated multiplication.
//
// Invariants:
//   - Normalise() resets a zero quaternion to identity.
//   - RotateAboutAxis() stores the standard positive-sine axis-angle value.
//   - RotateAboutAxis() treats a non-unit axis as Debug misuse while Profile
//     keeps the defined legacy normalization result pinned as a negative control.
//   - Golden cases pin Hamilton operand order and the active-rotation matrix
//     exposed by the public header.
//   - Characterization cases pin world-space outcomes without inspecting stored
//     quaternion components, so representation migrations must preserve them.
//   - Orientation-matrix conversion accepts an immutable quaternion.
//   - Repeated multiplication must stay close enough to unit length for a later
//     Normalise() pass to repair it deterministically.
//
// Related:
//   - SkullbonezSource/Maths/Quaternion.h
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Maths/Quaternion.h"
#include "../SkullbonezSource/Physics/ContactSolverCommon.h"

#include <bit>
#include <cmath>
#include <cstdint>

using SkullbonezCore::Math::Orientation::ConjugateQuaternionVectorPart;
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

void CheckVectorNear( const Vector3& value, const Vector3& expected, float epsilon = kEpsilon )
{
    CHECK( value.x == doctest::Approx( expected.x ).epsilon( epsilon ) );
    CHECK( value.y == doctest::Approx( expected.y ).epsilon( epsilon ) );
    CHECK( value.z == doctest::Approx( expected.z ).epsilon( epsilon ) );
}
} // namespace


TEST_CASE( "Quaternion migration: double conjugation is bitwise lossless" )
{
    float x = std::bit_cast<float>( uint32_t { 0x80000000u } );
    float y = std::bit_cast<float>( uint32_t { 0x00000001u } );
    float z = -123.5f;
    const uint32_t originalX = std::bit_cast<uint32_t>( x );
    const uint32_t originalY = std::bit_cast<uint32_t>( y );
    const uint32_t originalZ = std::bit_cast<uint32_t>( z );

    ConjugateQuaternionVectorPart( x, y, z );
    ConjugateQuaternionVectorPart( x, y, z );

    CHECK( std::bit_cast<uint32_t>( x ) == originalX );
    CHECK( std::bit_cast<uint32_t>( y ) == originalY );
    CHECK( std::bit_cast<uint32_t>( z ) == originalZ );
}


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

    CheckQuaternionNear( value, QuaternionComponents {} );
}


TEST_CASE( "Quaternion: axis-angle round trip returns to identity" )
{
    Quaternion value;
    value.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), kHalfPi );
    CheckQuaternionNear( value, QuaternionComponents { 0.0f, 0.0f, 0.70710677f, 0.70710677f } );

    value.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), -kHalfPi );

    CheckQuaternionNear( value, QuaternionComponents {} );
}


TEST_CASE( "Quaternion: non-unit axis remains a Debug tripwire with defined Release math" )
{
#if defined( _DEBUG )
    // Hazard: doctest cannot intercept the CRT assertion dialog. The Profile
    // branch below is the negative control for the shipped arithmetic, while
    // the Debug source assertion owns interactive misuse detection.
    CHECK( true );
#else
    Quaternion unitAxis;
    Quaternion scaledAxis;
    unitAxis.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), kHalfPi );
    scaledAxis.RotateAboutAxis( Vector3( 0.0f, 0.0f, 2.0f ), kHalfPi );

    const QuaternionComponents unit = ComponentsOf( unitAxis );
    const QuaternionComponents scaled = ComponentsOf( scaledAxis );
    CHECK( scaled.z != doctest::Approx( unit.z ) );
    CHECK( scaled.w != doctest::Approx( unit.w ) );
    CHECK( MagnitudeSquared( scaledAxis ) == doctest::Approx( 1.0f ).epsilon( kEpsilon ) );
#endif
}


TEST_CASE( "Quaternion: multiplication evaluates the Hamilton left operand times the right operand" )
{
    const Quaternion left( 1.0f, 2.0f, 3.0f, 4.0f );
    const Quaternion right( -2.0f, 5.0f, 7.0f, -3.0f );

    // Concept: asymmetric components distinguish Hamilton(left * right) from
    // the retired reversed-operand implementation.
    CheckQuaternionNear( left * right, QuaternionComponents { -12.0f, 1.0f, 28.0f, -41.0f } );
}


TEST_CASE( "Quaternion: orientation matrix exposes the Hamilton active rotation" )
{
    constexpr float HALF_ROOT_TWO = 0.70710677f;
    const Quaternion positiveZQuarterTurn( 0.0f, 0.0f, HALF_ROOT_TWO, HALF_ROOT_TWO );

    // Invariant: the public matrix maps +X to +Y for this positive Hamilton Z
    // quarter-turn.
    const Vector3 rotated = positiveZQuarterTurn.GetOrientationMatrix() * Vector3( 1.0f, 0.0f, 0.0f );
    CHECK( rotated.x == doctest::Approx( 0.0f ).epsilon( kEpsilon ) );
    CHECK( rotated.y == doctest::Approx( 1.0f ).epsilon( kEpsilon ) );
    CHECK( rotated.z == doctest::Approx( 0.0f ).epsilon( kEpsilon ) );
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
            Vector3 axis( random.Float( -1.0f, 1.0f ), random.Float( -1.0f, 1.0f ), random.Float( -1.0f, 1.0f ) );
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
        CHECK( fabsf( Dot( x, y ) ) <= 0.00002f );
        CHECK( fabsf( Dot( x, z ) ) <= 0.00002f );
        CHECK( fabsf( Dot( y, z ) ) <= 0.00002f );
    }
}


TEST_CASE( "Quaternion: orientation matrix exposes identity support extent and arbitrary rotation" )
{
    const Quaternion identity;
    const auto identityMatrix = identity.GetOrientationMatrix();
    CHECK( identityMatrix.SupportExtentY( Vector3( 2.0f, 3.0f, 4.0f ) ) == doctest::Approx( 3.0f ) );

    const Vector3 rotated = SkullbonezCore::Math::Transformation::RotatePointAboutArbitrary( kHalfPi,
                                                                                             Vector3( 0.0f, 0.0f, 1.0f ),
                                                                                             Vector3( 1.0f, 0.0f, 0.0f ) );
    CHECK( rotated.x == doctest::Approx( 0.0f ).epsilon( kEpsilon ) );
    CHECK( rotated.y == doctest::Approx( -1.0f ).epsilon( kEpsilon ) );
    CHECK( rotated.z == doctest::Approx( 0.0f ).epsilon( kEpsilon ) );
}


TEST_CASE( "Quaternion behavior: composed world rotations retain their call order" )
{
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 1.0f, 0.0f, 0.0f ), kHalfPi );
    orientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), kHalfPi );

    // Invariant: the public incremental-rotation API applies the X turn first
    // and the Y turn second, independent of the stored quaternion convention.
    CheckVectorNear( orientation.GetOrientationMatrix() * Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, -1.0f ) );
}


TEST_CASE( "Quaternion behavior: orientation matrix applies a positive world rotation" )
{
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), kHalfPi );

    CheckVectorNear( orientation.GetOrientationMatrix() * Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ) );
}


TEST_CASE( "Quaternion behavior: world inverse inertia preserves rotated principal axes" )
{
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), kHalfPi );
    const auto rotation = orientation.GetOrientationMatrix();
    const Vector3 worldImpulse( 1.0f, 0.0f, 0.0f );
    const Vector3 localImpulse = rotation.TransposeMultiply( worldImpulse );
    const Vector3 localResponse( localImpulse.x, localImpulse.y * 2.0f, localImpulse.z * 3.0f );

    // This is the same R * I^-1 * R^T mapping used by the body store and
    // persistent contact solver. The assertion names only the physical result.
    CheckVectorNear( rotation * localResponse, Vector3( 2.0f, 0.0f, 0.0f ) );
}


TEST_CASE( "Quaternion behavior: contact tangent basis remains deterministic" )
{
    Vector3 tangent1;
    Vector3 tangent2;
    SkullbonezCore::Physics::ContactSolver::BuildContactTangents( Vector3( 0.0f, 1.0f, 0.0f ), tangent1, tangent2 );

    CheckVectorNear( tangent1, Vector3( 1.0f, 0.0f, 0.0f ) );
    CheckVectorNear( tangent2, Vector3( 0.0f, 0.0f, -1.0f ) );
}


TEST_CASE( "Quaternion behavior: attached-camera basis round trips target-local vectors" )
{
    Quaternion orientation;
    orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), kHalfPi );
    const auto targetRotation = orientation.GetOrientationMatrix();
    const Vector3 localRight( 1.0f, 0.0f, 0.0f );
    const Vector3 worldRight = targetRotation * localRight;

    CheckVectorNear( worldRight, Vector3( 0.0f, 1.0f, 0.0f ) );
    CheckVectorNear( targetRotation.TransposeMultiply( worldRight ), localRight );
}
