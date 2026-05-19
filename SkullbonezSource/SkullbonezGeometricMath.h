#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezTerrain.h"


// --- Usings ---
using namespace SkullbonezCore::Geometry;


namespace SkullbonezCore
{
namespace Math
{
/* -- Geometric Math ---------------------------------------------------------------------------------------------------------------------------------------------

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
    static GeometricMath::PointPlaneClassification ClassifyPointAgainstPlane( const Plane& plane, const Vector3& point );

    // PRECONDITION: 'point' MUST lie on the same plane as 'triangle'.
    // Returns true if the point is inside the triangle boundary.
    // Delegates to ComputeBarycentricCoordinates; all weights ≥ 0 → inside.
    static bool IsPointInsideTriangle( const Triangle& triangle, const Vector3& point );

    // Computes barycentric coordinates (u, v, w) of 'point' relative to 'triangle'.
    // All weights sum to 1.  Any weight < 0 means the point is outside that edge.
    // Projects to the most numerically stable 2D axis (largest normal component) to
    // avoid near-degenerate area computations on steep or sliver triangles.
    // Reference: 3D Math Primer for Games and Graphics Development, Dunn & Parberry, p.260
    static Vector3 ComputeBarycentricCoordinates( const Triangle& triangle, const Vector3& point );

    // Returns the CCW-winding outward unit normal of the triangle:
    //   n = normalise( (v2 - v1) × (v3 - v2) )
    static Vector3 ComputeTriangleNormal( const Triangle& triangle );

    // Signed distance from a point to a plane:   dist = dot(n, point) - d
    // Positive = in front of plane, negative = behind, zero = on the plane.
    static float DeterminePointDistFromPlane( const Plane& plane, const Vector3& point );

  public:
    // Build a plane from three non-collinear points.  plane.normal = triangle normal,
    // plane.distance = dot( normal, v1 )  (satisfies the plane equation for any point on it).
    static Plane ComputePlane( const Triangle& triangle );

    // Compute the 3D intersection point of a ray with a plane or triangle.
    // Throws if the ray does not intersect the plane within [0,1] (overloaded).
    static Vector3 ComputeIntersectionPoint( const Plane& plane, const Ray& ray );
    static Vector3 ComputeIntersectionPoint( const Ray& ray, float fCollisionTime );

    // Parametric intersection time t ∈ [0,1] for a ray hitting a plane:
    //   t = -( dot(n, origin) - d ) / dot(n, direction)
    // Returns NO_COLLISION if the ray is parallel to the plane or has no extent (overloaded).
    static float CalculateIntersectionTime( const Plane& plane, const Ray& ray );
    static float CalculateIntersectionTime( const Triangle& triangle, const Ray& ray );

    // Returns the Y-height of the terrain plane at (xCoord, zCoord) using the Law of Sines.
    // Probes with Y=0, measures signed distance to plane, then corrects vertically.
    // Formula:  Y = -( dot(n, probe) - d ) / sin( π/2 - arccos(n.y) )
    static float GetHeightFromPlane( const Triangle& triangle, float xCoord, float zCoord );
};
} // namespace Math
} // namespace SkullbonezCore
