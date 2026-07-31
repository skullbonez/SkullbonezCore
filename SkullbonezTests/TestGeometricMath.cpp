//
// File: SkullbonezTests/TestGeometricMath.cpp
// Purpose:
//   Lock the production-reachable pure-math contracts for GeometricMath.
//
// Summary:
//   GeometricMath is the plane and ray-segment helper used by terrain physics.
//   Its ray API uses a movement vector rather than an infinite normalized
//   direction, so a valid collision time lives in the inclusive range [0,1].
//
// Glossary:
//   Ray segment: Origin plus a finite displacement vector for one query.
//   Plane equation: dot(normal, point) = distance.
//   NO_COLLISION: Sentinel time used when a ray segment cannot hit a plane.
//
// Invariants:
//   - Triangle winding controls the plane normal sign.
//   - Degenerate fatal paths are not invoked by this in-process runner; callers
//     must detect miss/precondition states before fatal-only APIs.
//
// Related:
//   - SkullbonezSource/Maths/GeometricMath.h
//   - SkullbonezSource/Maths/GeometricStructures.h
//   - Agentic/Reports/2026-07-30/maths-surface-reachability-mr0-census.md
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Maths/GeometricMath.h"
#include "../SkullbonezSource/Maths/GeometricStructures.h"

#include <cmath>

using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Geometry::Ray;
using SkullbonezCore::Geometry::Triangle;
using SkullbonezCore::Math::GeometricMath;
using SkullbonezCore::Math::Vector::CrossProduct;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::VectorMagSquared;

namespace
{
constexpr float kEpsilon = 0.00001f;

void CheckNear( float actual, float expected, float epsilon = kEpsilon )
{
    CHECK( std::fabs( actual - expected ) <= epsilon );
}

void CheckVectorNear( const Vector3& actual, const Vector3& expected, float epsilon = kEpsilon )
{
    CheckNear( actual.x, expected.x, epsilon );
    CheckNear( actual.y, expected.y, epsilon );
    CheckNear( actual.z, expected.z, epsilon );
}

Triangle MakeTriangle( const Vector3& v1, const Vector3& v2, const Vector3& v3 )
{
    Triangle triangle;
    triangle.v1 = v1;
    triangle.v2 = v2;
    triangle.v3 = v3;
    return triangle;
}

Triangle FlatTriangle()
{
    return MakeTriangle( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 1.0f ) );
}

Triangle SlopedTriangle()
{
    return MakeTriangle( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 1.0f, 0.0f ), Vector3( 0.0f, 1.0f, 1.0f ) );
}
} // namespace


TEST_CASE( "GeometricMath: plane construction follows triangle winding" )
{
    const Plane flatPlane = GeometricMath::ComputePlane( FlatTriangle() );
    CheckVectorNear( flatPlane.m_normal, Vector3( 0.0f, -1.0f, 0.0f ) );
    CheckNear( flatPlane.m_distance, 0.0f );

    const Plane slopedPlane = GeometricMath::ComputePlane( SlopedTriangle() );
    CheckVectorNear( slopedPlane.m_normal, Vector3( 0.57735026f, -0.57735026f, 0.57735026f ) );
}


TEST_CASE( "GeometricMath: ray-plane segment hit, miss, and boundary times" )
{
    const Plane plane = GeometricMath::ComputePlane( FlatTriangle() );

    const Ray hit( Vector3( 0.0f, 2.0f, 0.0f ), Vector3( 0.0f, -4.0f, 0.0f ) );
    CheckNear( GeometricMath::CalculateIntersectionTime( plane, hit ), 0.5f );

    const Ray endpointHit( Vector3( 0.0f, 2.0f, 0.0f ), Vector3( 0.0f, -2.0f, 0.0f ) );
    CheckNear( GeometricMath::CalculateIntersectionTime( plane, endpointHit ), 1.0f );

    const Ray startsOnPlane( Vector3( 0.5f, 0.0f, 0.5f ), Vector3( 0.0f, -2.0f, 0.0f ) );
    CheckNear( GeometricMath::CalculateIntersectionTime( plane, startsOnPlane ), 0.0f );

    const Ray parallel( Vector3( 0.0f, 2.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ) );
    CHECK( GeometricMath::CalculateIntersectionTime( plane, parallel ) == NO_COLLISION );

    const Ray zeroLength( Vector3( 0.0f, 2.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    CHECK( GeometricMath::CalculateIntersectionTime( plane, zeroLength ) == NO_COLLISION );
}


TEST_CASE( "GeometricMath: degenerate fatal preconditions are caller-detectable" )
{
    Plane zeroNormal;
    zeroNormal.m_normal = Vector3( 0.0f, 0.0f, 0.0f );
    zeroNormal.m_distance = 0.0f;
    CHECK( zeroNormal.m_normal == Vector3( 0.0f, 0.0f, 0.0f ) );

    const Plane plane = GeometricMath::ComputePlane( FlatTriangle() );
    const Ray awayFromPlane( Vector3( 0.0f, 2.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ) );
    CHECK( GeometricMath::CalculateIntersectionTime( plane, awayFromPlane ) < 0.0f );

    const Triangle collinear =
        MakeTriangle( Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 2.0f, 0.0f, 0.0f ) );
    const Vector3 edge1 = collinear.v2 - collinear.v1;
    const Vector3 edge2 = collinear.v3 - collinear.v2;
    CHECK( VectorMagSquared( CrossProduct( edge1, edge2 ) ) == doctest::Approx( 0.0f ) );
}


TEST_CASE( "GeometricMath: a degenerate triangle produces the zero plane fallback" )
{
    const Triangle collinear( Vector3( 0.0f, 0.0f, 0.0f ),
                              Vector3( 1.0f, 1.0f, 1.0f ),
                              Vector3( 2.0f, 2.0f, 2.0f ) );

    const Plane plane = GeometricMath::ComputePlane( collinear );

    CHECK( plane.m_normal == SkullbonezCore::Math::Vector::ZERO_VECTOR );
    CHECK( plane.m_distance == doctest::Approx( 0.0f ) );
}
