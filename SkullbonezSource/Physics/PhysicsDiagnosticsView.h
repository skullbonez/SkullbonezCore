/*
File: SkullbonezSource/Physics/PhysicsDiagnosticsView.h
Purpose:
  Defines the immutable solver diagnostics values published by PhysicsEngine.

Summary:
  PhysicsDiagnosticsView is the public, borrowed read model for one physics
  owner. Keeping these value records outside PhysicsWorld lets engine consumers
  inspect diagnostics without importing the solver sequencer and every stage it
  owns.

Glossary:
  Persistent contact: Solver row retained across fixed ticks for warm starting.
  Diagnostics view: Synchronous spans and references into one PhysicsEngine.
  Candidate pair: Broadphase-selected body pair awaiting narrowphase testing.

Invariants:
  - Every span and reference remains owned by the publishing PhysicsEngine and
    expires when that owner mutates or is destroyed.
  - Field order and units are validation-sensitive because tests and SkullScope
    consume these records directly.
  - This value boundary contains no PhysicsWorld or stage ownership.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.h
  - SkullbonezSource/Physics/PhysicsWorld.h
  - SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "../Maths/Vector3.h"
#include "PhysicsDebugData.h"
#include "Ragdoll.h"
#include "TerrainContactManifold.h"

namespace SkullbonezCore
{
namespace Math::CollisionDetection
{
class SpatialGrid;
}

namespace Physics
{

struct PersistentContact
{

    // One solver row for one contact point. bodyB == -1 means static terrain.
    // Accumulated impulses are cache-sensitive and validation-sensitive.
    int bodyA = -1;
    int bodyB = -1;
    uint32_t featureId = 0;
    int64_t key = 0;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    float normalMass = 0.0f;
    float tangentMass1 = 0.0f;
    float tangentMass2 = 0.0f;
    float bias = 0.0f;
    float frictionLimit = 0.0f;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
    bool warmStarted = false;
    bool isTerrain = false;
    bool supportsRestingPolicy = true;
    bool allowsTangentFriction = true;
    bool normalCoupledFriction = false;
    bool inhibitsSleep = false;
    uint8_t manifoldPointCount = 1;
    Math::Vector::Vector3 terrainNormal = Math::Vector::ZERO_VECTOR;
    float terrainWarmStart = 0.0f;

    // Captured before impulses so diagnostics can reject force-transfer rows
    // that had no actual relative impact motion.
    float preSolveNormalSpeed = 0.0f;
    float preSolveClosingSpeed = 0.0f;
    float preSolveSlipSpeed = 0.0f;
};

struct PersistentContactSolverStats
{
    int rowCount = 0;
    int cachePreviousRows = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int warmStartedRows = 0;
    int positionCorrectionRows = 0;
    int solverIterations = 0;
    float positionCorrectionTotal = 0.0f;
    float positionCorrectionMax = 0.0f;
};

struct PhysicsDiagnosticsView
{
    std::span<const PersistentContact> persistentContacts;
    const PersistentContactSolverStats& persistentContactSolverStats;
    std::span<const int> sleepIslandParent;
    std::span<const uint8_t> sleepSupportedThisFrame;
    std::span<const uint8_t> sleepInhibitedThisFrame;
    std::span<const uint8_t> sleepState;
    std::span<const uint8_t> sleepCounter;
    std::span<const uint8_t> sleepIslandEligible;
    std::span<const uint8_t> sleepIslandCanSleep;
    std::span<const PointJointConstraint> pointJointConstraints;
    const Math::CollisionDetection::SpatialGrid& spatialGrid;
    std::span<const std::pair<int, int>> candidatePairs;
    std::span<const int64_t> collisionCellKeys;
    std::span<const std::pair<int, int>> sleepSupportEdges;
    std::span<const int> sleepIslandVisualId;
    std::span<const PhysicsPipelineRecord> physicsPipelineTrace;
    std::span<const TerrainContactManifold> terrainContactManifolds;
};

} // namespace Physics
} // namespace SkullbonezCore
