/*
File: SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h
Purpose:
  Owns fixed-step diagnostic, collision-visual, and bounded trace state.

Summary:
  PhysicsStepDiagnostics combines the existing cold diagnostics sink with the
  model-order collision overlay, solver debug contacts, and pipeline trace.
  PhysicsWorld supplies synchronous physics views and cold topology name
  registration but retains no diagnostic storage or mutation authority.

Glossary:
  Pipeline trace: Saturated event count plus optional ordered payload records
    describing fixed-step decisions.
  Full-record consumer: Replay, SkullScope, or pipeline presentation path that
    needs payload fields rather than only the saturated count.
  Collision visual: Per-model byte marking contact for the debug overlay.
  Step emission: Debug-only CSV and SkullScope output after solver completion.
  Name registration: Cold replacement of the fixed diagnostics presentation
    table after scene topology changes.

Invariants:
  - Trace and visual mutation occurs at the same sequencer call positions.
  - Count-only and full-record modes saturate at the same fixed ceiling.
  - Full-record mode preserves one payload row per counted event in order.
  - Debug-only output remains behind the original _DEBUG boundaries.
  - Fixed steps consume the registered name table without rebuilding it.
  - Replay copies owned rows through explicit capture and restore APIs.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp
  - SkullbonezSource/Physics/PhysicsDiagnosticsSink.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include "../PhysicsDebugData.h"
#include "../PhysicsDiagnosticsSink.h"
#include "../PhysicsSolverSnapshot.h"
#include "../PhysicsStageCapacity.h"

#include <cstdint>
#include <span>

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsDiagnosticsView;

// Concept: one bounded recorder owns both the logical pipeline-event count and
// the optional payload rows retained for an active diagnostic consumer.
//
// Invariant: Count() saturates at PHYSICS_MAX_PIPELINE_TRACE_RECORDS in both
// modes. When full records are enabled, Records().size() equals Count() and
// preserves producer order; count-only mode retains no payload rows.
class PhysicsPipelineTraceRecorder
{
  private:
    PhysicsPipelineRowList<PhysicsPipelineRecord> m_records { "PhysicsStepDiagnostics.physicsPipelineTrace",
                                                              PhysicsCapacityReason::PipelineRecords };
    uint32_t m_recordCount = 0u;
    bool m_retainFullRecords = true;

  public:
    void Reserve();
    void Clear()
    {
        m_records.clear();
        m_recordCount = 0u;
    }
    void BeginStep( bool retainFullRecords )
    {
        Clear();
        m_retainFullRecords = retainFullRecords;
    }
    void Record( const PhysicsPipelineRecord& record );

    // Count a batch of canonical events without constructing payload rows.
    // Saturation exactly matches repeated Record calls at the fixed trace cap.
    void RecordEvents( std::size_t eventCount );
    void RestoreFullRecords( std::span<const PhysicsPipelineRecord> records );
    bool CanRestoreFullRecords( std::size_t recordCount ) const noexcept;
    bool RetainsFullRecords() const;
    bool CanRecord() const;
    uint32_t Count() const;
    int RemainingRecordCapacity() const;
    std::span<const PhysicsPipelineRecord> Records() const;
    uint64_t CollectDynamicMemoryBytes() const
    {
        return static_cast<uint64_t>( m_records.capacity() ) * static_cast<uint64_t>( sizeof( PhysicsPipelineRecord ) );
    }
};

class PhysicsStepDiagnostics
{
  private:
    PhysicsBodyRowList<uint8_t> m_collisionVisualContacts { "PhysicsStepDiagnostics.collisionVisualContacts",
                                                            PhysicsCapacityReason::SceneBodies };
    bool m_collisionVisualFrameActive = false;
    PhysicsContactRowList<PhysicsDebugContact> m_physicsDebugContacts { "PhysicsStepDiagnostics.physicsDebugContacts",
                                                                        PhysicsCapacityReason::PersistentContacts };
    PhysicsPipelineTraceRecorder m_pipelineTrace;
    bool m_pipelineTraceFullRecordConsumerActive = true;
    PhysicsDiagnosticsSink m_sink;

  public:
    PhysicsStepDiagnostics();
    void ReserveSceneCapacity( std::size_t bodyCapacity );
    void Clear();
    void BeginStep( int modelCount );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void MarkCollisionVisualContact( int index );

    // Selects the next fixed step's payload policy. Counting never turns off;
    // Replay, pipeline presentation, and Debug SkullScope request full rows.
    void SetPipelineTraceFullRecordConsumerActive( bool active );
    void RecordPipelineStage( const PhysicsPipelineRecord& record );

    // Count-only producers use this batch seam after their runtime mode branch
    // has been hoisted outside the hot row loop.
    void RecordPipelineEvents( std::size_t eventCount );
    bool RetainsFullPipelineRecords() const;
    int RemainingPipelineRecordCapacity() const;
    void EmitCollisionTime( bool diagnosticsSuppressed, const char* type, int bodyA, int bodyB, float collisionTime,
                            float availableTime );

    bool ShouldEmitStepDiagnostics( bool diagnosticsSuppressed ) const;
    void SetDiagnosticNames( std::span<const char* const> diagnosticNames );
    void EmitStepDiagnostics( bool diagnosticsSuppressed, const PhysicsDiagnosticsView& diagnosticsView,
                              const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, float deltaSeconds,
                              const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
#endif

    void CaptureReplayState( PhysicsSolverSnapshot& snapshot ) const;

    // Invariant: diagnostics rows join the same all-or-nothing solver restore.
    bool CanRestoreReplayState( const PhysicsSolverSnapshot& snapshot, int modelCount ) const noexcept;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );
    std::span<const uint8_t> GetCollisionVisualContacts() const;

    // Lifetime: these mutable buffers are borrowed only by the synchronous
    // producing stage and remain capacity-governed by this diagnostics owner.
    PhysicsContactRowList<PhysicsDebugContact>& MutableDebugContacts();
    std::span<const PhysicsDebugContact> GetDebugContacts() const;
    PhysicsPipelineTraceRecorder& MutablePipelineTraceRecorder();
    uint32_t GetPipelineRecordCount() const;
    std::span<const PhysicsPipelineRecord> GetPipelineTrace() const;
    uint64_t CollectDynamicMemoryBytes() const;
    uint64_t CollectDebugMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
