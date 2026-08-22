/*
File: SkullbonezSource/Physics/BoundingBox.cpp
Purpose:
  Defines oriented-box collision geometry and its broadphase/render helper math.

Summary:
  BoundingBox owns oriented-box shape facts and deterministic support, swept,
  transform, inertia, broadphase, and debug-render calculations used by
  collision and presentation.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/BoundingBox.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/

// An OBB is defined by three half-extents (half the width/height/depth).
// The actual world-space orientation is provided by the owning body row's
// quaternion; this class only stores the shape definition and provides
// volume/drag/collision queries.
//
// Inertia Tensor for a Solid Box:
//
//  For a rectangular box with half-extents (a, b, c) and mass m:
//
//    I_xx = m/12 * (4b² + 4c²)  = m/3 * (b² + c²)
//    I_yy = m/12 * (4a² + 4c²)  = m/3 * (a² + c²)
//    I_zz = m/12 * (4a² + 4b²)  = m/3 * (a² + b²)
//
//  (where a,b,c are HALF-extents, so full side = 2a etc.)
//  Equivalently with full sides (w,h,d): I_xx = m/12 * (h² + d²)
//


#include "BoundingBox.h"
#include "CollisionShape.h"
#include "../Maths/Vector3.h"
#include "BoundingSphere.h"
#include "ConvexHullShape.h"


using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Geometry;


BoundingBox::BoundingBox() : m_position( ZERO_VECTOR ), m_halfExtents( Vector3( 1.0f, 1.0f, 1.0f ) )
{
}


BoundingBox::BoundingBox( const Vector3& halfExtents, const Vector3& position )
    : m_position( position ), m_halfExtents( halfExtents )
{
}

// The box class stores only local shape data. Current world orientation lives on
// the body row, so any exact box-vs-box contact work must combine this shape
// with the body's quaternion later in the narrowphase.

// Compute model matrix: T(worldPos) * R * T(localOffset) * S(halfExtents)
// The shader renders a unit cube [-1,1]³ scaled by half-extents.
Matrix4 BoundingBox::GetModelMatrix( const Vector3& worldPos, const Matrix4& rotation ) const
{
    Matrix4 scale = Matrix4::Scale( m_halfExtents.x, m_halfExtents.y, m_halfExtents.z );
    Matrix4 model = Matrix4::Translate( worldPos ) * rotation * Matrix4::Translate( m_position ) * scale;

    // Why: compiler configurations may reassociate the matrix chain's
    // translation arithmetic differently. Replacing only translation with the
    // shared staged path keeps Debug/Profile/Release float bits identical while
    // retaining the matrix product's rotation and scale.
    const Vector3 center = GetRenderShapeCenter( worldPos, rotation, m_position );
    model.m[12] = center.x;
    model.m[13] = center.y;
    model.m[14] = center.z;
    model.m[15] = 1.0f;
    return model;
}


// Volume of a box = 8 * halfExtent.x * halfExtent.y * halfExtent.z
float BoundingBox::GetVolume() const
{
    return 8.0f * m_halfExtents.x * m_halfExtents.y * m_halfExtents.z;
}


// Simplified submersion: treat the box as a vertical slab with height
// 2*halfExtent.y.


// Drag coefficient for a cube: ~1.05 (bluff body)
float BoundingBox::GetDragCoefficient() const
{
    return 1.05f;
}


// Projected surface area: average of three face areas (crude approximation
// for an OBB at arbitrary orientation; exact would depend on orientation).
float BoundingBox::GetProjectedSurfaceArea() const
{
    float faceXY = 4.0f * m_halfExtents.x * m_halfExtents.y;
    float faceXZ = 4.0f * m_halfExtents.x * m_halfExtents.z;
    float faceYZ = 4.0f * m_halfExtents.y * m_halfExtents.z;
    return ( faceXY + faceXZ + faceYZ ) / 3.0f;
}


// Bounding radius: distance from center to corner
float BoundingBox::GetBoundingRadius() const
{
    return sqrtf( m_halfExtents.x * m_halfExtents.x + m_halfExtents.y * m_halfExtents.y +
                  m_halfExtents.z * m_halfExtents.z );
}


const Vector3& BoundingBox::GetPosition() const
{
    return m_position;
}


const Vector3& BoundingBox::GetHalfExtents() const
{
    return m_halfExtents;
}


// Concept: broadphase swept tests answer whether two moving bounds may touch;
// the narrowphase still owns exact oriented-box contact geometry.
// These tests approximate this OBB as a bounding sphere (radius = corner distance)
// for the broadphase pair check. The broadphase only needs to know "could these
// two objects possibly be touching this frame?" — a cheap sphere test is enough.
//
// Precise OBB-sphere and OBB-OBB narrowphase (SAT, contact manifold) happens in
// the terrain/impulse response layer where the full orientation is available.
//
// Both tests below solve the same swept-sphere quadratic:
//   |d + v_rel*t|² = R_combined²
//   → a·t² + 2b·t + c = 0,  t = (-b - sqrt(b²-ac)) / a
// where d is the centre-to-centre vector, v_rel is relative velocity, R_combined
// is the sum of the two bounding radii.
//


// Box vs Sphere swept test: approximate box as bounding sphere for broadphase
float BoundingBox::TestCollision( const BoundingSphere& target, const Ray& targetRay, const Ray& focusRay ) const
{
    // Cheap candidate test only. A later OBB/sphere manifold uses the real box
    // axes and closest point, so this broadphase test is allowed to be generous.
    // Approximate this box as a sphere of bounding radius for the swept test
    float combinedRadius = GetBoundingRadius() + target.GetRadius();
    float combinedRadiusSq = combinedRadius * combinedRadius;

    // Relative movement
    Vector3 totalMovement = targetRay.vector3 - focusRay.vector3;
    float totalMovementSq = VectorMagSquared( totalMovement );

    if ( totalMovementSq < TOLERANCE )
    {
        // Static overlap check
        Vector3 delta = ( targetRay.origin + target.GetPosition() ) - ( focusRay.origin + m_position );

        if ( VectorMagSquared( delta ) <= combinedRadiusSq )
        {
            return 0.0f;
        }

        return NO_COLLISION;
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

    if ( t < 0.0f || t > 1.0f )
    {
        return NO_COLLISION;
    }

    return t;
}


// Box vs Box swept test: approximate both as bounding spheres for broadphase
float BoundingBox::TestCollision( const BoundingBox& target, const Ray& targetRay, const Ray& focusRay ) const
{
    // Two oriented boxes can be expensive to sweep exactly. This conservative
    // radius test asks only whether their bounding balls could touch this tick.
    // Exact face/edge/corner contacts are built by ObjectContactManifold.cpp.
    float combinedRadius = GetBoundingRadius() + target.GetBoundingRadius();
    float combinedRadiusSq = combinedRadius * combinedRadius;

    Vector3 totalMovement = targetRay.vector3 - focusRay.vector3;
    float totalMovementSq = VectorMagSquared( totalMovement );

    if ( totalMovementSq < TOLERANCE )
    {
        Vector3 delta = ( targetRay.origin + target.GetPosition() ) - ( focusRay.origin + m_position );

        if ( VectorMagSquared( delta ) <= combinedRadiusSq )
        {
            return 0.0f;
        }

        return NO_COLLISION;
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

    if ( t < 0.0f || t > 1.0f )
    {
        return NO_COLLISION;
    }

    return t;
}


float BoundingBox::TestCollision( const ConvexHullShape& target, const Ray& targetRay, const Ray& focusRay ) const
{
    // Conservative broadphase candidate test. Exact box/hull SAT contacts are
    // generated later by ObjectContactManifold.cpp.
    float combinedRadius = GetBoundingRadius() + target.GetBoundingRadius();
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
