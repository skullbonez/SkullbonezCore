/*
File: SkullbonezSource/SkullbonezBoundingBox.cpp
Purpose:
  Defines oriented-box collision geometry and its broadphase/render helper math.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  OBB (Oriented Bounding Box): Box with rotation, used for exact object-space
  collision tests.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezBoundingBox.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// BOUNDING BOX (SkullbonezBoundingBox.cpp)
// =============================================================================
//
// PURPOSE: Oriented Bounding Box (OBB) collision shape implementation.
//
// An OBB is defined by three half-extents (half the width/height/depth).
// The actual world-space orientation is provided by the owning RigidBody's
// quaternion — this class only stores the shape definition and provides
// volume/drag/collision queries.
//
// --- Inertia Tensor for a Solid Box ---
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
// =============================================================================


#include "SkullbonezBoundingBox.h"
#include "SkullbonezVector3.h"
#include "SkullbonezBoundingSphere.h"


using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Geometry;


BoundingBox::BoundingBox()
    : m_position( ZERO_VECTOR ),
      m_halfExtents( Vector3( 1.0f, 1.0f, 1.0f ) )
{
}


BoundingBox::BoundingBox( const Vector3& halfExtents, const Vector3& position )
    : m_position( position ),
      m_halfExtents( halfExtents )
{
}

// The box class stores only local shape data. Current world orientation lives on
// the owning RigidBody/GameModel, so any exact box-vs-box contact work must
// combine this shape with the body's quaternion later in the narrowphase.

// Compute model matrix: T(worldPos) * R * S(halfExtents)
// The shader renders a unit cube [-1,1]³ scaled by half-extents.
Matrix4 BoundingBox::GetModelMatrix( const Vector3& worldPos, const Matrix4& rotation ) const
{
    Matrix4 translate = Matrix4::Translate( worldPos.x + m_position.x,
                                            worldPos.y + m_position.y,
                                            worldPos.z + m_position.z );
    Matrix4 scale = Matrix4::Scale( m_halfExtents.x, m_halfExtents.y, m_halfExtents.z );
    return translate * rotation * scale;
}


// Volume of a box = 8 * halfExtent.x * halfExtent.y * halfExtent.z
float BoundingBox::GetVolume() const
{
    return 8.0f * m_halfExtents.x * m_halfExtents.y * m_halfExtents.z;
}


// Simplified submersion: treat as a vertical slab with height 2*halfExtent.y.
// Compute fraction of that height below fluidSurfaceHeight.
float BoundingBox::GetSubmergedVolumePercent( float fluidSurfaceHeight ) const
{
    // This is a deliberately rough buoyancy approximation: it treats the box as
    // an upright vertical slab. The exact submerged volume of a rotated box would
    // need clipping against the water plane and is more expensive than this path
    // currently wants.
    float totalHeight = m_halfExtents.y * 2.0f;
    float bottom = m_position.y - m_halfExtents.y;
    float top = m_position.y + m_halfExtents.y;

    if ( fluidSurfaceHeight >= top )
    {
        return 1.0f;
    }
    if ( fluidSurfaceHeight <= bottom )
    {
        return 0.0f;
    }

    return ( fluidSurfaceHeight - bottom ) / totalHeight;
}


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
    return sqrtf( m_halfExtents.x * m_halfExtents.x +
                  m_halfExtents.y * m_halfExtents.y +
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


// =============================================================================
// BROADPHASE SWEPT COLLISION TESTS
// =============================================================================
//
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
// =============================================================================


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
        Vector3 delta = ( targetRay.origin + target.GetPosition() ) -
                        ( focusRay.origin + m_position );
        if ( VectorMagSquared( delta ) <= combinedRadiusSq )
        {
            return 0.0f;
        }
        return NO_COLLISION;
    }

    Vector3 d = ( focusRay.origin + m_position ) - ( targetRay.origin + target.GetPosition() );
    float totalMovementMag = sqrtf( totalMovementSq );
    Vector3 moveDir = totalMovement / totalMovementMag;

    float dDotMoveDir = d * moveDir;
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
        Vector3 delta = ( targetRay.origin + target.GetPosition() ) -
                        ( focusRay.origin + m_position );
        if ( VectorMagSquared( delta ) <= combinedRadiusSq )
        {
            return 0.0f;
        }
        return NO_COLLISION;
    }

    Vector3 d = ( focusRay.origin + m_position ) - ( targetRay.origin + target.GetPosition() );
    float totalMovementMag = sqrtf( totalMovementSq );
    Vector3 moveDir = totalMovement / totalMovementMag;

    float dDotMoveDir = d * moveDir;
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
