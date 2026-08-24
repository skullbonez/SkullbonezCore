/*
File: SkullbonezSource/Physics/BoundingSphere.cpp
Purpose:
  Defines sphere collision geometry, swept tests, volume facts, and render transforms.

Summary:
  BoundingSphere owns sphere shape facts and deterministic overlap, swept,
  inertia, broadphase, and render-transform calculations used by collision and
  presentation.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/BoundingSphere.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#include "BoundingSphere.h"
#include "CollisionShape.h"
#include "ConvexHullShape.h"
#include <immintrin.h> // SSE intrinsics for scale pass in GetModelMatrix


using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


BoundingSphere::BoundingSphere() : m_position( ZERO_VECTOR ), m_radius( 0.0f ), m_dragCoefficient( 0.4f )
{
}


BoundingSphere::BoundingSphere( float radius, const Vector3& localPosition, float dragCoefficient )
    : m_position( localPosition ), m_radius( radius ), m_dragCoefficient( dragCoefficient )
{
}

// This file contains two different levels of sphere collision:
//   1. Broadphase-style swept tests: cheap "could these shapes meet this tick?"
//      checks that return a time value.
//   2. Shape facts: radius, volume, area, drag, and render matrix.
//
// The precise resting/contact response is not here. If a broadphase test says
// "possible hit", PhysicsWorld asks the narrowphase manifold code for exact
// contact points and lets the shared Catto-style row solver respond.

/* Concept: swept sphere-vs-sphere collision detection.
 *
 * Finds the earliest time t ∈ [0,1] at which two moving spheres first touch.
 *
 * Setup:
 *   d = focus.center - target.center   (relative center vector at frame start)
 *   v = focus.velocity - target.velocity  (relative velocity this frame)
 *   R = r_focus + r_target               (sum of radii; contact when centers are this far apart)
 *
 * The spheres touch when |d + v·t|² = R²
 * Expanding:  (v·v)t² + 2(d·v)t + (d·d - R²) = 0
 *             with a = v·v,  b = d·v,  c = d·d - R²
 * Solution:   t = (-b - sqrt(b²-ac)) / a   (earlier root = first contact)
 *
 * Early rejection checks (cheapest first):
 *   1. No relative motion (|v|² ≈ 0) → no sweep, return NO_COLLISION
 *   2. Already overlapping (|d|² ≤ R²) → static resolution handles it, skip sweep
 *   3. Moving apart (d·v ≥ 0) → separating this frame, no impact possible
 *   4. Reachability cull: even at full frame travel they can't reach R apart
 *   5. Negative discriminant → trajectories don't intersect in 3D space
 */
float BoundingSphere::CollisionDetect( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const
{
    // Relative motion this frame (focus in target space).
    Vector3 relativeMovement = focusRay.vector3 - targetRay.vector3;
    float relativeMovementSq = Dot( relativeMovement, relativeMovement );

    if ( relativeMovementSq <= TOLERANCE * TOLERANCE )
    {
        return NO_COLLISION;
    }

    // Relative center vector from target to focus at frame start.
    Vector3 difference = focusRay.origin - targetRay.origin;
    float centerDistanceSq = Dot( difference, difference );
    float radiusSum = target.m_radius + m_radius;
    float radiusSumSq = radiusSum * radiusSum;

    // Already overlapping: skip swept solve and let static overlap handling resolve contact.
    if ( centerDistanceSq <= radiusSumSq )
    {
        return NO_COLLISION;
    }

    // If relative motion is separating, no swept impact can occur this frame.
    float closingDot = Dot( difference, relativeMovement );

    if ( closingDot >= 0.0f )
    {
        return NO_COLLISION;
    }

    // Cheap reachability cull: even if moving directly together for the whole frame,
    // they cannot reach contact distance.
    float maxTravel = sqrtf( relativeMovementSq );
    float reachRadius = radiusSum + maxTravel;

    if ( centerDistanceSq > reachRadius * reachRadius )
    {
        return NO_COLLISION;
    }

    // Solve |difference + relativeMovement * t|^2 = radiusSum^2 for earliest t in [0,1].
    float a = relativeMovementSq;
    float b = closingDot;
    float c = centerDistanceSq - radiusSumSq;
    float discriminant = b * b - a * c;

    if ( discriminant < 0.0f )
    {
        return NO_COLLISION;
    }

    return ( -b - sqrtf( discriminant ) ) / a;
}


float BoundingSphere::TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const
{
    // Public wrapper for the generic sphere-sphere variant branch. Sphere-box
    // intentionally has no matching member overload: ObjectContactManifold
    // routes both orders through its exact SweepSphereAgainstBox owner.
    return CollisionDetect( target, targetRay, focusRay );
}


float BoundingSphere::GetRadius() const
{
    return m_radius;
}


float BoundingSphere::GetBoundingRadius() const
{
    return m_radius;
}


const Vector3& BoundingSphere::GetPosition() const
{
    return m_position;
}


Matrix4 BoundingSphere::GetModelMatrix( const Vector3& worldPos, const Matrix4& rotation ) const
{
    // Builds the full TRS model matrix: T(worldPos) * rotation * T(m_position) * Scale(radius).
    // One source path and GetRenderShapeCenter's staged arithmetic own all
    // build configurations, so translation cannot drift with optimizer mode.

    // Three SSE scale passes + direct col3 write.
    //
    // Each pass loads one 4-float column of 'rotation' (16-byte-unaligned is fine with
    // _mm_loadu_ps), multiplies all 4 lanes by the broadcast scalar m_radius, and stores
    // the result directly into the output array. The translation column is then
    // evaluated from the same T(body) * R * T(localOffset) rule as Debug.
    //
    // SSE INTRINSICS USED:
    //   _mm_set1_ps(s)         — broadcast s into all 4 lanes: (s, s, s, s)
    //   _mm_loadu_ps(ptr)      — load 4 floats from unaligned memory
    //   _mm_mul_ps(a, b)       — lane-wise multiply: (a0*b0, a1*b1, a2*b2, a3*b3)
    //   _mm_storeu_ps(ptr, r)  — store 4 floats to unaligned memory

    const __m128 rr = _mm_set1_ps( m_radius ); // broadcast radius
    float res[16];

    _mm_storeu_ps( res + 0, _mm_mul_ps( _mm_loadu_ps( rotation.m + 0 ), rr ) ); // col0 * radius
    _mm_storeu_ps( res + 4, _mm_mul_ps( _mm_loadu_ps( rotation.m + 4 ), rr ) ); // col1 * radius

    _mm_storeu_ps( res + 8, _mm_mul_ps( _mm_loadu_ps( rotation.m + 8 ), rr ) ); // col2 * radius

    const Vector3 center = GetRenderShapeCenter( worldPos, rotation, m_position );
    res[12] = center.x;
    res[13] = center.y;
    res[14] = center.z;
    res[15] = 1.0f; // homogeneous w
    return Matrix4( res );
}


float BoundingSphere::GetVolume() const
{
    // m_volume of sphere = 4/3 * PI * m_radius^3
    return FOUR_OVER_THREE * _PI * m_radius * m_radius * m_radius;
}


float BoundingSphere::GetDragCoefficient() const
{
    return m_dragCoefficient;
}


void BoundingSphere::SetDragCoefficient( float dragCoefficient )
{
    m_dragCoefficient = dragCoefficient;
}


float BoundingSphere::GetProjectedSurfaceArea() const
{
    // Area of circle = PI * r^2
    return _PI * m_radius * m_radius;
}


float BoundingSphere::TestCollision( const ConvexHullShape& target, const Ray& targetRay, const Ray& focusRay ) const
{
    // Broadphase sweep only: hulls provide exact contacts later through the
    // object manifold builder, so this path stays conservative.
    float combinedRadius = m_radius + target.GetBoundingRadius();
    float combinedRadiusSq = combinedRadius * combinedRadius;

    Vector3 totalMovement = targetRay.vector3 - focusRay.vector3;
    float totalMovementSq = VectorMagSquared( totalMovement );

    if ( totalMovementSq < TOLERANCE )
    {
        Vector3 delta = ( targetRay.origin + target.GetPosition() ) - ( focusRay.origin + m_position );
        return VectorMagSquared( delta ) <= combinedRadiusSq ? 0.0f : NO_COLLISION;
    }

    Vector3 d = ( focusRay.origin + m_position ) - ( targetRay.origin + target.GetPosition() );
    float totalMovementMag = sqrtf( totalMovementSq );
    Vector3 moveDir = totalMovement / totalMovementMag;

    float dDotMoveDir = Dot( d, moveDir );
    float discriminant = dDotMoveDir * dDotMoveDir - ( VectorMagSquared( d ) - combinedRadiusSq );

    if ( discriminant < 0.0f )
    {
        return NO_COLLISION;
    }

    float t = ( dDotMoveDir - sqrtf( discriminant ) ) / totalMovementMag;
    return ( t < 0.0f || t > 1.0f ) ? NO_COLLISION : t;
}
