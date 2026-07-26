/*
File: SkullbonezSource/Physics/Stages/ExternalForceStage.h
Purpose:
  Defines the bounded value lane for gameplay-authored cylindrical forces.

Summary:
  Gameplay publishes ordered field records and dense per-body timer spans.
  Physics applies those values at one fixed solver position without retaining a
  content owner, callback, service, or host pointer.

Glossary:
  External field: A cylindrical acceleration primitive supplied by a higher
    owner for one fixed tick.
  Exposure: Model-row seconds accumulated while a body is affected.
  Repeat cooldown: Model-row seconds before another edge ejection may fire.

Invariants:
  - Field order, body-row order, strict strongest-field selection, and
    left-to-right floating-point accumulation are deterministic contracts.
  - Worker jobs read the same immutable field span and write disjoint body and
    timer rows.
  - Input spans are synchronous borrows and never escape ApplyBodyForces.

Related:
  - SkullbonezSource/Runtime/Scene/SceneWorld.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include "../PhysicsFixedList.h"
#include "../PhysicsRuntimeSettings.h"
#include "PhysicsSleepController.h"
#include "../../Core/SceneCapacity.h"
#include "../../Maths/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace Threading
{
class WorkerPool;
}
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsWorldForces;

struct ExternalCylindricalForceField
{
    Math::Vector::Vector3 center = Math::Vector::ZERO_VECTOR;
    float radiusMeters = 1.0f;
    float heightMeters = 1.0f;
    float inwardAccelerationMetersPerSecondSquared = 0.0f;
    float tangentialAccelerationMetersPerSecondSquared = 0.0f;
    float liftAccelerationMetersPerSecondSquared = 0.0f;
    float outwardEjectAccelerationMetersPerSecondSquared = 0.0f;
    float upwardEjectAccelerationMetersPerSecondSquared = 0.0f;
    float ejectHeightFraction = 1.0f;
    float minimumExposureSeconds = 0.0f;
    float repeatCooldownSeconds = 0.0f;
    float maxDeltaVelocityMetersPerSecond = 1.0f;
};

struct ExternalForceFrameInput
{

    // Lifetime: Gameplay owns all three spans; Physics borrows them for one
    // Step call and writes only the two model-row timer spans.
    std::span<const ExternalCylindricalForceField> fields;
    std::span<float> exposureSeconds;
    std::span<float> repeatCooldownSeconds;
    float stepSeconds = 0.0f;
    bool parallelEvaluation = false; // Gameplay-owned execution choice for this value frame.

    bool Active() const
    {
        return !fields.empty();
    }
};

class ExternalForceStage
{
  public:
    ExternalForceStage();
    void Clear();
    void ReserveBodyCapacity( std::size_t bodyCapacity );
    std::span<const int> ReleaseFixedBodies( const ExternalForceFrameInput& input, PhysicsBodyStore& bodyStore );

    // Lifetime: the concrete wake capability and worker owner are borrowed only
    // until this synchronous body partition completes.
    void ApplyBodyForces( const ExternalForceFrameInput& input, PhysicsBodyStore& bodyStore,
                          const ColliderStore& colliderStore, PhysicsNarrowphaseWakeAccess wakeAccess,
                          const PhysicsExecutionSettings& execution, Threading::WorkerPool& workerPool );
    uint64_t CollectMemoryBytes() const;

  private:
    Math::Vector::Vector3 SampleAcceleration( const ExternalForceFrameInput& input, const Math::Vector::Vector3& position,
                                              ExternalCylindricalForceField& outBestField,
                                              float& outBestAccelerationSq ) const;

    PhysicsBodyIndexList m_fixedTreeReleaseWakeScratch { "ExternalForceStage.fixedTreeReleaseWakeScratch" };
    PhysicsBodyIndexList m_releaseWakeBodies { "ExternalForceStage.releaseWakeBodies" };
};
} // namespace Physics
} // namespace SkullbonezCore
