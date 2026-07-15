/*
File: SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h
Purpose:
  Owns fixed-step diagnostic, collision-visual, and bounded trace state.

Summary:
  PhysicsStepDiagnostics combines the existing cold diagnostics sink with the
  model-order collision overlay, solver debug contacts, and pipeline trace.
  PhysicsWorld supplies synchronous physics views but retains no diagnostic
  storage or mutation authority.

Glossary:
  Pipeline trace: Bounded ordered records describing fixed-step decisions.
  Collision visual: Per-model byte marking contact for the debug overlay.
  Step emission: Debug-only CSV and SkullScope output after solver completion.

Invariants:
  - Trace and visual mutation occurs at the same sequencer call positions.
  - Debug-only output remains behind the original _DEBUG boundaries.
  - Replay copies owned rows through explicit capture and restore APIs.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include "../PhysicsDebugData.h"
#include "../PhysicsDiagnosticsSink.h"
#include "../../Runtime/Replay/ReplaySolverSnapshot.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsDiagnosticsView;

class PhysicsStepDiagnostics
{
  private:
    static constexpr std::size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
    std::vector<uint8_t> m_collisionVisualContacts;
    bool m_collisionVisualFrameActive = false;
    std::vector<PhysicsDebugContact> m_physicsDebugContacts;
    std::vector<PhysicsPipelineRecord> m_physicsPipelineTrace;
    PhysicsDiagnosticsSink m_sink;

  public:
    PhysicsStepDiagnostics();
    void Clear();
    void BeginStep( int modelCount );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void MarkCollisionVisualContact( int index );
    void RecordPipelineStage( const PhysicsPipelineRecord& record );
    bool CanRecordPipelineStage() const;
    int RemainingPipelineRecordCapacity() const;
    void EmitCollisionTime( bool diagnosticsSuppressed,
                            const char* type,
                            int bodyA,
                            int bodyB,
                            float collisionTime,
                            float availableTime );

    bool ShouldEmitStepDiagnostics( bool diagnosticsSuppressed ) const;
    bool ShouldEmitCollisionTimeDiagnostics( bool diagnosticsSuppressed ) const;
    void EmitStepDiagnostics( bool diagnosticsSuppressed,
                              const PhysicsDiagnosticsView& diagnosticsView,
                              const PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              float deltaSeconds,
                              const char* const* diagnosticNames,
                              int diagnosticNameCount,
                              const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
#endif

    void CaptureReplayState( Runtime::ReplaySolverWorldSnapshot& snapshot ) const;
    void RestoreReplayState( const Runtime::ReplaySolverWorldSnapshot& snapshot );
    const std::vector<uint8_t>& GetCollisionVisualContacts() const;
    std::vector<PhysicsDebugContact>& MutableDebugContacts();
    const std::vector<PhysicsDebugContact>& GetDebugContacts() const;
    std::vector<PhysicsPipelineRecord>& MutablePipelineTrace();
    const std::vector<PhysicsPipelineRecord>& GetPipelineTrace() const;
    uint64_t CollectDynamicMemoryBytes() const;
    uint64_t CollectDebugMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
