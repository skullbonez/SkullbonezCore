/*
File: SkullbonezSource/Maths/GeometricMath.cpp
Purpose:
  Implements plane construction and ray-plane intersection time.

Summary:
  Owns the triangle-normal helper used to construct planes and the
  production ray-plane intersection calculation. Unreachable legacy
  height, point, and barycentric operations are intentionally absent.

Invariants:
  - Plane representation is dot(normal, point) = distance, with normal expected
    to be unit length.
  - Triangle normal direction follows the engine's counter-clockwise winding
    convention; callers depend on signed distance polarity.
  - Degenerate or collinear triangles deterministically produce a zero-normal
    plane; only a later ray/plane query treats that normal as a bad contract.

Related:
  - SkullbonezSource/Maths/GeometricMath.h
  - Agentic/Reference/engine-glossary.md
*/
#include "GeometricMath.h"

#include <cassert>


using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


namespace
{
// Triangle normal via cross product.
//
// Given two edges of a CCW-wound triangle:
//   edge1 = v2 - v1
//   edge2 = v3 - v2
//
// The outward-facing normal is: n = edge1 × edge2 (then normalized).
Vector3 ComputeTriangleNormal( const Triangle& triangle )
{
    const Vector3 edge1 = triangle.v2 - triangle.v1;
    const Vector3 edge2 = triangle.v3 - triangle.v2;
    Vector3 normal = Vector::CrossProduct( edge1, edge2 );

    if ( !normal.TryNormalise() )
    {
        // Fallback: a degenerate triangle has no direction-bearing plane; a
        // zero normal lets missable queries report NO_COLLISION deterministically.
        return ZERO_VECTOR;
    }

    return normal;
}
} // namespace


// A plane is defined by dot( n, P ) = d. Since any triangle vertex lies on
// the plane, d = dot( n, v1 ).
Plane GeometricMath::ComputePlane( const Triangle& triangle )
{
    Plane plane {};
    plane.m_normal = ComputeTriangleNormal( triangle );
    plane.m_distance = Dot( triangle.v1, plane.m_normal );
    return plane;
}


// Ray-plane intersection time for P(t) = origin + t * direction:
//   t = -( dot( n, origin ) - d ) / dot( n, direction )
float GeometricMath::CalculateIntersectionTime( const Plane& plane, const Ray& ray )
{
    // Why: malformed query geometry is caller-reachable input, not lane F.
    // Debug diagnoses misuse; Release falls through to the ordinary parallel
    // denominator path and reports NO_COLLISION.
    assert( plane.m_normal != ZERO_VECTOR && "CalculateIntersectionTime requires a non-zero plane normal" );

    if ( ray.vector3.IsCloseToZero() )
    {
        return NO_COLLISION;
    }

    const float denominator = Dot( plane.m_normal, ray.vector3 );

    if ( !denominator )
    {
        return NO_COLLISION;
    }

    return -( ( Dot( plane.m_normal, ray.origin ) - plane.m_distance ) / denominator );
}
