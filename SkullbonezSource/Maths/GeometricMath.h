/*
File: SkullbonezSource/Maths/GeometricMath.h
Purpose:
  Provides the plane construction and ray-plane query used by terrain physics.

Summary:
  GeometricMath exposes only the two geometry operations with production
  callers: triangle-to-plane construction and plane/ray intersection time.
  Triangle-normal construction remains an implementation detail.

Glossary:
  Layering boundary: Compile-time dependency direction; Maths must stay below
    World so math helpers can be tested and reused without terrain/runtime
    ownership.

Invariants:
  - GeometricMath is a static-only utility and retains no geometry state.
  - Plane representation is dot(normal, point) = distance, with normal expected
    to be unit length.
  - NO_COLLISION marks ray/plane misses and must not be confused with a valid
    collision time.
  - Degenerate triangles produce a zero-normal plane deterministically.
  - Ray/plane queries require a non-zero plane normal; Debug diagnoses that
    query contract while Release returns through the ordinary miss path.

Related:
  - SkullbonezSource/Maths/GeometricMath.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include "MathsCommon.h"
#include "GeometricStructures.h"

namespace SkullbonezCore
{
namespace Math
{

class GeometricMath
{
  public:

    // Build a plane from three non-collinear points. plane.normal = triangle normal,
    // plane.distance = dot( normal, v1 ) (satisfies the plane equation for any point on it).
    static Geometry::Plane ComputePlane( const Geometry::Triangle& triangle );

    // Signed parametric intersection time for a ray hitting a plane:
    //   t = -( dot(n, origin) - d ) / dot(n, direction)
    // Negative values are valid intersections behind the ray origin.
    // NO_COLLISION means the ray is parallel to the plane or has no extent.
    static float CalculateIntersectionTime( const Geometry::Plane& plane, const Geometry::Ray& ray );
};
} // namespace Math
} // namespace SkullbonezCore
