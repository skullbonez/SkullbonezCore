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
  Body: Simulated object state such as pose, velocity, mass, and sleep flag.
  Collider: Shape and material-adjacent collision metadata paired with a body.
  Facade: Narrow public boundary that hides solver implementation containers.
  View: Immutable span-like snapshot exposed to callers without ownership.

Invariants:
  - Public API structs do not include or require GameModelCollection.
  - Descriptors describe intent; later facade code owns allocation order and
    deterministic solver mutation.
  - Views are immutable spans over API records, not mutable storage leaks.

Related:
  - SkullbonezSource/Physics/PhysicsHandles.h
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include <cstdint>

#include "CollisionShape.h"
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
    PHYSICS_BODY_UPDATE_MATERIAL_RESPONSE = 1u << 3,
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

struct PhysicsStepDesc
{
    float deltaSeconds = 0.0f;
    uint64_t frameIndex = 0;
    bool fixedStep = true;
    bool scenePhysicsEnabled = true;
};

struct PhysicsActivationCommand
{
    PhysicsActivationCommandKind kind = PhysicsActivationCommandKind::WakeBody;
    PhysicsBodyHandle body;
    bool enabled = true;
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
} // namespace Physics
} // namespace SkullbonezCore
