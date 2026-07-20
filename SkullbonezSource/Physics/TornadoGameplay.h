/*
File: SkullbonezSource/Physics/TornadoGameplay.h
Purpose:
  Owns tornado gameplay state and applies tornado-driven body effects.

Summary:
  Tornado gameplay is a deterministic force/ejection layer that runs before
  broadphase. A narrow value capability performs immediate sleep wake-up while
  this component owns tornado configs, capture timers, cooldown timers, and the
  release decisions that produce an ordered wake list.

Glossary:
  Capture timer: Per-body time spent inside an active tornado before an eject
    slot is allowed to fire.
  Eject cooldown: Per-body delay after an ejection impulse before another eject
    impulse can fire.
  Fixed-tree release: Authored fixed props that become dynamic when tornado
    acceleration exceeds their release threshold.
  Step state: Small value record computed once per fixed tick after advancing
    the tornado system clock.

Invariants:
  - Release wake bodies are emitted in deterministic source/body order; callers
    must apply wake propagation before running the per-body tornado force pass.
  - Capture and cooldown arrays are model-indexed and must be sized to the live
    solver model count before per-body force application.
  - The force context borrows a scoped wake capability, never a concrete sleep
    owner or PhysicsWorld reference.

Related:
  - SkullbonezSource/Physics/TornadoGameplay.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include "TornadoField.h"
#include "PhysicsRuntimeSettings.h"
#include "Stages/PhysicsSleepController.h"

#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
} // namespace Runtime

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsWorldForces;

struct TornadoGameplayStepState
{
    float stepSeconds = 0.0f;
    bool useSystem = false;
    bool active = false;
};

struct TornadoBodyForceContext
{
    // Lifetime: all references and the wake capability are borrowed from
    // PhysicsWorld::RunSolverPhysics for one fixed tick and are never retained.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    PhysicsNarrowphaseWakeAccess wakeAccess;
    std::span<const uint8_t> sleepState;
    std::span<float> timeRemaining;
    std::span<const uint8_t> underwaterSleepLocked;
    float dt = 0.0f;
    const PhysicsExecutionSettings& execution;
    Threading::WorkerPool& workerPool;
    int minParallelBodies = 0;
    const char* workerMarkerPath = nullptr;
    uint32_t workerMarkerHash = 0;
};

class TornadoGameplay
{
  public:
    TornadoGameplay();

    void ReserveBodyCapacity( int capacity );
    void Clear();

    void SetFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetFieldConfig() const;
    void SetSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetSystemConfig() const;
    float GetSystemElapsedSeconds() const;
    void SetReplayState( const std::vector<float>& captureSeconds,
                         const std::vector<float>& ejectCooldownSeconds,
                         const TornadoFieldConfig& fieldConfig,
                         const TornadoSystemConfig& systemConfig,
                         float systemElapsedSeconds );

    const std::vector<float>& CaptureSeconds() const;
    const std::vector<float>& EjectCooldownSeconds() const;

    TornadoGameplayStepState BeginStep( float dt );
    const std::vector<int>& ReleaseFixedBodies( const TornadoGameplayStepState& stepState,
                                                PhysicsBodyStore& bodyStore );
    void ApplyBodyForces( const TornadoGameplayStepState& stepState, const TornadoBodyForceContext& context );

    uint64_t CollectMemoryBytes() const;
    uint64_t CollectDebugMemoryBytes() const;

  private:
    Math::Vector::Vector3 SampleAcceleration( const TornadoGameplayStepState& stepState,
                                              const Math::Vector::Vector3& position,
                                              TornadoFieldConfig& outBestConfig,
                                              float& outBestAccelerationSq ) const;
    void EnsureStateBuffers( int modelCount );

    TornadoField m_field;
    TornadoSystem m_system;
    std::vector<float> m_captureSeconds;
    std::vector<float> m_ejectCooldownSeconds;
    std::vector<int> m_fixedTreeReleaseWakeScratch;
    std::vector<int> m_releaseWakeBodies;
};
} // namespace Physics
} // namespace SkullbonezCore
