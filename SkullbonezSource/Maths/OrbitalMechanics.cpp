/*
File: SkullbonezSource/Maths/OrbitalMechanics.cpp
Purpose:
  Implements deterministic, allocation-free orbital-mechanics value math.

Summary:
  Classical two-body formulas provide element conversion and Kepler
  propagation. A universal-variable Lambert solve supplies a bounded
  single-revolution seed for higher-level planners while keeping every
  caller-reachable numeric failure in recoverable status lane R.

Glossary:
  Perifocal frame: Orbit-local XY plane whose +X axis points to periapsis.
  Stumpff functions: Stable universal-variable functions C(z) and S(z).
  Transfer angle: Directed angle from Lambert departure position to arrival.
  Safeguarded Newton: Newton iteration constrained to a known sign-changing
    bracket, with bisection fallback.

Invariants:
  - Kepler iteration is capped at 16 turns; Lambert iteration at 48 turns.
  - Trigonometric inputs are clamped before inverse functions.
  - No function stores a span, allocates, throws, or mutates engine state.
  - The engine's XZ solar-system convention treats -Y angular momentum as
    prograde; XY callers retain the conventional +Z fallback.

Related:
  - SkullbonezSource/Maths/OrbitalMechanics.h
  - SkullbonezTests/TestOrbitalMechanics.cpp
  - Agentic/Reports/2026-07-24/solar-system-trajectory-planner-closure.md
*/
#include "OrbitalMechanics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SkullbonezCore
{
namespace Math
{
namespace Orbital
{
namespace
{
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float NUMERIC_EPSILON = 1.0e-6f;
constexpr float GEOMETRY_EPSILON = 1.0e-5f;
constexpr int KEPLER_ITERATION_CAP = 16;
constexpr int LAMBERT_ITERATION_CAP = 48;

using Vector::CrossProduct;
using Vector::Vector3;

float Dot( const Vector3& a, const Vector3& b )
{
    return a * b;
}

float MagnitudeSquared( const Vector3& value )
{
    return Dot( value, value );
}

float Magnitude( const Vector3& value )
{
    return std::sqrt( MagnitudeSquared( value ) );
}

bool IsFinite( const Vector3& value )
{
    return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
}

float ClampUnit( float value )
{
    return std::clamp( value, -1.0f, 1.0f );
}

float WrapRadians( float value )
{
    value = std::fmod( value, TWO_PI );

    if ( value > PI )
    {
        value -= TWO_PI;
    }
    else if ( value < -PI )
    {
        value += TWO_PI;
    }

    return value;
}

float SignedAngle( const Vector3& from, const Vector3& to, const Vector3& unitNormal )
{
    return std::atan2( Dot( CrossProduct( from, to ), unitNormal ), Dot( from, to ) );
}

Vector3 RotatePerifocal( const OrbitalElements& elements, float x, float y )
{
    const float cosAscending = std::cos( elements.longitudeAscendingNode );
    const float sinAscending = std::sin( elements.longitudeAscendingNode );
    const float cosArgument = std::cos( elements.argumentPeriapsis );
    const float sinArgument = std::sin( elements.argumentPeriapsis );
    const float cosInclination = std::cos( elements.inclination );
    const float sinInclination = std::sin( elements.inclination );

    const float basisXX = cosAscending * cosArgument - sinAscending * sinArgument * cosInclination;
    const float basisXY = -cosAscending * sinArgument - sinAscending * cosArgument * cosInclination;
    const float basisYX = sinAscending * cosArgument + cosAscending * sinArgument * cosInclination;
    const float basisYY = -sinAscending * sinArgument + cosAscending * cosArgument * cosInclination;
    const float basisZX = sinArgument * sinInclination;
    const float basisZY = cosArgument * sinInclination;
    return Vector3( basisXX * x + basisXY * y, basisYX * x + basisYY * y, basisZX * x + basisZY * y );
}

bool ValidElements( const OrbitalElements& elements )
{
    return std::isfinite( elements.semiMajorAxis ) && std::isfinite( elements.eccentricity ) &&
           std::isfinite( elements.inclination ) && std::isfinite( elements.longitudeAscendingNode ) &&
           std::isfinite( elements.argumentPeriapsis ) && std::isfinite( elements.meanAnomalyAtEpoch ) &&
           std::isfinite( elements.mu ) && elements.semiMajorAxis > GEOMETRY_EPSILON && elements.mu > 0.0f &&
           elements.eccentricity >= 0.0f && elements.eccentricity < 1.0f;
}

OrbitalStatus SolveEccentricAnomaly( float meanAnomaly, float eccentricity, float& outEccentricAnomaly )
{
    float eccentricAnomaly = meanAnomaly + eccentricity * std::sin( meanAnomaly );

    for ( int iteration = 0; iteration < KEPLER_ITERATION_CAP; ++iteration )
    {
        const float residual = eccentricAnomaly - eccentricity * std::sin( eccentricAnomaly ) - meanAnomaly;
        const float derivative = 1.0f - eccentricity * std::cos( eccentricAnomaly );

        if ( std::fabs( derivative ) <= NUMERIC_EPSILON )
        {
            return OrbitalStatus::NotConverged;
        }

        const float correction = residual / derivative;
        eccentricAnomaly -= correction;

        if ( std::fabs( correction ) <= NUMERIC_EPSILON )
        {
            outEccentricAnomaly = eccentricAnomaly;
            return OrbitalStatus::Ok;
        }
    }

    return OrbitalStatus::NotConverged;
}

void Stumpff( double z, double& outC, double& outS )
{

    if ( std::fabs( z ) < 1.0e-8 )
    {
        outC = 0.5;
        outS = 1.0 / 6.0;
        return;
    }

    if ( z > 0.0 )
    {
        const double root = std::sqrt( z );
        outC = ( 1.0 - std::cos( root ) ) / z;
        outS = ( root - std::sin( root ) ) / ( root * root * root );
        return;
    }

    const double root = std::sqrt( -z );
    outC = ( std::cosh( root ) - 1.0 ) / ( -z );
    outS = ( std::sinh( root ) - root ) / ( root * root * root );
}

bool LambertTimeResidual( double z, double radius1, double radius2, double transferA, double targetTimeScaled,
                          double& outResidual, double& outY )
{
    double c = 0.0;
    double s = 0.0;
    Stumpff( z, c, s );

    if ( c <= 0.0 || !std::isfinite( c ) || !std::isfinite( s ) )
    {
        return false;
    }

    const double sqrtC = std::sqrt( c );
    const double y = radius1 + radius2 + transferA * ( z * s - 1.0 ) / sqrtC;

    if ( y <= 0.0 || !std::isfinite( y ) )
    {
        return false;
    }

    const double x = std::sqrt( y / c );
    const double residual = x * x * x * s + transferA * std::sqrt( y ) - targetTimeScaled;

    if ( !std::isfinite( residual ) )
    {
        return false;
    }

    outResidual = residual;
    outY = y;
    return true;
}
} // namespace


OrbitalStatus ElementsFromState( const Vector3& relativePosition, const Vector3& relativeVelocity, float mu,
                                 OrbitalElements& out )
{
    out = {};

    if ( mu <= 0.0f || !std::isfinite( mu ) || !IsFinite( relativePosition ) || !IsFinite( relativeVelocity ) )
    {
        return OrbitalStatus::Degenerate;
    }

    const float radius = Magnitude( relativePosition );

    if ( radius <= GEOMETRY_EPSILON )
    {
        return OrbitalStatus::Degenerate;
    }

    const Vector3 angularMomentum = CrossProduct( relativePosition, relativeVelocity );
    const float angularMomentumMagnitude = Magnitude( angularMomentum );

    if ( angularMomentumMagnitude <= GEOMETRY_EPSILON )
    {
        return OrbitalStatus::Degenerate;
    }

    const float speedSquared = MagnitudeSquared( relativeVelocity );
    const float specificEnergy = 0.5f * speedSquared - mu / radius;

    if ( specificEnergy >= -NUMERIC_EPSILON )
    {
        return OrbitalStatus::NotElliptic;
    }

    const Vector3 eccentricityVector = CrossProduct( relativeVelocity, angularMomentum ) * ( 1.0f / mu ) -
                                       relativePosition * ( 1.0f / radius );

    const float eccentricity = Magnitude( eccentricityVector );

    if ( !std::isfinite( eccentricity ) || eccentricity >= 1.0f )
    {
        return OrbitalStatus::NotElliptic;
    }

    const float semiMajorAxis = -mu / ( 2.0f * specificEnergy );

    if ( semiMajorAxis <= GEOMETRY_EPSILON || !std::isfinite( semiMajorAxis ) )
    {
        return OrbitalStatus::NotElliptic;
    }

    const Vector3 unitAngularMomentum = angularMomentum * ( 1.0f / angularMomentumMagnitude );
    const Vector3 node( -angularMomentum.y, angularMomentum.x, 0.0f );
    const float nodeMagnitude = Magnitude( node );
    float longitudeAscendingNode = 0.0f;

    if ( nodeMagnitude > GEOMETRY_EPSILON )
    {
        longitudeAscendingNode = std::atan2( node.y, node.x );
    }

    float argumentPeriapsis = 0.0f;
    float trueAnomaly = 0.0f;

    if ( eccentricity > GEOMETRY_EPSILON )
    {
        const Vector3 unitEccentricity = eccentricityVector * ( 1.0f / eccentricity );

        if ( nodeMagnitude > GEOMETRY_EPSILON )
        {
            argumentPeriapsis = SignedAngle( node * ( 1.0f / nodeMagnitude ), unitEccentricity, unitAngularMomentum );
        }
        else
        {
            argumentPeriapsis = std::atan2( eccentricityVector.y, eccentricityVector.x );
        }

        trueAnomaly = SignedAngle( unitEccentricity, relativePosition * ( 1.0f / radius ), unitAngularMomentum );
    }
    else if ( nodeMagnitude > GEOMETRY_EPSILON )
    {
        trueAnomaly = SignedAngle( node * ( 1.0f / nodeMagnitude ), relativePosition * ( 1.0f / radius ),
                                   unitAngularMomentum );
    }
    else
    {
        trueAnomaly = std::atan2( relativePosition.y, relativePosition.x );
    }

    const float eccentricAnomaly = 2.0f * std::atan2( std::sqrt( 1.0f - eccentricity ) * std::sin( 0.5f * trueAnomaly ),
                                                      std::sqrt( 1.0f + eccentricity ) * std::cos( 0.5f * trueAnomaly ) );

    out.semiMajorAxis = semiMajorAxis;
    out.eccentricity = eccentricity;
    out.inclination = std::acos( ClampUnit( angularMomentum.z / angularMomentumMagnitude ) );
    out.longitudeAscendingNode = WrapRadians( longitudeAscendingNode );
    out.argumentPeriapsis = WrapRadians( argumentPeriapsis );
    out.meanAnomalyAtEpoch = WrapRadians( eccentricAnomaly - eccentricity * std::sin( eccentricAnomaly ) );
    out.mu = mu;
    return OrbitalStatus::Ok;
}


OrbitalStatus PropagateToTime( const OrbitalElements& elements, float deltaSeconds, Vector3& outRelativePosition,
                               Vector3& outRelativeVelocity )
{
    outRelativePosition = Vector3( 0.0f, 0.0f, 0.0f );
    outRelativeVelocity = Vector3( 0.0f, 0.0f, 0.0f );

    if ( !ValidElements( elements ) || !std::isfinite( deltaSeconds ) )
    {
        return elements.eccentricity >= 1.0f ? OrbitalStatus::NotElliptic : OrbitalStatus::Degenerate;
    }

    const float meanMotion = std::sqrt( elements.mu /
                                        ( elements.semiMajorAxis * elements.semiMajorAxis * elements.semiMajorAxis ) );

    const float meanAnomaly = WrapRadians( elements.meanAnomalyAtEpoch + meanMotion * deltaSeconds );
    float eccentricAnomaly = 0.0f;
    const OrbitalStatus solveStatus = SolveEccentricAnomaly( meanAnomaly, elements.eccentricity, eccentricAnomaly );

    if ( solveStatus != OrbitalStatus::Ok )
    {
        return solveStatus;
    }

    const float cosEccentric = std::cos( eccentricAnomaly );
    const float sinEccentric = std::sin( eccentricAnomaly );
    const float ellipseMinorScale = std::sqrt( 1.0f - elements.eccentricity * elements.eccentricity );
    const float denominator = 1.0f - elements.eccentricity * cosEccentric;

    if ( denominator <= NUMERIC_EPSILON )
    {
        return OrbitalStatus::Degenerate;
    }

    const float x = elements.semiMajorAxis * ( cosEccentric - elements.eccentricity );
    const float y = elements.semiMajorAxis * ellipseMinorScale * sinEccentric;
    const float velocityScale = elements.semiMajorAxis * meanMotion / denominator;
    const float velocityX = -velocityScale * sinEccentric;
    const float velocityY = velocityScale * ellipseMinorScale * cosEccentric;
    outRelativePosition = RotatePerifocal( elements, x, y );
    outRelativeVelocity = RotatePerifocal( elements, velocityX, velocityY );
    return IsFinite( outRelativePosition ) && IsFinite( outRelativeVelocity ) ? OrbitalStatus::Ok
                                                                              : OrbitalStatus::NotConverged;
}


std::size_t SampleOrbitPolyline( const OrbitalElements& elements, std::span<Vector3> outPoints )
{

    if ( !ValidElements( elements ) || outPoints.empty() )
    {
        return 0;
    }

    const float ellipseMinorScale = std::sqrt( 1.0f - elements.eccentricity * elements.eccentricity );
    const float inverseCount = 1.0f / static_cast<float>( outPoints.size() );

    for ( std::size_t index = 0; index < outPoints.size(); ++index )
    {
        const float eccentricAnomaly = TWO_PI * static_cast<float>( index ) * inverseCount;
        const float x = elements.semiMajorAxis * ( std::cos( eccentricAnomaly ) - elements.eccentricity );
        const float y = elements.semiMajorAxis * ellipseMinorScale * std::sin( eccentricAnomaly );
        outPoints[index] = RotatePerifocal( elements, x, y );
    }

    return outPoints.size();
}


OrbitalStatus SolveLambert( const Vector3& r1, const Vector3& r2, float timeOfFlight, float mu, bool prograde,
                            LambertSolution& out )
{
    out = {};

    if ( timeOfFlight <= 0.0f || mu <= 0.0f || !std::isfinite( timeOfFlight ) || !std::isfinite( mu ) || !IsFinite( r1 ) ||
         !IsFinite( r2 ) )
    {
        return OrbitalStatus::Degenerate;
    }

    const double radius1 = static_cast<double>( Magnitude( r1 ) );
    const double radius2 = static_cast<double>( Magnitude( r2 ) );

    if ( radius1 <= GEOMETRY_EPSILON || radius2 <= GEOMETRY_EPSILON )
    {
        return OrbitalStatus::Degenerate;
    }

    const Vector3 cross = CrossProduct( r1, r2 );
    const double crossMagnitude = static_cast<double>( Magnitude( cross ) );
    const double cosTransfer = std::clamp( static_cast<double>( Dot( r1, r2 ) ) / ( radius1 * radius2 ), -1.0, 1.0 );
    const double sinMagnitude = crossMagnitude / ( radius1 * radius2 );

    if ( sinMagnitude <= GEOMETRY_EPSILON || 1.0 - cosTransfer <= GEOMETRY_EPSILON )
    {
        return OrbitalStatus::Degenerate;
    }

    // Concept: solar-system gameplay is XZ with -Y angular momentum. XY math
    // tests use +Z. Selecting the dominant convention keeps the bool API
    // deterministic without smuggling a runtime camera/world dependency down.
    const Vector3 progradeNormal = std::fabs( cross.y ) > GEOMETRY_EPSILON ? Vector3( 0.0f, -1.0f, 0.0f )
                                                                           : Vector3( 0.0f, 0.0f, 1.0f );

    const bool directedPrograde = Dot( cross, progradeNormal ) >= 0.0f;
    const double sinTransfer = directedPrograde == prograde ? sinMagnitude : -sinMagnitude;
    const double transferA = sinTransfer * std::sqrt( radius1 * radius2 / ( 1.0 - cosTransfer ) );

    if ( std::fabs( transferA ) <= GEOMETRY_EPSILON )
    {
        return OrbitalStatus::Degenerate;
    }

    constexpr double zMin = -4.0 * static_cast<double>( PI ) * static_cast<double>( PI );
    constexpr double zMax = 4.0 * static_cast<double>( PI ) * static_cast<double>( PI );
    constexpr int bracketSamples = 192;
    const double targetTimeScaled = std::sqrt( static_cast<double>( mu ) ) * static_cast<double>( timeOfFlight );
    double lower = 0.0;
    double upper = 0.0;
    double lowerResidual = 0.0;
    bool bracketed = false;
    bool havePrevious = false;

    for ( int sample = 0; sample <= bracketSamples; ++sample )
    {
        const double z = zMin + ( zMax - zMin ) * static_cast<double>( sample ) / bracketSamples;
        double residual = 0.0;
        double y = 0.0;

        if ( !LambertTimeResidual( z, radius1, radius2, transferA, targetTimeScaled, residual, y ) )
        {
            continue;
        }

        if ( std::fabs( residual ) <= 1.0e-8 )
        {
            lower = upper = z;
            lowerResidual = residual;
            bracketed = true;
            break;
        }

        if ( havePrevious && ( residual > 0.0 ) != ( lowerResidual > 0.0 ) )
        {
            upper = z;
            bracketed = true;
            break;
        }

        lower = z;
        lowerResidual = residual;
        havePrevious = true;
    }

    if ( !bracketed )
    {
        return OrbitalStatus::NotConverged;
    }

    double z = 0.5 * ( lower + upper );
    double y = 0.0;
    bool converged = lower == upper;

    for ( int iteration = 0; iteration < LAMBERT_ITERATION_CAP && !converged; ++iteration )
    {
        double residual = 0.0;

        if ( !LambertTimeResidual( z, radius1, radius2, transferA, targetTimeScaled, residual, y ) )
        {
            z = 0.5 * ( lower + upper );
            continue;
        }

        if ( std::fabs( residual ) <= 1.0e-7 * targetTimeScaled )
        {
            converged = true;
            break;
        }

        if ( ( residual > 0.0 ) == ( lowerResidual > 0.0 ) )
        {
            lower = z;
            lowerResidual = residual;
        }
        else
        {
            upper = z;
        }

        // Invariant: Newton may accelerate only inside the established sign
        // bracket. Any invalid derivative falls back to deterministic bisection.
        const double derivativeStep = 1.0e-4 * std::max( 1.0, std::fabs( z ) );
        double shiftedResidual = 0.0;
        double shiftedY = 0.0;
        double candidate = 0.5 * ( lower + upper );

        if ( LambertTimeResidual( z + derivativeStep, radius1, radius2, transferA, targetTimeScaled, shiftedResidual,
                                  shiftedY ) )
        {
            const double derivative = ( shiftedResidual - residual ) / derivativeStep;

            if ( std::fabs( derivative ) > 1.0e-12 )
            {
                const double newton = z - residual / derivative;

                if ( newton > lower && newton < upper )
                {
                    candidate = newton;
                }
            }
        }

        z = candidate;
    }

    double finalResidual = 0.0;

    if ( !LambertTimeResidual( z, radius1, radius2, transferA, targetTimeScaled, finalResidual, y ) ||
         ( !converged && std::fabs( finalResidual ) > 1.0e-6 * targetTimeScaled ) )
    {
        return OrbitalStatus::NotConverged;
    }

    const double f = 1.0 - y / radius1;
    const double g = transferA * std::sqrt( y / static_cast<double>( mu ) );
    const double gDot = 1.0 - y / radius2;

    if ( std::fabs( g ) <= 1.0e-9 || !std::isfinite( g ) )
    {
        return OrbitalStatus::Degenerate;
    }

    out.v1 = ( r2 - r1 * static_cast<float>( f ) ) * static_cast<float>( 1.0 / g );
    out.v2 = ( r2 * static_cast<float>( gDot ) - r1 ) * static_cast<float>( 1.0 / g );

    if ( !IsFinite( out.v1 ) || !IsFinite( out.v2 ) )
    {
        out = {};

        return OrbitalStatus::NotConverged;
    }

    return OrbitalStatus::Ok;
}


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
} // namespace Orbital
} // namespace Math
} // namespace SkullbonezCore
