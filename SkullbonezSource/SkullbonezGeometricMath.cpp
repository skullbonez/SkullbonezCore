// --- Includes ---
#include "SkullbonezGeometricMath.h"


// --- Usings ---
using namespace SkullbonezCore::Math;


Vector3 GeometricMath::ComputeTriangleNormal( const Triangle& triangle )
{
    // CCW winding:  n = normalize( (v2 - v1) x (v3 - v2) )
    Vector3 normal = Vector::CrossProduct( triangle.v2 - triangle.v1,
                                           triangle.v3 - triangle.v2 );
    normal.Normalise();
    return normal;
}


Plane GeometricMath::ComputePlane( const Triangle& triangle )
{
    // Plane equation:  n · p = d   with   n from triangle, d = n · v1
    Plane plane;
    ZeroMemory( &plane, sizeof( plane ) );
    plane.m_normal = ComputeTriangleNormal( triangle );
    plane.m_distance = triangle.v1 * plane.m_normal;
    return plane;
}


float GeometricMath::DeterminePointDistFromPlane( const Plane& plane,
                                                  const Vector3& point )
{
    // Signed distance:  s = n · p - d
    return plane.m_normal * point - plane.m_distance;
}


GeometricMath::PointPlaneClassification
GeometricMath::ClassifyPointAgainstPlane( const Plane& plane,
                                          const Vector3& point )
{
    //   s > 0  →  front,   s < 0  →  back,   s = 0  →  on plane
    const float s = DeterminePointDistFromPlane( plane, point );
    if ( s > 0.0f ) return PointPlaneClassification::FrontSideOfPlane;
    if ( s < 0.0f ) return PointPlaneClassification::BackSideOfPlane;
    return PointPlaneClassification::CoincideWithPlane;
}


float GeometricMath::GetHeightFromPlane( const Triangle& triangle,
                                         float xCoord,
                                         float zCoord )
{
    // Drop a vertical line (0,1,0) through (x, 0, z) and find where it meets the plane.
    // Let s = signed distance to plane, theta = angle between plane normal and vertical.
    // Since n.(0,1,0) = n.y = cos(angle_to_up), the angle between the plane itself
    // and vertical is theta = pi/2 - acos(n.y), and:
    //   y =  -s / sin(theta)
    const Vector3 point = Vector3( xCoord, 0.0f, zCoord );
    const Plane trianglePlane = ComputePlane( triangle );
    const float s = DeterminePointDistFromPlane( trianglePlane, point );
    const float theta = _HALF_PI - acosf( trianglePlane.m_normal.y );
    return -( s / sinf( theta ) );
}


float GeometricMath::CalculateIntersectionTime( const Plane& plane,
                                                const Ray& ray )
{
    // Ray hits plane n·p = d when n·(o + t·d_ray) = d, so:
    //   t = -(n·o - d) / (n·d_ray)
    if ( plane.m_normal == ZERO_VECTOR )
    {
        throw std::runtime_error( "Division by zero!  (GeometricMath::CalculateIntersectionTime)" );
    }
    if ( ray.vector3.IsCloseToZero() )
    {
        return NO_COLLISION;
    }
    const float denom = plane.m_normal * ray.vector3;
    if ( !denom )
    {
        return NO_COLLISION; // ray parallel to plane
    }
    return -( ( plane.m_normal * ray.origin ) - plane.m_distance ) / denom;
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
    const float t = CalculateIntersectionTime( plane, ray );
    if ( t > 1.0f || t < 0.0f )
    {
        throw std::runtime_error( "Supplied ray will not intersect with this plane!  (GeometricMath::ComputeIntersectionPoint)" );
    }
    return ComputeIntersectionPoint( ray, t );
}


Vector3 GeometricMath::ComputeIntersectionPoint( const Ray& ray,
                                                 float fCollisionTime )
{
    // p(t) = origin + t · direction
    return ray.origin + ( ray.vector3 * fCollisionTime );
}


bool GeometricMath::IsPointInsideTriangle( const Triangle& triangle,
                                           const Vector3& point )
{
    // Inside iff all three barycentric weights are non-negative
    const Vector3 b = ComputeBarycentricCoordinates( triangle, point );
    return ( b.x >= 0 && b.y >= 0 && b.z >= 0 );
}


Vector3 GeometricMath::ComputeBarycentricCoordinates( const Triangle& triangle,
                                                      const Vector3& point )
{
    // Project onto the axis-aligned plane perpendicular to the triangle's largest
    // normal component — that gives the projected triangle the maximum 2D area
    // (and best numerical conditioning). Drop the dominant axis; keep the other two.
    //
    // 2D barycentric weights for projected point p relative to triangle (v1,v2,v3),
    // using edges measured from v3:
    //
    //         (p-v3).a2 · (v2-v3).a1  -  (v2-v3).a2 · (p-v3).a1
    //   b1 = ───────────────────────────────────────────────────
    //         (v1-v3).a2 · (v2-v3).a1  -  (v2-v3).a2 · (v1-v3).a1
    //
    //         (p-v1).a2 · (v1-v3).a1  -  (v1-v3).a2 · (p-v1).a1
    //   b2 = ─────────────────────────────────────────────────── × (-1/denom)
    //
    //   b3 = 1 - b1 - b2
    //
    // (a1, a2) = the two retained axes after projection. See "3D Math Primer
    // for Games and Graphics Development" by Dunn & Parberry, p.260.
    Vector3 normal = ComputeTriangleNormal( triangle );
    normal.Absolute();

    float e1_a, e1_b; // (v1 - v3) on retained axes
    float e2_a, e2_b; // (v2 - v3)
    float p1_a, p1_b; // (p  - v1)
    float p3_a, p3_b; // (p  - v3)

    if ( normal.x >= normal.y && normal.x >= normal.z )
    {
        // drop X — project to YZ
        e1_a = triangle.v1.y - triangle.v3.y;  e1_b = triangle.v1.z - triangle.v3.z;
        e2_a = triangle.v2.y - triangle.v3.y;  e2_b = triangle.v2.z - triangle.v3.z;
        p1_a = point.y       - triangle.v1.y;  p1_b = point.z       - triangle.v1.z;
        p3_a = point.y       - triangle.v3.y;  p3_b = point.z       - triangle.v3.z;
    }
    else if ( normal.y >= normal.z )
    {
        // drop Y — project to ZX
        e1_a = triangle.v1.z - triangle.v3.z;  e1_b = triangle.v1.x - triangle.v3.x;
        e2_a = triangle.v2.z - triangle.v3.z;  e2_b = triangle.v2.x - triangle.v3.x;
        p1_a = point.z       - triangle.v1.z;  p1_b = point.x       - triangle.v1.x;
        p3_a = point.z       - triangle.v3.z;  p3_b = point.x       - triangle.v3.x;
    }
    else
    {
        // drop Z — project to XY
        e1_a = triangle.v1.x - triangle.v3.x;  e1_b = triangle.v1.y - triangle.v3.y;
        e2_a = triangle.v2.x - triangle.v3.x;  e2_b = triangle.v2.y - triangle.v3.y;
        p1_a = point.x       - triangle.v1.x;  p1_b = point.y       - triangle.v1.y;
        p3_a = point.x       - triangle.v3.x;  p3_b = point.y       - triangle.v3.y;
    }

    // 2× signed area of projected triangle (zero ⇒ colinear)
    const float denom = e1_b * e2_a - e2_b * e1_a;
    if ( !denom )
    {
        throw std::runtime_error( "Division by zero due to co-linear triangle.  (GeometricMath::ComputeBarycentricCoordinates)" );
    }

    Vector3 b;
    b.x = ( p3_b * e2_a - e2_b * p3_a ) /  denom;
    b.y = ( p1_b * e1_a - e1_b * p1_a ) / -denom;
    b.z = 1.0f - b.x - b.y;
    return b;
}
