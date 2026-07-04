/*
File: SkullbonezSource/Physics/PhysicsApi.h
Purpose:
  Declares the public physics command, query, and immutable-view contract.

Mental model:
  This header is the migration target for scene setup, runtime tools, replay,
  rendering, and diagnostics. It names the operations and data snapshots those
  callers should use without exposing GameModelCollection, PhysicsWorld, or
  solver-private containers.

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
  Island: Immutable solver-group summary for sleep/support diagnostics.
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
  - Public API structs do not include or require GameModelCollection.
  - Command and update descriptors target physics handles, not model indices;
    scene object ids are descriptive identity metadata, not storage offsets.
  - Descriptors describe intent; later facade code owns allocation order and
    deterministic solver mutation.
  - Standalone body views are deterministic dense-store views; collider and
    constraint views remain slot-order until their stores migrate.
  - Deletion retires body handle generations and tombstones dependent
    collider/constraint slots so stale handles fail before reuse.
  - Views are immutable spans over API records, not mutable storage leaks.

Related:
  - SkullbonezSource/Physics/PhysicsHandles.h
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "CollisionShape.h"
#include "PhysicsBodyStore.h"
#include "PhysicsHandles.h"
#include "../Maths/Matrix4.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"
#include "../Rendering/RenderMaterial.h"

namespace SkullbonezCore
{
namespace Basics
{
struct ReplaySolverWorldSnapshot;
}

namespace Physics
{
struct PhysicsDebugContact;
struct PhysicsPipelineRecord;

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
    Rendering::RenderMaterial renderMaterial;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    float mass = 1.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
    float volume = 0.0f;
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
    PhysicsBodyMotionKind motionKind = PhysicsBodyMotionKind::Dynamic;
    bool startsAsleep = false;
    bool releasesFromFixedOnContact = false;
    float contactReleaseImpulseThreshold = 0.0f;
    const char* diagnosticName = nullptr;
};

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
    Rendering::RenderMaterial renderMaterial;
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
    float projectedSurfaceArea = 0.0f;
    float dragCoefficient = 0.0f;
};

struct PhysicsColliderUpdateDesc
{
    PhysicsColliderHandle collider;
    uint32_t updateMask = PHYSICS_COLLIDER_UPDATE_NONE;
    Math::CollisionDetection::CollisionShape shape;
    float boundingRadius = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;
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
    uint32_t bodyCount = 0;
    bool sleeping = false;
    bool supported = false;
};

struct PhysicsIslandCollectionView
{
    const PhysicsIslandView* islands = nullptr;
    uint32_t islandCount = 0;
};

struct PhysicsRenderInstanceView
{
    PhysicsBodyHandle body;
    PhysicsSceneObjectId sceneObjectId;
    Math::Transformation::Matrix4 modelMatrix;
    Rendering::RenderMaterial material;
    bool fixed = false;
    float fixedContactAlpha = 0.0f;
};

struct PhysicsRenderView
{
    const PhysicsRenderInstanceView* instances = nullptr;
    uint32_t instanceCount = 0;
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
    const Basics::ReplaySolverWorldSnapshot* snapshot = nullptr;
    uint32_t modelCount = 0;
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
    bool activationCommandsPassed = false;
    bool rayCastHit = false;
    Math::Vector::Vector3 finalPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 finalLinearVelocity = Math::Vector::ZERO_VECTOR;
    bool secondaryBodyAdvanced = false;
    PhysicsBodyHandle secondaryBody;
    Math::Vector::Vector3 secondaryFinalPosition = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 secondaryFinalLinearVelocity = Math::Vector::ZERO_VECTOR;
    uint64_t deterministicHash = 0;
};

class PhysicsStandaloneWorld
{
  public:
    // Clears all bodies and advances the initial generation for future slots.
    // Existing handles become invalid even when the same slot index is reused.
    void Clear();

    // Creates one body from descriptor data without consulting GameModel or scene
    // storage. PhysicsBodyStore assigns handle identity and owns the dense
    // mutable row used by Step().
    PhysicsBodyHandle CreateBody( const PhysicsBodyCreateDesc& desc );

    // Applies masked public fields to a live body. Stale handles fail without
    // mutating any other slot.
    bool UpdateBody( const PhysicsBodyUpdateDesc& desc );

    // Tombstones a live body and its colliders, then advances generations so
    // stale handles fail.
    bool DestroyBody( PhysicsBodyHandle body );

    // Records a one-shot impulse on a live body without waking it. The next
    // standalone step consumes the impulse while walking dense body records.
    bool SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );

    // Records a one-shot impulse and wakes the body through the body store.
    // Stale handles fail before mutating any row.
    bool ApplyBodyImpulse( PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );

    // Creates a collider for a live body. Invalid or stale body handles return
    // an invalid collider handle without mutating storage; valid creation uses
    // deterministic collider slot order for later query and view output.
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
    // collider slot order and returns the closest hit. Equal-distance candidates
    // keep the earlier collider slot, making replay/debug selection stable.
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
    // is owned by this world and is invalidated by later mutation.
    const PhysicsColliderView* Collider( PhysicsColliderHandle collider ) const;

    // Returns alive colliders in deterministic slot order. The view points at
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

    // Returns immutable contact diagnostics in deterministic solver/contact
    // order. The standalone world has no narrowphase solver yet, so this is an
    // empty stable view rather than a GameModelCollection-backed leak.
    PhysicsContactCollectionView Contacts() const;

    // Returns immutable sleep/support island summaries in deterministic island
    // order. The standalone world has no island solver yet, so this is an empty
    // stable view until store-owned islands migrate behind the public API.
    PhysicsIslandCollectionView Islands() const;

  private:
    bool IsAlive( PhysicsBodyHandle body ) const;
    bool IsAlive( PhysicsColliderHandle collider ) const;
    bool IsAlive( PhysicsConstraintHandle constraint ) const;
    PhysicsBodyRecord MakeBodyRecord( const PhysicsBodyCreateDesc& desc ) const;
    PhysicsBodyRecord* MutableBodyRecord( PhysicsBodyHandle body );
    const PhysicsBodyRecord* BodyRecord( PhysicsBodyHandle body ) const;
    PhysicsBodyView MakeBodyView( const PhysicsBodyRecord& record ) const;
    void InvalidateBodyViews();
    const std::vector<PhysicsBodyView>& BodyViewCache() const;
    PhysicsColliderView MakeColliderView( const PhysicsColliderCreateDesc& desc, PhysicsColliderHandle collider ) const;
    PhysicsPointJointView MakePointJointView( const PhysicsPointJointCreateDesc& desc,
                                              PhysicsConstraintHandle constraint ) const;
    void TombstoneColliderSlot( uint32_t index );
    void TombstoneConstraintSlot( uint32_t index );

    PhysicsBodyStore m_bodyStore;                                               // Dense body records and handle generations for standalone stepping.
    mutable std::vector<PhysicsBodyView> m_bodyViewCache;                       // Cold public view cache rebuilt from bodyStore.
    mutable bool m_bodyViewCacheDirty = true;                                   // True when body records changed since the last view build.
    std::vector<PhysicsColliderView> m_colliders;                               // Slot-indexed collider records paired with body handles.
    std::vector<uint32_t> m_colliderGenerations;                                // Per-collider stale-handle counter.
    std::vector<uint8_t> m_colliderAlive;                                       // 0/1 collider liveness for compact deterministic scans.
    std::vector<uint32_t> m_freeColliderIndices;                                // Reusable tombstoned collider slots.
    mutable std::vector<PhysicsColliderView> m_colliderViewScratch;             // Filtered alive-collider view returned by Colliders().
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
