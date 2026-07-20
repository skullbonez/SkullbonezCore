/*
File: SkullbonezSource/Physics/PhysicsApi.h
Purpose:
  Declares the public physics command, query, and immutable-view contract.

Summary:
  This header is the migration target for scene setup, runtime tools, replay,
  rendering, and diagnostics. It names the operations and data snapshots those
  callers should use without exposing runtime collection owners, PhysicsWorld,
  or solver-private containers.

Glossary:
  Activation command: Handle-based request to wake a body, seed it asleep, or
    toggle the standalone world's sleep gate.
  AABB (Axis-Aligned Bounding Box): Query box aligned to world X/Y/Z axes.
  Body: Simulated object state such as pose, velocity, mass, and sleep flag.
  Broadphase query: Cheap spatial query that returns candidate bodies, not exact
    narrowphase contacts.
  Collider: Shape and material-adjacent collision metadata paired with a body.
  Contact: Immutable collision-pair view exposed for diagnostics/replay without
    solver-private manifold storage.
  Constraint: Solver relationship between bodies; the standalone API currently
    stores point joints as constraint-handle records.
  Deterministic order: Public collection views and broadphase candidates
    iterate stable store order; smoke hashes derive from stable handle
    assignment so replay/debug evidence does not depend on allocator addresses
    or STL traversal accidents.
  Facade: Narrow public boundary that hides solver implementation containers.
  Feature id: Deterministic contact key for one shape-pair feature in a public
    contact view; replay and smoke hashes use it as diagnostic identity.
  Friction: Sliding resistance copied from collider material data into contact
    views for diagnostics and future solver inputs.
  Island: Immutable solver-group summary for sleep/support diagnostics.
  Restitution: Bounce response copied from collider material data into contact
    views for diagnostics and future solver inputs.
  Ray cast: Query that shoots a line segment through physics space and returns
    the closest candidate hit.
  Sleep: Optional optimization that skips integration for quiet dynamic bodies
    while the world sleep gate is enabled.
  STL (Standard Template Library): C++ library containers and algorithms.
  Wake: Clearing a body's sleep flag so later steps can integrate it.
  Standalone world: Public physics owner that can step without runtime or
    game-object storage.
  View: Immutable span-like snapshot exposed to callers without ownership.

Invariants:
  - Public API structs do not include or require runtime collection owners.
  - Command and update descriptors target physics handles, not model indices;
    scene object ids are descriptive identity metadata, not storage offsets.
  - Descriptors describe intent; later facade code owns allocation order and
    deterministic solver mutation.
  - Standalone body and collider views are deterministic dense-store views;
    constraint views remain slot-order until their store migrates.
  - Deletion retires body handle generations and tombstones dependent
    collider/constraint slots so stale handles fail before reuse.
  - Views are immutable spans over API records, not mutable storage leaks.

Related:
  - SkullbonezSource/Physics/PhysicsHandles.h
  - SkullbonezSource/Physics/PhysicsEngine.h
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "ColliderStore.h"
#include "CollisionShape.h"
#include "PhysicsBodyStore.h"
#include "PhysicsHandles.h"
#include "../Maths/Matrix4.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsDebugContact;
struct PhysicsPipelineRecord;
struct PhysicsSolverSnapshot;

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

enum PhysicsColliderUpdateMask : uint32_t
{
    PHYSICS_COLLIDER_UPDATE_NONE = 0u,
    PHYSICS_COLLIDER_UPDATE_SHAPE = 1u << 0,
    PHYSICS_COLLIDER_UPDATE_RESPONSE = 1u << 1,
    PHYSICS_COLLIDER_UPDATE_BROADPHASE = 1u << 2
};

enum PhysicsPointJointUpdateMask : uint32_t
{
    PHYSICS_POINT_JOINT_UPDATE_NONE = 0u,
    PHYSICS_POINT_JOINT_UPDATE_BODIES = 1u << 0,
    PHYSICS_POINT_JOINT_UPDATE_ANCHORS = 1u << 1,
    PHYSICS_POINT_JOINT_UPDATE_SOLVER = 1u << 2,
    PHYSICS_POINT_JOINT_UPDATE_GROUP = 1u << 3
};

enum class PhysicsActivationCommandKind : uint8_t
{
    WakeBody,
    SeedBodyAsleep,
    SetSleepEnabled
};

struct PhysicsBodyCreateDesc
{
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShape shape;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
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
    bool usesWorldInertia = false;
    float contactReleaseImpulseThreshold = 0.0f;
    Geometry::Terrain* terrain = nullptr;
    const char* diagnosticName = nullptr;
};

inline PhysicsBodyCreateDesc MakePhysicsBodyCreateDesc( PhysicsSceneObjectId sceneObjectId,
                                                        const Math::CollisionDetection::CollisionShape& shape,
                                                        const Math::Vector::Vector3& position,
                                                        const Math::Orientation::Quaternion& orientation,
                                                        const Math::Vector::Vector3& linearVelocity,
                                                        const Math::Vector::Vector3& angularVelocity,
                                                        const Math::Vector::Vector3& rotationalInertia,
                                                        float mass,
                                                        float restitution,
                                                        PhysicsBodyMotionKind motionKind,
                                                        Geometry::Terrain* terrain,
                                                        const char* diagnosticName = nullptr )
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
        // Invariant: sphere body descriptors must match the retired
        // retired model-side sphere cache multiplication order. The physics
        // regression CSV is sensitive enough to catch one-ulp volume drift.
        desc.volume = FOUR_OVER_THREE * _PI * radiusSq * radius;
        desc.projectedSurfaceArea = _PI * radiusSq;
    }
    desc.motionKind = motionKind;
    desc.usesWorldInertia = !std::holds_alternative<Math::CollisionDetection::BoundingSphere>( desc.shape );
    desc.terrain = terrain;
    desc.diagnosticName = diagnosticName;
    return desc;
}

struct PhysicsBodyUpdateDesc
{
    PhysicsBodyHandle body;
    uint32_t updateMask = PHYSICS_BODY_UPDATE_NONE;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    float mass = 1.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic;
    bool sleeping = false;
    const char* diagnosticName = nullptr;
};

struct PhysicsColliderCreateDesc
{
    PhysicsBodyHandle body;
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShape shape;
    float boundingRadius = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    uint32_t contactMaterialId = 0;
    char contactMaterialName[32] = {};
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
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

inline PhysicsColliderCreateDesc MakeColliderCreateDesc( Math::CollisionDetection::CollisionShape shape,
                                                         float restitution,
                                                         uint32_t contactMaterialId,
                                                         const char* contactMaterialName = nullptr )
{
    // Why: creation paths already know the exact primitive facts. Build the
    // collider import packet once there so PhysicsEngine owns the live row and
    // collection owners do not rediscover shape metrics on append.
    PhysicsColliderCreateDesc desc;
    desc.shape = std::move( shape );
    desc.boundingRadius = Math::CollisionDetection::GetShapeBoundingRadius( desc.shape );
    desc.restitution = restitution;
    desc.contactMaterialId = contactMaterialId;
    if ( contactMaterialName && contactMaterialName[0] != '\0' )
    {
        strncpy_s( desc.contactMaterialName, sizeof( desc.contactMaterialName ), contactMaterialName, _TRUNCATE );
    }
    desc.projectedSurfaceArea = Math::CollisionDetection::GetShapeProjectedSurfaceArea( desc.shape );
    desc.dragCoefficient = Math::CollisionDetection::GetShapeDragCoefficient( desc.shape );
    return desc;
}

struct PhysicsColliderUpdateDesc
{
    PhysicsColliderHandle collider;
    uint32_t updateMask = PHYSICS_COLLIDER_UPDATE_NONE;
    Math::CollisionDetection::CollisionShape shape;
    float boundingRadius = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    uint32_t contactMaterialId = 0;
    char contactMaterialName[32] = {};
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
};

struct PhysicsPointJointCreateDesc
{
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
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
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;
};

struct PhysicsStandaloneStepDesc
{
    float deltaSeconds = 0.0f;                                                  // Seconds to integrate; negative values are invalid.
    uint64_t frameIndex = 0;                                                    // Deterministic caller frame id for traceable samples.
    bool fixedStep = true;                                                      // True when deltaSeconds comes from a fixed tick schedule.
    // m/s^2-style acceleration applied to awake dynamic bodies.
    Math::Vector::Vector3 worldLinearAcceleration = Math::Vector::ZERO_VECTOR;
    bool scenePhysicsEnabled = true;                                            // False means the step is a no-op, not an error.
};

struct PhysicsActivationCommand
{
    PhysicsActivationCommandKind kind = PhysicsActivationCommandKind::WakeBody; // Operation selector.
    PhysicsBodyHandle body;                                                     // Target for body commands; ignored by SetSleepEnabled.
    bool enabled = true;                                                        // Desired sleep-gate value for SetSleepEnabled.
};

struct PhysicsRayCastDesc
{
    Math::Vector::Vector3 origin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 direction = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    float maxDistance = 0.0f;
    bool includeFixedBodies = true;
    bool includeSleepingBodies = true;
};

struct PhysicsRayCastHit
{
    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;
    PhysicsSceneObjectId sceneObjectId;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    float distance = 0.0f;
    bool hit = false;
};

struct PhysicsBroadphaseCellQueryDesc
{
    Math::Vector::Vector3 min = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 max = Math::Vector::ZERO_VECTOR;
    bool includeFixedBodies = true;
    bool includeSleepingBodies = true;
};

struct PhysicsBroadphaseQueryResultView
{
    const PhysicsBodyHandle* bodies = nullptr;
    uint32_t bodyCount = 0;
};

struct PhysicsBodyView
{
    PhysicsBodyHandle body;
    PhysicsSceneObjectId sceneObjectId;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 inverseRotationalInertia = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;
    float inverseMass = 0.0f;
    float boundingRadius = 0.0f;
    PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic;
    bool sleeping = false;
    bool sleepSupported = false;
    bool sleepInhibited = false;
};

struct PhysicsBodyCollectionView
{
    const PhysicsBodyView* bodies = nullptr;
    uint32_t bodyCount = 0;
};

struct PhysicsColliderView
{
    PhysicsColliderHandle collider;
    PhysicsBodyHandle body;
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShape shape;
    float boundingRadius = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    uint32_t contactMaterialId = 0;
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
};

struct PhysicsColliderCollectionView
{
    const PhysicsColliderView* colliders = nullptr;
    uint32_t colliderCount = 0;
};

struct PhysicsPointJointView
{
    PhysicsConstraintHandle constraint;
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;
};

struct PhysicsPointJointCollectionView
{
    const PhysicsPointJointView* pointJoints = nullptr;
    uint32_t pointJointCount = 0;
};

struct PhysicsContactView
{
    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    PhysicsColliderHandle colliderA;
    PhysicsColliderHandle colliderB;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    float penetrationDepth = 0.0f;
    float normalImpulse = 0.0f;
    float restitutionA = 0.0f;
    float restitutionB = 0.0f;
    float frictionA = 0.0f;
    float frictionB = 0.0f;
    uint32_t contactMaterialAId = 0;
    uint32_t contactMaterialBId = 0;
    uint32_t featureId = 0;
    bool touching = false;
};

struct PhysicsContactCollectionView
{
    const PhysicsContactView* contacts = nullptr;
    uint32_t contactCount = 0;
};

struct PhysicsIslandView
{
    uint32_t islandId = 0;
    const PhysicsBodyHandle* bodies = nullptr;
    uint32_t bodyCount = 0;
    bool sleeping = false;
    bool supported = false;
};

struct PhysicsIslandCollectionView
{
    const PhysicsIslandView* islands = nullptr;
    uint32_t islandCount = 0;
};

// Borrowed descriptor-refresh inputs owned by the scene/model collection. The
// physics facade reads these spans only for the duration of the refresh call.
struct PhysicsAuthoredBodyRefreshView
{
    const uint32_t* replayBodyIds = nullptr;
    const ModelRowHint* fixedTreeReleaseRoots = nullptr;
    const char* const* diagnosticNames = nullptr;
    PhysicsAuthoredBodyCount bodyCount;
};

struct PhysicsDiagnosticsSnapshot
{
    const int64_t* collisionCellKeys = nullptr;
    uint32_t collisionCellKeyCount = 0;
    const uint8_t* collisionVisualContacts = nullptr;
    uint32_t collisionVisualContactCount = 0;
    const uint8_t* sleepStates = nullptr;
    uint32_t sleepStateCount = 0;
    const int* sleepIslandVisualIds = nullptr;
    uint32_t sleepIslandVisualIdCount = 0;
    const uint8_t* sleepSupportedStates = nullptr;
    uint32_t sleepSupportedStateCount = 0;
    const uint8_t* sleepInhibitedStates = nullptr;
    uint32_t sleepInhibitedStateCount = 0;
    const PhysicsDebugContact* debugContacts = nullptr;
    uint32_t debugContactCount = 0;
    const PhysicsPipelineRecord* pipelineRecords = nullptr;
    uint32_t pipelineRecordCount = 0;
};

struct PhysicsReplaySolverSnapshotView
{
    const PhysicsSolverSnapshot* snapshot = nullptr;
    PhysicsBodyCount bodyCount;
};

struct PhysicsStandaloneSmokeResult
{
    bool passed = false;
    bool lifecycleChecksPassed = false;
    PhysicsBodyHandle body;
    PhysicsColliderHandle collider;
    PhysicsConstraintHandle constraint;
    uint32_t bodyCount = 0;
    uint32_t colliderCount = 0;
    uint32_t pointJointCount = 0;
    uint32_t contactCount = 0;
    uint32_t islandCount = 0;
    uint32_t broadphaseQueryCount = 0;
    uint32_t stepCount = 0;
    uint64_t islandHash = 0;
    bool activationCommandsPassed = false;
    bool rayCastHit = false;
    Math::Vector::Vector3 finalPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 finalLinearVelocity = Math::Vector::ZERO_VECTOR;
    bool secondaryBodyAdvanced = false;
    PhysicsBodyHandle secondaryBody;
    Math::Vector::Vector3 secondaryFinalPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 secondaryFinalLinearVelocity = Math::Vector::ZERO_VECTOR;
    uint64_t contactHash = 0;
    uint64_t deterministicHash = 0;
};

class PhysicsStandaloneWorld
{
  public:
    // Clears all bodies and advances the initial generation for future slots.
    // Existing handles become invalid even when the same slot index is reused.
    void Clear();

    // Creates one body from descriptor data without consulting scene storage.
    // PhysicsBodyStore assigns handle identity and owns the dense mutable row
    // used by Step().
    PhysicsBodyHandle CreateBody( const PhysicsBodyCreateDesc& desc );

    // Applies masked public fields to a live body. Stale handles fail without
    // mutating any other slot.
    bool UpdateBody( const PhysicsBodyUpdateDesc& desc );

    // Tombstones a live body and its colliders, then advances generations so
    // stale handles fail.
    bool DestroyBody( PhysicsBodyHandle body );

    // Records a one-shot impulse on a live body without waking it. The next
    // standalone step consumes the impulse while walking dense hot-field rows.
    bool SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );

    // Records a one-shot impulse and wakes the body through the body store.
    // Stale handles fail before mutating any row.
    bool ApplyBodyImpulse( PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );

    // Creates a collider record for a live body. Invalid or stale body handles
    // return an invalid collider handle without mutating storage; valid creation
    // appends to the dense collider store used by later query and view output.
    PhysicsColliderHandle CreateCollider( const PhysicsColliderCreateDesc& desc );

    // Applies masked public fields to a live collider. Stale handles fail
    // without mutating any other slot.
    bool UpdateCollider( const PhysicsColliderUpdateDesc& desc );

    // Tombstones a live collider and advances its generation so stale handles fail.
    bool DestroyCollider( PhysicsColliderHandle collider );

    // Creates a point joint between two live bodies. Invalid or stale body
    // handles return an invalid constraint handle without mutating storage;
    // valid creation uses deterministic constraint slot order.
    PhysicsConstraintHandle CreatePointJoint( const PhysicsPointJointCreateDesc& desc );

    // Applies masked public fields to a live point joint. Stale handles fail
    // without mutating any other slot.
    bool UpdatePointJoint( const PhysicsPointJointUpdateDesc& desc );

    // Tombstones a live constraint and advances its generation so stale handles fail.
    bool DestroyConstraint( PhysicsConstraintHandle constraint );

    // Advances awake dynamic bodies by one deterministic semi-implicit Euler step.
    bool Step( const PhysicsStandaloneStepDesc& desc );

    // Applies a handle-based wake/sleep command without model-index lookup.
    // Fixed, stale, or invalid body targets fail for body commands without
    // mutating storage. SetSleepEnabled ignores the body field and uses enabled.
    bool ApplyActivationCommand( const PhysicsActivationCommand& command );

    // Returns the standalone world's sleep gate. Disabling sleep wakes all
    // bodies and makes create/update/query/step paths treat body sleep flags as
    // inactive until the gate is re-enabled.
    bool SleepEnabled() const;

    // Conservatively ray-casts live collider bounding spheres in deterministic
    // collider-store order and returns the closest hit. Equal-distance
    // candidates keep the earlier collider row, making replay/debug selection stable.
    PhysicsRayCastHit RayCast( const PhysicsRayCastDesc& desc ) const;

    // Conservatively returns live bodies whose body or collider bounding sphere
    // overlaps the query AABB, in deterministic body slot order. The view
    // points at internal scratch storage and is valid until the next
    // QueryBroadphaseCells() call or non-const world mutation.
    PhysicsBroadphaseQueryResultView QueryBroadphaseCells( const PhysicsBroadphaseCellQueryDesc& desc ) const;

    // Returns a live body view, or null for stale/dead handles. The pointer is
    // owned by this world and is invalidated by the next Body() call or mutation.
    const PhysicsBodyView* Body( PhysicsBodyHandle body ) const;

    // Returns alive bodies in deterministic dense-store order. This is the
    // public body ordering for future replay snapshots and count/query smoke evidence.
    // The view points at internal scratch storage and is valid until the next
    // Bodies() call or non-const world mutation.
    PhysicsBodyCollectionView Bodies() const;

    // Returns a live collider view, or null for stale/dead handles. The pointer
    // is owned by this world and is invalidated by the next Collider() call,
    // Colliders() call, or non-const world mutation.
    const PhysicsColliderView* Collider( PhysicsColliderHandle collider ) const;

    // Returns alive colliders in deterministic dense-store order. The view points at
    // internal scratch storage and is valid until the next Colliders() call or
    // non-const world mutation.
    PhysicsColliderCollectionView Colliders() const;

    // Returns a live point-joint view, or null for stale/dead handles. The
    // pointer is owned by this world and is invalidated by later mutation.
    const PhysicsPointJointView* PointJoint( PhysicsConstraintHandle constraint ) const;

    // Returns alive point joints in deterministic slot order. The view points at
    // internal scratch storage and is valid until the next PointJoints() call or
    // non-const world mutation.
    PhysicsPointJointCollectionView PointJoints() const;

    // Returns immutable contact diagnostics from the most recent standalone
    // Step() in deterministic collider-pair order. The view points at internal
    // contact storage and is valid until the next non-const world mutation.
    PhysicsContactCollectionView Contacts() const;

    // Returns immutable sleep/support island summaries in deterministic island
    // order. The standalone world has no island solver yet, so this is an empty
    // stable view until store-owned islands migrate behind the public API.
    PhysicsIslandCollectionView Islands() const;

  private:
    bool IsAlive( PhysicsBodyHandle body ) const;
    bool IsAlive( PhysicsColliderHandle collider ) const;
    bool IsAlive( PhysicsConstraintHandle constraint ) const;
    PhysicsBodyRecord* MutableBodyRecord( PhysicsBodyHandle body );
    const PhysicsBodyRecord* BodyRecord( PhysicsBodyHandle body ) const;
    PhysicsBodyView MakeBodyView( const PhysicsBodyRecord& record, std::size_t bodyIndex ) const;
    void InvalidateBodyViews();
    const std::vector<PhysicsBodyView>& BodyViewCache() const;
    ColliderRecord MakeColliderRecord( const PhysicsColliderCreateDesc& desc ) const;
    PhysicsColliderView MakeColliderView( const ColliderRecord& record ) const;
    PhysicsPointJointView MakePointJointView( const PhysicsPointJointCreateDesc& desc,
                                              PhysicsConstraintHandle constraint ) const;
    void TombstoneConstraintSlot( uint32_t index );
    void ClearContacts();
    void ClearIslands();
    void GenerateStandaloneContacts();
    void GenerateStandaloneIslands();
    bool TryAppendSphereSphereContact( const ColliderRecord& colliderA,
                                       std::size_t bodyAIndex,
                                       const ColliderRecord& colliderB,
                                       std::size_t bodyBIndex );
    bool TryAppendSphereBoxContact( const ColliderRecord& colliderA,
                                    std::size_t bodyAIndex,
                                    const ColliderRecord& colliderB,
                                    std::size_t bodyBIndex );

    PhysicsBodyStore m_bodyStore;                                               // Dense cold/hot body rows plus handle generations for standalone stepping.
    mutable std::vector<PhysicsBodyView> m_bodyViewCache;                       // Cold public view cache rebuilt from bodyStore.
    mutable bool m_bodyViewCacheDirty = true;                                   // True when body rows changed since the last view build.
    ColliderStore m_colliderStore;                                              // Dense collider records and handle generations for standalone queries.
    mutable PhysicsColliderView m_singleColliderViewScratch;                    // Cold single-collider projection returned by Collider().
    mutable std::vector<PhysicsColliderView> m_colliderViewScratch;             // Filtered collider view returned by Colliders().
    std::vector<PhysicsContactView> m_contacts;                                 // Rebuilt contact rows from standalone collider/body stores.
    std::vector<PhysicsIslandView> m_islands;                                   // Rebuilt island rows from standalone contacts/constraints.
    std::vector<PhysicsBodyHandle> m_islandBodyScratch;                         // Flat immutable body spans referenced by island rows.
    std::vector<uint32_t> m_islandParentScratch;                                // Union-find parent rows for island generation.
    std::vector<uint32_t> m_islandRankScratch;                                  // Union-find rank rows for deterministic merges.
    std::vector<uint32_t> m_islandHandleRowScratch;                             // Handle-index to body-row lookup during island generation.
    std::vector<uint32_t> m_islandRootSlotScratch;                              // Root-row to public island-row lookup.
    std::vector<uint32_t> m_islandBodyOffsetScratch;                            // Per-island offset into m_islandBodyScratch.
    std::vector<PhysicsPointJointView> m_pointJoints;                           // Slot-indexed public constraint records.
    std::vector<uint32_t> m_constraintGenerations;                              // Per-constraint stale-handle counter.
    std::vector<uint8_t> m_constraintAlive;                                     // 0/1 constraint liveness for deterministic scans.
    std::vector<uint32_t> m_freeConstraintIndices;                              // Reusable tombstoned constraint slots.
    mutable std::vector<PhysicsPointJointView>
        m_pointJointViewScratch;                                                // Filtered alive-point-joint view returned by PointJoints().
    mutable std::vector<PhysicsBodyHandle>
        m_broadphaseQueryScratch;                                               // Filtered body handles returned by broadphase queries.
    uint32_t m_nextInitialGeneration = PHYSICS_HANDLE_INITIAL_GENERATION;       // Generation base after Clear().
    bool m_sleepEnabled = true;                                                 // Standalone sleep gate controlled by activation commands.
};

PhysicsStandaloneSmokeResult RunPhysicsStandaloneSmoke();
} // namespace Physics
} // namespace SkullbonezCore
