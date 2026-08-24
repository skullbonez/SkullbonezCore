/*
File: SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h
Purpose:
  Enforces replay restore phase order while retaining only detached values.

Summary:
  ReplayRestoreTransaction owns the restore cursor, live backup, result, and
  branch-reset values needed across synchronous restore phases. Runtime owners
  enter individual phase calls as borrows and are never retained here.

Glossary:
  Live backup: Detached solver sample captured before any restore mutation.
  Topology restore: Cold scene rebuild needed when an artifact's body layout
    differs from the current generated scene.

Invariants:
  - The normal walk is select, backup, topology, checkpoint, step, verify,
    complete; illegal transitions are fatal invariant.
  - Failure before mutation ends in Failed; failure after a live backup and
    possible mutation ends only after rollback.
  - No owner pointer, reference, callback, or service bundle is retained;
    Debug diagnostics copy bounded text before synchronous App publication.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/App/ReplayRestoreOperations.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayCoordination.h"
#include "ReplayRecorder.h"
#include "ReplayScrubber.h"
#include "../../Core/FatalError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
// Detached restore evidence retained by Replay until App publishes it to the
// diagnostics sibling. String pointers are copied into transaction storage.
struct ReplayRestoreProbePacket
{
    uint64_t targetReplayFrame = 0;
    int targetSceneFrame = 0;
    uint64_t targetSolverHash = 0;
    uint64_t targetPresentationHash = 0;
    std::size_t targetBodyCount = 0;
    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
    bool hashCaptured = false;
    bool hashMatched = false;
    bool fallbackAttempted = false;
    bool fallbackRestored = false;
};

struct ReplayRestoreResultPacket
{
    const char* restoreSource = nullptr;
    uint64_t targetReplayFrame = 0;
    int targetSceneFrame = 0;
    uint64_t checkpointReplayFrame = 0;
    uint64_t targetSolverHash = 0;
    uint64_t targetPresentationHash = 0;
    std::size_t targetBodyCount = 0;
    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    uint16_t contactCount = 0;
    uint16_t pipelineRecordCount = 0;
    bool checkpointBoundary = false;
    bool hashCaptured = false;
    bool hashMatched = false;
    bool fallbackAttempted = false;
    bool fallbackRestored = false;
    const char* failureReason = nullptr;
};

class ReplayRestorePhaseCursor
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        ArtifactSelected,
        LiveBackupCaptured,
        TopologyPrepared,
        CheckpointApplied,
        TargetStepped,
        TargetVerified,
        TimelineResetApplied,
        Complete,
        Failed,
        RolledBack,
        Count
    };

    static constexpr bool IsLegalTransition( Phase from, Phase to )
    {
        const bool adjacent = ( from == Phase::Idle && to == Phase::ArtifactSelected ) ||
                              ( from == Phase::ArtifactSelected && to == Phase::LiveBackupCaptured ) ||
                              ( from == Phase::LiveBackupCaptured && to == Phase::TopologyPrepared ) ||
                              ( from == Phase::TopologyPrepared && to == Phase::CheckpointApplied ) ||
                              ( from == Phase::CheckpointApplied && to == Phase::TargetStepped ) ||
                              ( from == Phase::TargetStepped && to == Phase::TargetVerified );
        const bool completion = ( from == Phase::TargetVerified || from == Phase::TimelineResetApplied ) &&
                                to == Phase::Complete;
        const bool timelineReset = from == Phase::TargetVerified && to == Phase::TimelineResetApplied;
        const bool preMutationFailure = ( from == Phase::Idle || from == Phase::ArtifactSelected ||
                                          from == Phase::LiveBackupCaptured || from == Phase::TopologyPrepared ) &&
                                        to == Phase::Failed;
        const bool rollback = ( from == Phase::LiveBackupCaptured || from == Phase::TopologyPrepared ||
                                from == Phase::CheckpointApplied || from == Phase::TargetStepped ||
                                from == Phase::TargetVerified ) &&
                              to == Phase::RolledBack;
        return adjacent || completion || timelineReset || preMutationFailure || rollback;
    }

    bool TryAdvance( Phase next )
    {
        if ( !IsLegalTransition( m_phase, next ) )
        {
            return false;
        }

        m_phase = next;
        return true;
    }

    Phase Current() const
    {
        return m_phase;
    }

  private:
    Phase m_phase = Phase::Idle;
};

// Invariant:
// - Restore follows the adjacent selection, backup, topology, checkpoint,
//   stepping, verification, and completion walk. Illegal order is a fatal invariant failure.
// - Recoverable failure before mutation ends in Failed. Once live state may
//   have changed, failure can return only after the retained backup is applied
//   and the cursor reaches RolledBack.
// - Scrubber publication accepts success only from Complete and recoverable
//   failure only from Failed/RolledBack, so split completion calls cannot be
//   reordered by caller convention.
// - Branch completion records TimelineResetApplied before Complete. Rollback
//   records a verified live-backup application before RolledBack. These proofs
//   keep terminal publication dependent on transaction state, not caller order.
// - The transaction owns only detached values and its phase cursor. Every
//   runtime owner is borrowed by one ReplayRuntime phase call and expires when
//   that call returns.
// - TestOwnerRequestQueues.cpp proves the complete cursor transition matrix.
class ReplayRestoreTransaction
{
  public:
    explicit ReplayRestoreTransaction( const ReplaySceneTimelineResetInput& timelineReset = {} )
        : m_timelineReset( timelineReset )
    {
    }

    ReplayRestoreTransaction( const ReplayRestoreTransaction& ) = delete;
    ReplayRestoreTransaction& operator=( const ReplayRestoreTransaction& ) = delete;

    void SetArtifactRequest( const ReplayLiveRestoreRequest& request )
    {
        if ( m_phase.Current() != ReplayRestorePhaseCursor::Phase::Idle || m_hasArtifactRequest ||
             request.kind != ReplayLiveRestoreKind::V2ArtifactTarget )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Artifact request must be bound once before restore selection. phase=%u bound=%u kind=%u",
                      static_cast<unsigned int>( m_phase.Current() ), m_hasArtifactRequest ? 1u : 0u,
                      static_cast<unsigned int>( request.kind ) );
        }

        m_artifactRequest = request;
        m_artifactRequest.solverSample = nullptr;
        m_hasArtifactRequest = true;
    }

    const ReplayLiveRestoreRequest& ArtifactRequest() const
    {
        if ( !m_hasArtifactRequest )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction", "Artifact restore started without a bound request." );
        }

        return m_artifactRequest;
    }

    void SelectArtifact( std::size_t checkpointIndex, std::size_t targetIndex )
    {
        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::ArtifactSelected, "SelectArtifact" );
        m_checkpointIndex = checkpointIndex;
        m_targetIndex = targetIndex;
    }

    void CaptureLiveBackup( ReplaySolverFrameSample&& sample )
    {
        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::LiveBackupCaptured, "CaptureLiveBackup" );

        // Hazard: ReplaySolverFrameSample owns a replay-policy vector. Moving
        // the freshly captured sample transfers its registered allocation;
        // copying here would add an unregistered restore-time growth path.
        m_liveBackup = std::move( sample );
        m_hasLiveBackup = true;
    }

    void MarkTopologyPrepared( bool rebuilt, bool stateMutated )
    {
        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::TopologyPrepared, "MarkTopologyPrepared" );
        m_generatedTopologyRebuilt = rebuilt;
        m_stateMutated = stateMutated;
    }

    void MarkCheckpointApplied()
    {
        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::CheckpointApplied, "MarkCheckpointApplied" );
        m_stateMutated = true;
    }

    void BeginTargetStep( uint32_t eventCursor )
    {
        if ( m_phase.Current() != ReplayRestorePhaseCursor::Phase::CheckpointApplied || m_targetStepStarted )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Target stepping began outside the applied-checkpoint phase. phase=%u already_started=%u",
                      static_cast<unsigned int>( m_phase.Current() ), m_targetStepStarted ? 1u : 0u );
        }

        m_eventCursor = eventCursor;
        m_eventsApplied = 0;
        m_unsupportedEvents = 0;
        m_targetStepStarted = true;
    }

    void RecordAppliedTargetEvent( uint32_t sequence )
    {
        RequireTargetStepStarted( "RecordAppliedTargetEvent" );
        m_eventCursor = (std::max)( m_eventCursor, sequence + 1u );
        ++m_eventsApplied;
    }

    void RecordUnsupportedTargetEvent()
    {
        RequireTargetStepStarted( "RecordUnsupportedTargetEvent" );
        ++m_unsupportedEvents;
    }

    void MarkTargetStepped( ReplayFrameIndex frame )
    {
        RequireTargetStepStarted( "MarkTargetStepped" );
        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::TargetStepped, "MarkTargetStepped" );
        m_targetFrame = frame;
    }

    void MarkTargetVerified()
    {
        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::TargetVerified, "MarkTargetVerified" );
    }

    void PrepareTimelineReset( uint32_t parentBranchId, int sceneFrame, uint64_t solverHash )
    {
        if ( m_phase.Current() != ReplayRestorePhaseCursor::Phase::TargetVerified || m_timelineResetRequired )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Timeline reset prepared outside the verified target phase. phase=%u already_required=%u",
                      static_cast<unsigned int>( m_phase.Current() ), m_timelineResetRequired ? 1u : 0u );
        }

        m_timelineResetRequired = true;
        m_parentBranchId = parentBranchId;
        m_branchSceneFrame = sceneFrame;
        m_branchSolverHash = solverHash;
    }

    void MarkTimelineResetApplied()
    {
        if ( !m_timelineResetRequired )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Timeline reset application recorded without a prepared branch reset." );
        }

        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::TimelineResetApplied, "MarkTimelineResetApplied" );
        m_timelineResetApplied = true;
    }

    bool CompletionReady() const
    {
        const ReplayRestorePhaseCursor::Phase current = m_phase.Current();
        return m_timelineResetRequired
                   ? m_timelineResetApplied && current == ReplayRestorePhaseCursor::Phase::TimelineResetApplied
                   : !m_timelineResetApplied && current == ReplayRestorePhaseCursor::Phase::TargetVerified;
    }

    void Complete()
    {
        if ( !CompletionReady() )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Restore completion reached without satisfying branch timeline state. phase=%u required=%u applied=%u",
                      static_cast<unsigned int>( m_phase.Current() ), m_timelineResetRequired ? 1u : 0u,
                      m_timelineResetApplied ? 1u : 0u );
        }

        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::Complete, "Complete" );
    }

    void RequireScrubberPublicationTerminal( bool restored ) const
    {
        const ReplayRestorePhaseCursor::Phase current = m_phase.Current();

        const bool successTerminal = restored && current == ReplayRestorePhaseCursor::Phase::Complete &&
                                     m_timelineResetRequired == m_timelineResetApplied;
        const bool failureTerminal = !restored &&
                                     ( current == ReplayRestorePhaseCursor::Phase::Failed ||
                                       ( current == ReplayRestorePhaseCursor::Phase::RolledBack && m_liveBackupApplied ) );

        if ( !( successTerminal || failureTerminal ) )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Scrubber publication reached before the restore transaction became terminal. "
                      "restored=%u phase=%u timeline_required=%u timeline_applied=%u rollback_applied=%u",
                      restored ? 1u : 0u, static_cast<unsigned int>( current ), m_timelineResetRequired ? 1u : 0u,
                      m_timelineResetApplied ? 1u : 0u, m_liveBackupApplied ? 1u : 0u );
        }
    }

    void FailBeforeMutation( const char* reason )
    {
        if ( m_stateMutated )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Pre-mutation failure reported after live state mutation. phase=%u",
                      static_cast<unsigned int>( m_phase.Current() ) );
        }

        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::Failed, "FailBeforeMutation" );
        CopyFailure( reason );
    }

    void MarkLiveBackupApplied()
    {
        if ( !m_stateMutated || !m_hasLiveBackup || m_liveBackupApplied ||
             !ReplayRestorePhaseCursor::IsLegalTransition( m_phase.Current(), ReplayRestorePhaseCursor::Phase::RolledBack ) )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Live-backup application recorded without mutated state, retained backup, or rollback phase. "
                      "phase=%u mutated=%u backup=%u",
                      static_cast<unsigned int>( m_phase.Current() ), m_stateMutated ? 1u : 0u, m_hasLiveBackup ? 1u : 0u );
        }

        m_liveBackupApplied = true;
    }

    bool RollbackReady() const
    {
        return m_stateMutated && m_hasLiveBackup && m_liveBackupApplied &&
               ReplayRestorePhaseCursor::IsLegalTransition( m_phase.Current(), ReplayRestorePhaseCursor::Phase::RolledBack );
    }

    void MarkRolledBack( const char* reason )
    {
        if ( !RollbackReady() )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Rollback completed without verified live-backup application. phase=%u mutated=%u backup=%u "
                      "applied=%u",
                      static_cast<unsigned int>( m_phase.Current() ), m_stateMutated ? 1u : 0u, m_hasLiveBackup ? 1u : 0u,
                      m_liveBackupApplied ? 1u : 0u );
        }

        AdvanceOrFatal( ReplayRestorePhaseCursor::Phase::RolledBack, "MarkRolledBack" );
        CopyFailure( reason );
    }

    ReplayRestorePhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

    const ReplaySceneTimelineResetInput& TimelineReset() const
    {
        return m_timelineReset;
    }

    const ReplaySolverFrameSample& LiveBackup() const
    {
        return m_liveBackup;
    }

    bool HasLiveBackup() const
    {
        return m_hasLiveBackup;
    }

    bool StateMutated() const
    {
        return m_stateMutated;
    }

    bool GeneratedTopologyRebuilt() const
    {
        return m_generatedTopologyRebuilt;
    }

    std::size_t CheckpointIndex() const
    {
        return m_checkpointIndex;
    }

    std::size_t TargetIndex() const
    {
        return m_targetIndex;
    }

    ReplayFrameIndex TargetFrame() const
    {
        return m_targetFrame;
    }

    uint32_t EventCursor() const
    {
        return m_eventCursor;
    }

    std::size_t EventsApplied() const
    {
        return m_eventsApplied;
    }

    std::size_t UnsupportedEvents() const
    {
        return m_unsupportedEvents;
    }

    const char* FailureReason() const
    {
        return m_failureReason;
    }

    void RecordFailure( const char* reason )
    {
        CopyFailure( reason );
    }

    RunReplayV2TargetRestoreResult& Result()
    {
        return m_result;
    }

    const RunReplayV2TargetRestoreResult& Result() const
    {
        return m_result;
    }

    void RequestInteractiveScene()
    {
        m_enterInteractiveRequested = true;
    }

    bool EnterInteractiveRequested() const
    {
        return m_enterInteractiveRequested;
    }

    bool TimelineResetRequired() const
    {
        return m_timelineResetRequired;
    }

    uint32_t ParentBranchId() const
    {
        return m_parentBranchId;
    }

    int BranchSceneFrame() const
    {
        return m_branchSceneFrame;
    }

    uint64_t BranchSolverHash() const
    {
        return m_branchSolverHash;
    }

#ifdef _DEBUG
    void RecordRestoreProbeDiagnostic( const ReplayRestoreProbePacket& diagnostic )
    {
        m_restoreProbeDiagnostic = diagnostic;
        m_hasRestoreProbeDiagnostic = true;
    }

    bool HasRestoreProbeDiagnostic() const
    {
        return m_hasRestoreProbeDiagnostic;
    }

    const ReplayRestoreProbePacket& RestoreProbeDiagnostic() const
    {
        return m_restoreProbeDiagnostic;
    }

    void RecordRestoreResultDiagnostic( const ReplayRestoreResultPacket& diagnostic )
    {
        m_restoreResultDiagnostic = diagnostic;
        strncpy_s( m_restoreSource, diagnostic.restoreSource ? diagnostic.restoreSource : "unknown", _TRUNCATE );
        strncpy_s( m_restoreFailureReason, diagnostic.failureReason ? diagnostic.failureReason : "", _TRUNCATE );
        m_restoreResultDiagnostic.restoreSource = m_restoreSource;
        m_restoreResultDiagnostic.failureReason = m_restoreFailureReason;
        m_hasRestoreResultDiagnostic = true;
    }

    bool HasRestoreResultDiagnostic() const
    {
        return m_hasRestoreResultDiagnostic;
    }

    const ReplayRestoreResultPacket& RestoreResultDiagnostic() const
    {
        return m_restoreResultDiagnostic;
    }
#endif

  private:
    void RequireTargetStepStarted( const char* operation ) const
    {
        if ( m_phase.Current() != ReplayRestorePhaseCursor::Phase::CheckpointApplied || !m_targetStepStarted )
        {
            SB_FATAL( "Runtime/ReplayRestoreTransaction",
                      "Target-step progress changed outside its transaction phase. operation=%s phase=%u started=%u",
                      operation, static_cast<unsigned int>( m_phase.Current() ), m_targetStepStarted ? 1u : 0u );
        }
    }

    void AdvanceOrFatal( ReplayRestorePhaseCursor::Phase next, const char* operation )
    {
        const ReplayRestorePhaseCursor::Phase current = m_phase.Current();

        if ( !m_phase.TryAdvance( next ) )
        {
            // Fatal invariant: accepting an out-of-order restore phase could publish a
            // partial topology or return after mutation without rollback.
            SB_FATAL( "Runtime/ReplayRestoreTransaction", "Illegal phase transition. operation=%s current=%u next=%u",
                      operation, static_cast<unsigned int>( current ), static_cast<unsigned int>( next ) );
        }
    }

    void CopyFailure( const char* reason )
    {
        strncpy_s( m_failureReason, reason ? reason : "restore failed", _TRUNCATE );
    }

    ReplaySceneTimelineResetInput m_timelineReset;
    ReplayLiveRestoreRequest m_artifactRequest;
    ReplaySolverFrameSample m_liveBackup;
    RunReplayV2TargetRestoreResult m_result;
    ReplayRestorePhaseCursor m_phase;
    std::size_t m_checkpointIndex = 0;
    std::size_t m_targetIndex = 0;
    ReplayFrameIndex m_targetFrame = 0;
    uint32_t m_eventCursor = 0;
    std::size_t m_eventsApplied = 0;
    std::size_t m_unsupportedEvents = 0;
    bool m_hasLiveBackup = false;
    bool m_hasArtifactRequest = false;
    bool m_stateMutated = false;
    bool m_generatedTopologyRebuilt = false;
    bool m_enterInteractiveRequested = false;
    bool m_timelineResetRequired = false;
    bool m_timelineResetApplied = false;
    bool m_liveBackupApplied = false;
    bool m_targetStepStarted = false;
    uint32_t m_parentBranchId = 0;
    int m_branchSceneFrame = 0;
    uint64_t m_branchSolverHash = 0;
    char m_failureReason[320] = {};
#ifdef _DEBUG
    // Lifetime: diagnostic strings are copied into transaction-owned bounded
    // storage so publication never borrows an artifact or stack reason buffer.
    ReplayRestoreProbePacket m_restoreProbeDiagnostic;
    ReplayRestoreResultPacket m_restoreResultDiagnostic;
    char m_restoreSource[32] = {};
    char m_restoreFailureReason[320] = {};
    bool m_hasRestoreProbeDiagnostic = false;
    bool m_hasRestoreResultDiagnostic = false;
#endif
};

} // namespace Runtime
} // namespace SkullbonezCore
