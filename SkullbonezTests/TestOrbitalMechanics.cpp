//
// File: SkullbonezTests/TestOrbitalMechanics.cpp
// Purpose:
//   Verifies the allocation-free elliptic propagation and Lambert seed library.
//
// Summary:
//   Analytic circular and Hohmann cases pin units, orientation, convergence,
//   and recoverable failure behavior independently of Runtime and Physics.
//
// Glossary:
//   Circular speed: sqrt(mu / radius) for a two-body circular orbit.
//   Quarter arc: Ninety-degree circular transfer used as a Lambert oracle.
//
// Invariants:
//   - Tests use the solar scene's XZ plane and -Y prograde angular momentum.
//   - Every failure case verifies finite zero output rather than NaN leakage.
//   - Tolerances cover single-precision iteration without hiding wrong units.
//
// Related:
//   - SkullbonezSource/Maths/OrbitalMechanics.h
//   - Agentic/Plans/TODO/solar-system-trajectory-planner.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/OrbitalMechanics.h"

#include <array>
#include <cmath>

using SkullbonezCore::Math::Orbital::ElementsFromState;
using SkullbonezCore::Math::Orbital::LambertSolution;
using SkullbonezCore::Math::Orbital::OrbitalElements;
using SkullbonezCore::Math::Orbital::OrbitalStatus;
using SkullbonezCore::Math::Orbital::PropagateToTime;
using SkullbonezCore::Math::Orbital::SampleOrbitPolyline;
using SkullbonezCore::Math::Orbital::SolveLambert;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr float MU = 40000.0f;
constexpr float EARTH_RADIUS = 80.0f;

float HohmannTransferSeconds( float r1, float r2, float mu )
{
    if ( r1 <= 0.0f || r2 <= 0.0f || mu <= 0.0f )
    {
        return 0.0f;
    }
    const float semiMajorAxis = 0.5f * ( r1 + r2 );
    return PI * std::sqrt( semiMajorAxis * semiMajorAxis * semiMajorAxis / mu );
}


float HohmannDepartureDeltaV( float r1, float r2, float mu )
{
    if ( r1 <= 0.0f || r2 <= 0.0f || mu <= 0.0f )
    {
        return 0.0f;
    }
    const float circularSpeed = std::sqrt( mu / r1 );
    const float transferSpeed = circularSpeed * std::sqrt( 2.0f * r2 / ( r1 + r2 ) );
    return transferSpeed - circularSpeed;
}


void CheckVectorNear( const Vector3& value, const Vector3& expected, float epsilon = 0.0005f )
{
    CHECK( value.x == doctest::Approx( expected.x ).epsilon( epsilon ) );
    CHECK( value.y == doctest::Approx( expected.y ).epsilon( epsilon ) );
    CHECK( value.z == doctest::Approx( expected.z ).epsilon( epsilon ) );
}

bool IsFinite( const Vector3& value )
{
    return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
}
} // namespace


TEST_CASE( "Orbital mechanics: element round-trip preserves the epoch state" )
{
    const float circularSpeed = std::sqrt( MU / EARTH_RADIUS );
    const Vector3 position( EARTH_RADIUS, 0.0f, 0.0f );
    const Vector3 velocity( 0.0f, 0.0f, circularSpeed );
    OrbitalElements elements;
    REQUIRE( ElementsFromState( position, velocity, MU, elements ) == OrbitalStatus::Ok );

    Vector3 propagatedPosition( 0.0f, 0.0f, 0.0f );
    Vector3 propagatedVelocity( 0.0f, 0.0f, 0.0f );
    REQUIRE( PropagateToTime( elements, 0.0f, propagatedPosition, propagatedVelocity ) == OrbitalStatus::Ok );
    CheckVectorNear( propagatedPosition, position );
    CheckVectorNear( propagatedVelocity, velocity );
}


TEST_CASE( "Orbital mechanics: one circular period returns to the start" )
{
    const float circularSpeed = std::sqrt( MU / EARTH_RADIUS );
    const float period = 2.0f * PI * std::sqrt( EARTH_RADIUS * EARTH_RADIUS * EARTH_RADIUS / MU );
    OrbitalElements elements;
    REQUIRE(
        ElementsFromState( Vector3( EARTH_RADIUS, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, circularSpeed ), MU, elements ) ==
        OrbitalStatus::Ok );

    Vector3 position( 0.0f, 0.0f, 0.0f );
    Vector3 velocity( 0.0f, 0.0f, 0.0f );
    REQUIRE( PropagateToTime( elements, period, position, velocity ) == OrbitalStatus::Ok );
    CheckVectorNear( position, Vector3( EARTH_RADIUS, 0.0f, 0.0f ), 0.002f );
    CheckVectorNear( velocity, Vector3( 0.0f, 0.0f, circularSpeed ), 0.002f );

    std::array<Vector3, 16> points;
    CHECK( SampleOrbitPolyline( elements, points ) == points.size() );
    CheckVectorNear( points.front(), Vector3( EARTH_RADIUS, 0.0f, 0.0f ) );
}


TEST_CASE( "Orbital mechanics: Lambert quarter arc recovers circular velocity" )
{
    const float circularSpeed = std::sqrt( MU / EARTH_RADIUS );
    const float quarterPeriod = 0.5f * PI * std::sqrt( EARTH_RADIUS * EARTH_RADIUS * EARTH_RADIUS / MU );
    LambertSolution solution;
    REQUIRE( SolveLambert( Vector3( EARTH_RADIUS, 0.0f, 0.0f ),
                           Vector3( 0.0f, 0.0f, EARTH_RADIUS ),
                           quarterPeriod,
                           MU,
                           true,
                           solution ) == OrbitalStatus::Ok );
    CheckVectorNear( solution.v1, Vector3( 0.0f, 0.0f, circularSpeed ), 0.003f );
    CheckVectorNear( solution.v2, Vector3( -circularSpeed, 0.0f, 0.0f ), 0.003f );
}


TEST_CASE( "Orbital mechanics: Hohmann helpers match the solar design table" )
{
    constexpr float marsRadius = 121.6f;
    CHECK( HohmannTransferSeconds( EARTH_RADIUS, marsRadius, MU ) == doctest::Approx( 15.90f ).epsilon( 0.002f ) );
    CHECK( HohmannDepartureDeltaV( EARTH_RADIUS, marsRadius, MU ) == doctest::Approx( 2.20f ).epsilon( 0.02f ) );

    const float transferTime = HohmannTransferSeconds( EARTH_RADIUS, marsRadius, MU );
    const float arrivalAngle = PI - 0.02f;
    LambertSolution seed;
    REQUIRE(
        SolveLambert( Vector3( EARTH_RADIUS, 0.0f, 0.0f ),
                      Vector3( marsRadius * std::cos( arrivalAngle ), 0.0f, marsRadius * std::sin( arrivalAngle ) ),
                      transferTime,
                      MU,
                      true,
                      seed ) == OrbitalStatus::Ok );
    CHECK( seed.v1.z - std::sqrt( MU / EARTH_RADIUS ) ==
           doctest::Approx( HohmannDepartureDeltaV( EARTH_RADIUS, marsRadius, MU ) ).epsilon( 0.08f ) );
}


TEST_CASE( "Orbital mechanics: solar launch phase predicts the first Mars intercept window" )
{
    constexpr float marsRadius = 121.6f;
    const float marsPhase = 44.1f * PI / 180.0f;
    const float marsSpeed = std::sqrt( MU / marsRadius );
    OrbitalElements marsOrbit;
    REQUIRE( ElementsFromState( Vector3( marsRadius * std::cos( marsPhase ), 0.0f, marsRadius * std::sin( marsPhase ) ),
                                Vector3( -marsSpeed * std::sin( marsPhase ), 0.0f, marsSpeed * std::cos( marsPhase ) ),
                                MU,
                                marsOrbit ) == OrbitalStatus::Ok );

    const float transferSeconds = HohmannTransferSeconds( EARTH_RADIUS, marsRadius, MU );
    Vector3 marsAtArrival( 0.0f, 0.0f, 0.0f );
    Vector3 marsVelocityAtArrival( 0.0f, 0.0f, 0.0f );
    REQUIRE( PropagateToTime( marsOrbit, transferSeconds, marsAtArrival, marsVelocityAtArrival ) == OrbitalStatus::Ok );

    // Why: an ideal Hohmann departure from Earth's orbital radius reaches the
    // opposite side at this time. The authored 44.1-degree Mars lead places its
    // centre inside the ship-plus-Mars collision radius at that arrival.
    const Vector3 idealTransferArrival( -marsRadius, 0.0f, 0.0f );
    const Vector3 miss = marsAtArrival - idealTransferArrival;
    CHECK( std::sqrt( Dot( miss, miss ) ) < 3.2f );
    CHECK( HohmannDepartureDeltaV( EARTH_RADIUS, marsRadius, MU ) == doctest::Approx( 2.1989f ).epsilon( 0.001f ) );
}


TEST_CASE( "Orbital mechanics: invalid inputs fail without NaN output" )
{
    OrbitalElements elements;
    CHECK( ElementsFromState( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), MU, elements ) ==
           OrbitalStatus::Degenerate );
    CHECK( ElementsFromState( Vector3( EARTH_RADIUS, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 100.0f ), MU, elements ) ==
           OrbitalStatus::NotElliptic );

    elements = {};
    elements.semiMajorAxis = EARTH_RADIUS;
    elements.eccentricity = 1.1f;
    elements.mu = MU;
    Vector3 position( 9.0f, 9.0f, 9.0f );
    Vector3 velocity( 9.0f, 9.0f, 9.0f );
    CHECK( PropagateToTime( elements, 1.0f, position, velocity ) == OrbitalStatus::NotElliptic );
    CHECK( IsFinite( position ) );
    CHECK( IsFinite( velocity ) );

    LambertSolution solution;
    CHECK( SolveLambert( Vector3( EARTH_RADIUS, 0.0f, 0.0f ),
                         Vector3( 0.0f, 0.0f, EARTH_RADIUS ),
                         0.0f,
                         MU,
                         true,
                         solution ) == OrbitalStatus::Degenerate );
    CHECK( SolveLambert( Vector3( EARTH_RADIUS, 0.0f, 0.0f ),
                         Vector3( -EARTH_RADIUS, 0.0f, 0.0f ),
                         10.0f,
                         MU,
                         true,
                         solution ) == OrbitalStatus::Degenerate );
    CHECK( IsFinite( solution.v1 ) );
    CHECK( IsFinite( solution.v2 ) );
}
