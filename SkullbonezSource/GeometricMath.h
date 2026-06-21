/*
File: SkullbonezSource/GeometricMath.h
Purpose:
  Provides geometric helper functions for collision, projection, and intersection tests.

Mental model:
  Math code is shared infrastructure. Coordinate conventions, units,
  handedness, and simplifications matter because subtle assumptions spread
  through rendering and physics.

Glossary:
  Engine module: A source file with one focused responsibility inside the
  SkullbonezCore runtime.

Related:
  - SkullbonezSource/GeometricMath.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"
#include "Terrain.h"

namespace SkullbonezCore
{
namespace Math
{
/* -- Geometric Math
---------------------------------------------------------------------------------------------------------------------------------------------

    Static utility methods for the linear-algebra and spatial-query operations that underpin
    collision detection and terrain queries.

    All methods are stateless.  No instances of this class are ever created.

    Key operations:
      - Triangle normal and plane construction          (cross product + dot product)
      - Signed point-to-plane distance                  (dot(n, P) - d)
      - Ray-plane intersection time and point           (parametric ray equation)
      - Terrain height at (X, Z) coordinates            (signed distance + Law of Sines)
      - Point-in-triangle test                          (barycentric coordinate signs)
      - Barycentric coordinate computation              (axis-dropped 2D projection)

    Plane representation used throughout:   dot( n, P ) = d
      n = unit normal, d = scalar distance from origin along n.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class GeometricMath
{
  private:
    // The possible outcomes for testing a point against a plane
    enum class PointPlaneClassification
    {
        FrontSideOfPlane,
        BackSideOfPlane,
        CoincideWithPlane
    };

    // Classifies whether a point is on the front side, back side, or coincides with the specified plane.
    // Uses DeterminePointDistFromPlane: positive = front, negative = back, zero = on plane.
    static GeometricMath::PointPlaneClassification ClassifyPointAgainstPlane( const Geometry::Plane& plane,
                                                                              const Vector::Vector3& point );

    // PRECONDITION: 'point' MUST lie on the same plane as 'triangle'.
    // Contract: true means the point is inside the triangle boundary.
    // Delegates to ComputeBarycentricCoordinates; all weights ≥ 0 → inside.
    static bool IsPointInsideTriangle( const Geometry::Triangle& triangle, const Vector::Vector3& point );

    // Barycentric output is (u, v, w) for 'point' relative to 'triangle'.
    // All weights sum to 1.  Any weight < 0 means the point is outside that edge.
    // Projects to the most numerically stable 2D axis (largest normal component) to
    // avoid near-degenerate area computations on steep or sliver triangles.
    // Reference: 3D Math Primer for Games and Graphics Development, Dunn & Parberry, p.260
    static Vector::Vector3 ComputeBarycentricCoordinates( const Geometry::Triangle& triangle,
                                                          const Vector::Vector3& point );

    // CCW-winding outward unit normal of the triangle:
    //   n = normalise( (v2 - v1) × (v3 - v2) )
    static Vector::Vector3 ComputeTriangleNormal( const Geometry::Triangle& triangle );

    // Signed distance from a point to a plane:   dist = dot(n, point) - d
    // Positive = in front of plane, negative = behind, zero = on the plane.
    static float DeterminePointDistFromPlane( const Geometry::Plane& plane, const Vector::Vector3& point );

  public:
    // Build a plane from three non-collinear points.  plane.normal = triangle normal,
    // plane.distance = dot( normal, v1 )  (satisfies the plane equation for any point on it).
    static Geometry::Plane ComputePlane( const Geometry::Triangle& triangle );

    // Throws when the supplied ray does not reach the plane/triangle segment.
    // Throws if the ray does not intersect the plane within [0,1] (overloaded).
    static Vector::Vector3 ComputeIntersectionPoint( const Geometry::Plane& plane, const Geometry::Ray& ray );
    static Vector::Vector3 ComputeIntersectionPoint( const Geometry::Ray& ray, float fCollisionTime );

    // Parametric intersection time t ∈ [0,1] for a ray hitting a plane:
    //   t = -( dot(n, origin) - d ) / dot(n, direction)
    // NO_COLLISION means the ray is parallel to the plane or has no extent.
    static float CalculateIntersectionTime( const Geometry::Plane& plane, const Geometry::Ray& ray );
    static float CalculateIntersectionTime( const Geometry::Triangle& triangle, const Geometry::Ray& ray );

    // Terrain-plane Y-height at (xCoord, zCoord) using the Law of Sines.
    // Probes with Y=0, measures signed distance to plane, then corrects vertically.
    // Formula:  Y = -( dot(n, probe) - d ) / sin( π/2 - arccos(n.y) )
    static float GetHeightFromPlane( const Geometry::Triangle& triangle, float xCoord, float zCoord );
};
} // namespace Math
} // namespace SkullbonezCore
