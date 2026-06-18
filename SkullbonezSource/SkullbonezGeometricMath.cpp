/*
File: SkullbonezSource/SkullbonezGeometricMath.cpp
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
  - SkullbonezSource/SkullbonezGeometricMath.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezGeometricMath.h"


using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


// Triangle normal via cross product.
//
// Given two edges of a CCW-wound triangle:
//   edge1 = v2 - v1
//   edge2 = v3 - v2
//
// The outward-facing normal is:   n = edge1 × edge2  (then normalised)
//
// The cross product of two edge vectors gives a vector perpendicular to both.
// Counter-clockwise vertex winding (as seen from the front face) guarantees
// that the result points outward from the surface.
Vector3 GeometricMath::ComputeTriangleNormal( const Triangle& triangle )
{
    // Counter-clockwise edge order preserves the outward-facing normal.
    Vector3 edge1 = triangle.v2 - triangle.v1;
    Vector3 edge2 = triangle.v3 - triangle.v2;

    Vector3 m_normal = Vector::CrossProduct( edge1, edge2 );

    // normalise and return
    m_normal.Normalise();
    return m_normal;
}


// A plane is defined by the equation:   dot( n, P ) = d
//
// where n is the unit normal, P is any point on the plane, and d is the
// scalar distance of the plane from the origin along n.
//
// Since any vertex of the triangle lies on the plane:
//   d = dot( n, v1 )
Plane GeometricMath::ComputePlane( const Triangle& triangle )
{
    Plane plane;
    ZeroMemory( &plane, sizeof( plane ) );

    plane.m_normal = GeometricMath::ComputeTriangleNormal( triangle );

    plane.m_distance = triangle.v1 * plane.m_normal;

    return plane;
}


// Signed distance from a point to a plane.
//
// From the plane equation  dot( n, P ) = d:
//   dist = dot( n, point ) - d
//
// Positive result  → point is in front of the plane (same side as n)
// Negative result  → point is behind the plane
// Zero             → point lies on the plane
float GeometricMath::DeterminePointDistFromPlane( const Plane& plane,
                                                  const Vector3& point )
{
    // Signed distance: dot(n, point) - d
    return ( plane.m_normal * point - plane.m_distance );
}


GeometricMath::PointPlaneClassification
GeometricMath::ClassifyPointAgainstPlane( const Plane& plane,
                                          const Vector3& point )
{
    // determine the m_distance the point is to the plane
    float result = GeometricMath::DeterminePointDistFromPlane( plane, point );

    // if the m_distance is positive the point is on the front side of the plane
    if ( result > 0.0f )
    {
        return PointPlaneClassification::FrontSideOfPlane;
    }

    // if the m_distance is negative the point is on the back side of the plane
    if ( result < 0.0f )
    {
        return PointPlaneClassification::BackSideOfPlane;
    }

    // if the m_distance is 0, the point coincides with the plane
    return PointPlaneClassification::CoincideWithPlane;
}


// Solve for the Y-coordinate of a terrain plane at a given (X, Z) position.
//
// Strategy: use the signed-distance formula to find how far the projected XZ
// point (with Y = 0) sits from the plane, then push it vertically until it
// lands on the surface.
//
// Derivation using the Law of Sines:
//
//   The signed distance from the XZ probe point to the plane (along the normal)
//   is known: normalDist = dot( n, probe ) - d
//
//   Let θ = angle between the plane normal and the vertical axis (0,1,0).
//
//     cos(θ) = n.y   →   θ = arccos( n.y )
//
//   The elevation correction Δy we must move *vertically* to reach the plane is
//   related to normalDist by the Law of Sines in the right triangle formed by
//   the normal direction and the vertical:
//
//     sin( π/2 - θ ) = sin( complement of θ ) = cos( θ ) = n.y
//
//   But we want the angle between the *normal* and the *vertical* direction:
//     θ_from_horizontal = π/2 - arccos( n.y )
//
//   By the Law of Sines in the right triangle:
//     Δy / 1 = normalDist / sin( θ_from_horizontal )
//
//   Therefore:
//     Δy = normalDist / sin( θ_from_horizontal )
//
//   A negation is applied because a positive normalDist (probe above plane)
//   means we must move *down* to reach the surface.
float GeometricMath::GetHeightFromPlane( const Triangle& triangle,
                                         float xCoord,
                                         float zCoord )
{
    // probe the XZ plane (Y = 0) to find vertical offset to terrain surface
    Vector3 point = Vector3( xCoord, 0.0f, zCoord );

    Plane trianglePlane = GeometricMath::ComputePlane( triangle );

    // How far above/below the plane is this XZ probe point (at Y=0)?
    float normalDist = GeometricMath::DeterminePointDistFromPlane( trianglePlane, point );

    // θ = angle between the plane normal and the vertical axis (0,1,0).
    // cos(θ) = dot( n, (0,1,0) ) = n.y, so θ = arccos( n.y ).
    // We want sin of the complement angle from horizontal: sin(π/2 - θ).
    float theta = _HALF_PI - acosf( trianglePlane.m_normal.y );

    // Law of sines: Δy = normalDist / sin(θ_from_horizontal).
    // Negate because a probe above the plane must move down to reach it.
    return -( normalDist / sinf( theta ) );
}


// Ray-plane intersection time.
//
// A ray is parameterised as:   P(t) = origin + t * direction
//
// The ray hits the plane when:   dot( n, P(t) ) = d
//
// Substituting the ray:
//   dot( n, origin + t * direction ) = d
//   dot( n, origin ) + t * dot( n, direction ) = d
//
// Solving for t:
//   t = ( d - dot( n, origin ) ) / dot( n, direction )
//     = -( dot( n, origin ) - d ) / dot( n, direction )
//
// t = 0 → ray starts on the plane
// 0 < t ≤ 1 → plane is hit within this frame's movement vector
// dot(n, direction) = 0 → ray is parallel to plane → no intersection
float GeometricMath::CalculateIntersectionTime( const Plane& plane,
                                                const Ray& ray )
{
    // ensure data is valid
    if ( plane.m_normal == ZERO_VECTOR )
    {
        throw std::runtime_error( "Division by zero!  (GeometricMath::CalculateIntersectionTime)" );
    }

    // if the ray doesnt go anywhere then no collision will occur
    if ( ray.vector3.IsCloseToZero() )
    {
        return NO_COLLISION;
    }

    // check the m_normal and ray aren't perpendicular to each other
    float denominator = plane.m_normal * ray.vector3;
    if ( !denominator )
    {
        return NO_COLLISION;
    }

    // t = -( dot(n, origin) - d ) / dot(n, direction)
    return -( ( ( plane.m_normal * ray.origin ) - plane.m_distance ) / denominator );
}


float GeometricMath::CalculateIntersectionTime( const Triangle& triangle,
                                                const Ray& ray )
{
    return GeometricMath::CalculateIntersectionTime(
        GeometricMath::ComputePlane( triangle ),
        ray );
}


Vector3 GeometricMath::ComputeIntersectionPoint( const Plane& plane,
                                                 const Ray& ray )
{
    float collisionTime = GeometricMath::CalculateIntersectionTime( plane, ray );

    // ensure the ray intersects with the plane
    if ( collisionTime > 1.0f || collisionTime < 0.0f )
    {
        throw std::runtime_error( "Supplied ray will not intersect with this plane!  (GeometricMath::ComputeIntersectionPoint)" );
    }

    // translate from the origin of the ray along the ray until the collision
    // occurs, and return this vector
    return GeometricMath::ComputeIntersectionPoint( ray, collisionTime );
}


// Evaluates the parametric ray at time t: P(t) = origin + t * direction
// Returns the 3D world position that lies fraction t along the ray vector.
// t = 0 → ray origin;  t = 1 → tip of direction vector;  t ∈ (0,1) → somewhere between.
Vector3 GeometricMath::ComputeIntersectionPoint( const Ray& ray,
                                                 float fCollisionTime )
{
    return ray.origin + ( ray.vector3 * fCollisionTime );
}


bool GeometricMath::IsPointInsideTriangle( const Triangle& triangle,
                                           const Vector3& point )
{
    Vector3 barycentricCoords =
        GeometricMath::ComputeBarycentricCoordinates( triangle, point );

    // if the point lies outside of the triangle, there will be at least one
    // negative barycentric coordinate.  returning the result of this test will
    // determine whether the point lies inside or outside of the triangle
    return ( barycentricCoords.x >= 0 &&
             barycentricCoords.y >= 0 &&
             barycentricCoords.z >= 0 );
}


Vector3 GeometricMath::ComputeBarycentricCoordinates( const Triangle& triangle,
                                                      const Vector3& point )
{
    Vector3 m_normal = GeometricMath::ComputeTriangleNormal( triangle );

    // convert the m_normal to an absolute representation
    m_normal.Absolute();

    /*
        In order to get the most accurate calculation,  it is optimal to
        project the triangle onto the plane that will give the projected
        triangle the largest possible area.  this is done by taking the
        largest absolute component of the m_normal, and discarding this
        component from the supplied point and triangle

        Triangle after projection (assume XY projection):
                v1
                  /	\
                 /	 \					Assume:  Positive Y is upwards
                /	  \						     Positive X is to the right
               /	   \
              /			\
             /			 \
            /			  \
           /	. <- point \
          /					\
         /					 \
        /					  \
     v0 ----------------------- v2

        Assume the triangle has been projected onto the appropriate plane.
        Let p = point, and assume the v0, v1 and v2 are the coordinates that make
        up the Triangle 'triangle'

        Eight 2-Dimensional vectors now need to be calculated.  This calculation
        will assist in determining the relative weights of the vertices to formulate
        the point 'p' through the barycentric computations

        Assume we are projected onto the XY plane.

        We need to find:
            (a) the vector represented by v2->v0 for X (edge 1)
            (b) the vector represented by v2->v0 for Y (edge 1)
            (c) the vector represented by v2->v1 for X (edge 2)
            (d) the vector represented by v2->v1 for Y (edge 2)
            (e) the vector represented by v0->p  for X (inner edge 1)
            (f) the vector represented by v0->p  for Y (inner edge 1)
            (g) the vector represented by v2->p  for X (inner edge 2)
            (h) the vector represented by v2->p  for Y (inner edge 2)

        This can be expressed (irrespective of projection) by:
            float v2_v0_axis1,  // edge 1
                  v2_v0_axis2,  // edge 1
                  v2_v1_axis1,  // edge 2
                  v2_v1_axis2,  // edge 2
                  v0_p_axis1,   // inner edge 1
                  v0_p_axis2,   // inner edge 1
                  v2_p_axis1,   // inner edge 2
                  v2_p_axis2;   // inner edge 2

         Now, back to our assumed XY projection:
            v2_v0_axis1 = v0.x - v2.x;	// edge 1
            v2_v0_axis2 = v0.y - v2.y;	// edge 1
            v2_v1_axis1 = v1.x - v2.x;	// edge 2
            v2_v1_axis2 = v1.y - v2.y;	// edge 2
            v0_p_axis1	= p.x  - v0.x;	// inner edge 1
            v0_p_axis2	= p.y  - v0.y;	// inner edge 1
            v2_p_axis1	= p.x  - v2.x;	// inner edge 2
            v2_p_axis2	= p.y  - v2.y;	// inner edge 2

         And the same goes for the other projections.
    */

    float v2_v0_axis1, // edge 1
        v2_v0_axis2,   // edge 1
        v2_v1_axis1,   // edge 2
        v2_v1_axis2,   // edge 2
        v0_p_axis1,    // inner edge 1
        v0_p_axis2,    // inner edge 1
        v2_p_axis1,    // inner edge 2
        v2_p_axis2;    // inner edge 2

    if ( m_normal.x >= m_normal.y && m_normal.x >= m_normal.z )
    {
        // discard 'x' component, project onto yz plane
        v2_v0_axis1 = triangle.v1.y - triangle.v3.y; // edge 1
        v2_v0_axis2 = triangle.v1.z - triangle.v3.z; // edge 1
        v2_v1_axis1 = triangle.v2.y - triangle.v3.y; // edge 2
        v2_v1_axis2 = triangle.v2.z - triangle.v3.z; // edge 2
        v0_p_axis1 = point.y - triangle.v1.y;        // inner edge 1
        v0_p_axis2 = point.z - triangle.v1.z;        // inner edge 1
        v2_p_axis1 = point.y - triangle.v3.y;        // inner edge 2
        v2_p_axis2 = point.z - triangle.v3.z;        // inner edge 2
    }
    else if ( m_normal.y >= m_normal.z )
    {
        // discard 'y' component, project onto xz plane
        v2_v0_axis1 = triangle.v1.z - triangle.v3.z; // edge 1
        v2_v0_axis2 = triangle.v1.x - triangle.v3.x; // edge 1
        v2_v1_axis1 = triangle.v2.z - triangle.v3.z; // edge 2
        v2_v1_axis2 = triangle.v2.x - triangle.v3.x; // edge 2
        v0_p_axis1 = point.z - triangle.v1.z;        // inner edge 1
        v0_p_axis2 = point.x - triangle.v1.x;        // inner edge 1
        v2_p_axis1 = point.z - triangle.v3.z;        // inner edge 2
        v2_p_axis2 = point.x - triangle.v3.x;        // inner edge 2
    }
    else
    {
        // discard 'z' component, project onto xy plane
        v2_v0_axis1 = triangle.v1.x - triangle.v3.x; // edge 1
        v2_v0_axis2 = triangle.v1.y - triangle.v3.y; // edge 1
        v2_v1_axis1 = triangle.v2.x - triangle.v3.x; // edge 2
        v2_v1_axis2 = triangle.v2.y - triangle.v3.y; // edge 2
        v0_p_axis1 = point.x - triangle.v1.x;        // inner edge 1
        v0_p_axis2 = point.y - triangle.v1.y;        // inner edge 1
        v2_p_axis1 = point.x - triangle.v3.x;        // inner edge 2
        v2_p_axis2 = point.y - triangle.v3.y;        // inner edge 2
    }

    /*
        Now the computation is complete, it is time to compute the
        barycentric coordinates.

        Computing barycentric coordinates for a 2d point is defined
        as follows (following on from above comments)

                v2_p_axis2  * v2_v1_axis1 - v2_v1_axis2 * v2_p_axis1
        b1 =	-----------------------------------------------------
                v2_v0_axis2 * v2_v1_axis1 - v2_v1_axis2 * v2_v0_axis1

                v0_p_axis2  * v2_v0_axis1 - v2_v0_axis2 * v0_p_axis1
        b2 =	-----------------------------------------------------
                v2_v0_axis2 * v2_v1_axis1 - v2_v1_axis2 * v2_v0_axis1

        b3 also contains its own equation, BUT since b3 can be derrived from
        b1 and b2 which will already be calculated, it is not necessary to
        compute it
    */

    // pre-calculate the denominator
    float denominator = v2_v0_axis2 * v2_v1_axis1 - v2_v1_axis2 * v2_v0_axis1;

    // check for a division by zero (this would imply the supplied triangle
    // was co-linear)
    if ( !denominator )
    {
        throw std::runtime_error( "Division by zero due to co-linear triangle.  (GeometricMath::ComputeBarycentricCoordinates)" );
    }

    Vector3 barycentricResult = Vector3( ( v2_p_axis2 * v2_v1_axis1 -
                                           v2_v1_axis2 * v2_p_axis1 ) /
                                             denominator,
                                         ( v0_p_axis2 * v2_v0_axis1 -
                                           v2_v0_axis2 * v0_p_axis1 ) /
                                             -denominator,
                                         0.0f );

    // derrive the Z component
    barycentricResult.z = 1.0f - barycentricResult.x - barycentricResult.y;

    return barycentricResult;
}
