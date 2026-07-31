/*
File: SkullbonezSource/Physics/PhysicsApi.h
Purpose:
  Declares the public physics command, query, and immutable-view contract.

Summary:
  This header names cold descriptors and query values shared by PhysicsEngine,
  scene setup, runtime tools, replay, and tests. It exposes no simulation owner
  or solver-private container.

Glossary:
  Broadphase query: Cheap spatial query that returns candidate bodies, not exact
    narrowphase contacts.
  Contact: Immutable collision-pair view exposed for diagnostics/replay without
    solver-private manifold storage.
  Constraint: Solver relationship between bodies; point joints use stable
    typed handles at the PhysicsEngine owner boundary.
  Deterministic order: Broadphase candidates iterate stable body-store order so
    validation does not depend on allocator addresses or hash traversal.
  Ray cast: Query that shoots a line segment through physics space and returns
    the closest candidate hit.
  Frame matrix: Public mapping that states whether each geometric value is
    body-local, body-to-world, body-principal, or world-space.
  View: Borrowed immutable result whose storage remains owned by PhysicsEngine.

Invariants:
  - Public API structs do not include or require runtime collection owners.
  - Command and update descriptors target physics handles, not model indices;
    scene object ids are descriptive identity metadata, not storage offsets.
  - Descriptors describe intent; PhysicsEngine owns allocation order and
    deterministic solver mutation.
  - Body pose, linear/angular velocity, ray/AABB inputs, and query hits are
    world-space. Shape offsets and joint anchors are body-local, orientation
    maps body to world, and rotational inertia is a body-principal diagonal.
  - Unproved or procedurally transformed hull geometry carries a non-shareable
    identity and therefore receives its own cold storage row.
  - Query views borrow PhysicsEngine fixed scratch and never expose mutable
    storage.

Related:
  - SkullbonezSource/Physics/PhysicsHandles.h
  - SkullbonezSource/Physics/PhysicsEngine.h
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include "CollisionShape.h"
#include "PhysicsHandles.h"
#include "../Maths/Matrix4.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
enum class PhysicsBodyMotionKind : uint8_t
{
    Dynamic,
    Fixed
};

enum PhysicsBodyUpdateMask : uint32_t
{
    PHYSICS_BODY_UPDATE_NONE = 0u,
    PHYSICS_BODY_UPDATE_POSE = 1u << 0,
    PHYSICS_BODY_UPDATE_VELOCITY = 1u << 1,
    PHYSICS_BODY_UPDATE_MASS = 1u << 2,
    PHYSICS_BODY_UPDATE_MOTION_KIND = 1u << 3,
    PHYSICS_BODY_UPDATE_SLEEP_STATE = 1u << 4,
    PHYSICS_BODY_UPDATE_RENDER_MATERIAL = 1u << 5,
    PHYSICS_BODY_UPDATE_DIAGNOSTIC_NAME = 1u << 6
};

enum PhysicsPointJointUpdateMask : uint32_t
{
    PHYSICS_POINT_JOINT_UPDATE_NONE = 0u,
    PHYSICS_POINT_JOINT_UPDATE_BODIES = 1u << 0,
    PHYSICS_POINT_JOINT_UPDATE_ANCHORS = 1u << 1,
    PHYSICS_POINT_JOINT_UPDATE_SOLVER = 1u << 2,
    PHYSICS_POINT_JOINT_UPDATE_GROUP = 1u << 3
};

struct PhysicsBodyCreateDesc
{
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShape shape;                                     // Geometry and center offset are body-local.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;                         // World-space center of mass.
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION; // Body-to-world rotation.
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;                   // World-space distance per second.
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;                  // World-space radians per second.
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;                // Body-principal diagonal.
    float mass = 1.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    float boundingRadius = 0.0f;
    float volume = 0.0f;
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
    float angularVelocityLimit = 5.0f;
    float contactEpsilon = 0.05f;
    int fixedTreeReleaseRootIndex = -1;
    PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic;
    bool startsAsleep = false;
    bool releasesFromFixedOnContact = false;
    bool usesWorldInertia = false;                                                      // Rotate world torque through the body-principal diagonal when true.
    float contactReleaseImpulseThreshold = 0.0f;
    const char* diagnosticName = nullptr;
};

inline PhysicsBodyCreateDesc
MakePhysicsBodyCreateDesc( PhysicsSceneObjectId sceneObjectId, const Math::CollisionDetection::CollisionShape& shape,
                           const Math::Vector::Vector3& position, const Math::Orientation::Quaternion& orientation,
                           const Math::Vector::Vector3& linearVelocity, const Math::Vector::Vector3& angularVelocity,
                           const Math::Vector::Vector3& rotationalInertia, float mass, float restitution,
                           PhysicsBodyMotionKind motionKind, const char* diagnosticName = nullptr )
{
    PhysicsBodyCreateDesc desc;
    desc.sceneObjectId = sceneObjectId;
    desc.shape = shape;
    desc.position = position;
    desc.orientation = orientation;
    desc.linearVelocity = linearVelocity;
    desc.angularVelocity = angularVelocity;
    desc.rotationalInertia = rotationalInertia;
    desc.mass = mass;
    desc.restitution = restitution;
    desc.boundingRadius = Math::CollisionDetection::GetShapeBoundingRadius( desc.shape );
    desc.volume = Math::CollisionDetection::GetShapeVolume( desc.shape );
    desc.projectedSurfaceArea = Math::CollisionDetection::GetShapeProjectedSurfaceArea( desc.shape );
    desc.dragCoefficient = Math::CollisionDetection::GetShapeDragCoefficient( desc.shape );

    if ( const auto* sphere = std::get_if<Math::CollisionDetection::BoundingSphere>( &desc.shape ) )
    {
        const float radius = sphere->GetRadius();
        const float radiusSq = radius * radius;

        // Invariant: sphere body descriptors must match the retired model-side
        // sphere cache multiplication order. The physics
        // regression CSV is sensitive enough to catch one-ulp volume drift.
        desc.volume = FOUR_OVER_THREE * _PI * radiusSq * radius;
        desc.projectedSurfaceArea = _PI * radiusSq;
    }

    desc.motionKind = motionKind;
    desc.usesWorldInertia = !std::holds_alternative<Math::CollisionDetection::BoundingSphere>( desc.shape );
    desc.diagnosticName = diagnosticName;
    return desc;
}

struct PhysicsBodyUpdateDesc
{
    PhysicsBodyHandle body;
    uint32_t updateMask = PHYSICS_BODY_UPDATE_NONE;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;                         // World-space center of mass.
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION; // Body-to-world rotation.
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;                   // World-space distance per second.
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;                  // World-space radians per second.
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;                // Body-principal diagonal.
    float mass = 1.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic;
    bool sleeping = false;
    const char* diagnosticName = nullptr;
};

struct HullShapeIdentity
{
    static constexpr std::size_t MAX_PATH_BYTES = 260u;

    char normalizedResolvedPath[MAX_PATH_BYTES] = {};
    uint32_t scaleXBits = 0u;
    uint32_t scaleYBits = 0u;
    uint32_t scaleZBits = 0u;
    bool shareable = false;

    // Invariant: equality proves both authored source and canonical X/Y/Z
    // scale construction. Unproved or procedurally built hulls stay unique.
    bool operator==( const HullShapeIdentity& rhs ) const
    {
        return shareable && rhs.shareable && scaleXBits == rhs.scaleXBits && scaleYBits == rhs.scaleYBits &&
               scaleZBits == rhs.scaleZBits && strcmp( normalizedResolvedPath, rhs.normalizedResolvedPath ) == 0;
    }
};

inline uint32_t HullScaleBits( float value )
{
    uint32_t bits = 0u;
    static_assert( sizeof( bits ) == sizeof( value ), "Hull identity requires 32-bit IEEE-754 floats." );
    memcpy( &bits, &value, sizeof( bits ) );
    return bits;
}

inline HullShapeIdentity MakeShareableHullShapeIdentity( const char* resolvedAuthoredPath,
                                                         const Math::Vector::Vector3& cumulativeScale )
{
    HullShapeIdentity identity;

    if ( !resolvedAuthoredPath || resolvedAuthoredPath[0] == '\0' || !std::isfinite( cumulativeScale.x ) ||
         !std::isfinite( cumulativeScale.y ) || !std::isfinite( cumulativeScale.z ) )
    {
        return identity;
    }

    std::size_t writeIndex = 0u;

    for ( const char* read = resolvedAuthoredPath; *read != '\0'; ++read )
    {

        if ( writeIndex + 1u >= HullShapeIdentity::MAX_PATH_BYTES )
        {
            return HullShapeIdentity {};
        }

        char value = *read == '\\' ? '/' : *read;

        if ( value >= 'A' && value <= 'Z' )
        {
            value = static_cast<char>( value - 'A' + 'a' );
        }

        identity.normalizedResolvedPath[writeIndex++] = value;
    }

    identity.normalizedResolvedPath[writeIndex] = '\0';
    identity.scaleXBits = HullScaleBits( cumulativeScale.x );
    identity.scaleYBits = HullScaleBits( cumulativeScale.y );
    identity.scaleZBits = HullScaleBits( cumulativeScale.z );
    identity.shareable = true;
    return identity;
}

struct PhysicsColliderCreateDesc
{
    PhysicsBodyHandle body;
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShape shape;                                     // Geometry and center offset are body-local.
    float boundingRadius = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    uint32_t contactMaterialId = 0;
    char contactMaterialName[32] = {};
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
    HullShapeIdentity hullIdentity;
};

struct PhysicsAuthoredBodyRegistration
{

    // One scene-creation commit publishes both physics rows. Either both
    // handles are valid or neither row remains live.
    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;

    bool IsValid() const
    {
        return body.IsValid() && collider.IsValid();
    }
};

// Hazard: keep the source variant borrowed and copy it explicitly into the
// large return packet. Passing both values by value made MSVC Profile builds
// default box and hull inputs back to the sphere alternative after this packet
// gained HullShapeIdentity.
inline PhysicsColliderCreateDesc MakeColliderCreateDesc( const Math::CollisionDetection::CollisionShape& shape,
                                                         float restitution, uint32_t contactMaterialId,
                                                         const char* contactMaterialName = nullptr,
                                                         HullShapeIdentity hullIdentity = {} )
{

    // Why: creation paths already know the exact primitive facts. Build the
    // collider import packet once there so PhysicsEngine owns the live row and
    // collection owners do not rediscover shape metrics on append.
    PhysicsColliderCreateDesc desc;
    desc.shape = shape;
    desc.boundingRadius = Math::CollisionDetection::GetShapeBoundingRadius( desc.shape );
    desc.restitution = restitution;
    desc.contactMaterialId = contactMaterialId;

    if ( contactMaterialName && contactMaterialName[0] != '\0' )
    {
        strncpy_s( desc.contactMaterialName, sizeof( desc.contactMaterialName ), contactMaterialName, _TRUNCATE );
    }

    desc.projectedSurfaceArea = Math::CollisionDetection::GetShapeProjectedSurfaceArea( desc.shape );
    desc.dragCoefficient = Math::CollisionDetection::GetShapeDragCoefficient( desc.shape );
    desc.hullIdentity = std::move( hullIdentity );
    return desc;
}

struct PhysicsPointJointCreateDesc
{
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;                     // Offset from body A's center in body A axes.
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;                     // Offset from body B's center in body B axes.
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;
};

struct PhysicsPointJointUpdateDesc
{
    PhysicsConstraintHandle constraint;
    uint32_t updateMask = PHYSICS_POINT_JOINT_UPDATE_NONE;
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;                     // Offset from body A's center in body A axes.
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;                     // Offset from body B's center in body B axes.
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;
};

struct PhysicsRayCastDesc
{
    Math::Vector::Vector3 origin = Math::Vector::ZERO_VECTOR;                           // World-space segment origin.
    Math::Vector::Vector3 direction = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );        // World direction; need not be unit length.
    float maxDistance = 0.0f;                                                           // World-space length after direction normalization.
    bool includeFixedBodies = true;
    bool includeSleepingBodies = true;
};

struct PhysicsRayCastHit
{
    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;
    PhysicsSceneObjectId sceneObjectId;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;                            // World-space hit point.
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;                           // Unit world-space outward normal.
    float distance = 0.0f;                                                              // World-space distance from the query origin.
    bool hit = false;
};

struct PhysicsBroadphaseCellQueryDesc
{
    Math::Vector::Vector3 min = Math::Vector::ZERO_VECTOR;                              // World-space axis-aligned lower bound.
    Math::Vector::Vector3 max = Math::Vector::ZERO_VECTOR;                              // World-space axis-aligned upper bound.
    bool includeFixedBodies = true;
    bool includeSleepingBodies = true;
};

struct PhysicsBroadphaseQueryResultView
{
    const PhysicsBodyHandle* bodies = nullptr;
    uint32_t bodyCount = 0;
};

// Borrowed descriptor-refresh inputs owned by the scene/model collection. The
// PhysicsEngine reads these spans only for the duration of the refresh call.
struct PhysicsAuthoredBodyRefreshView
{
    const PhysicsSceneObjectId* sceneObjectIds = nullptr;
    const ModelRowHint* fixedTreeReleaseRoots = nullptr;
    const char* const* diagnosticNames = nullptr;
    PhysicsAuthoredBodyCount bodyCount;
};

} // namespace Physics
} // namespace SkullbonezCore
