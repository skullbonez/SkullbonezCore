/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
Purpose:
  Provides the runtime diagnostics ownership boundary.

Summary:
  DiagnosticsRuntime sequences performance, memory, and physics diagnostic
  work. Capture remains a sibling owner applied by App from typed results.

Glossary:
  Reconciled memory: Tracked engine bytes plus any process memory not accounted

    for by replay or model collection snapshots.

Invariants:
  - DiagnosticsRuntime is a boundary; artifact schema and heavy logging formats
    stay in RuntimeDiagnostics unless this file owns them.
  - Memory sampling is cached for diagnostics reads; deep process samples are
    reserved for explicit dumps and stress/perf evidence.
  - Debug-only physics diagnostics stay behind _DEBUG.
  - Scene-end capacity reporting runs before old scene identity is replaced.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp
  - SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "DiagnosticsRuntime.h"
#include "DiagnosticsPhysicsUI.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "OverlayDebugState.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
constexpr double MAIN_MEMORY_SAMPLE_INTERVAL_SECONDS = 1.0;

const char* DiagnosticFileNameFromPath( const char* path )
{
    const char* forwardSlash = std::strrchr( path, '/' );
    const char* backSlash = std::strrchr( path, '\\' );
    const char* separator = forwardSlash;
    if ( !separator || ( backSlash && backSlash > separator ) )
    {
        separator = backSlash;
    }
    return separator ? separator + 1 : path;
}

void WriteJsonString( FILE* file, const char* value )
{
    // Concept: Memory dumps are written without a JSON library, so this helper
    // is the narrow escaping boundary for user-controlled checkpoint/path text.
    fputc( '"', file );

    if ( value )
    {
        for ( const char* cursor = value; *cursor != '\0'; ++cursor )
        {
            switch ( *cursor )
            {
            case '\\':
                fputs( "\\\\", file );
                break;
            case '"':
                fputs( "\\\"", file );
                break;
            case '\n':
                fputs( "\\n", file );
                break;
            case '\r':
                fputs( "\\r", file );
                break;
            case '\t':
                fputs( "\\t", file );
                break;
            default:
                fputc( *cursor, file );
                break;
            }
        }
    }

    fputc( '"', file );
}

uint64_t ReplayTrajectoryLaneCounter( const uint64_t* counters, SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    const std::size_t laneIndex = static_cast<std::size_t>( lane );
    return laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT ? counters[laneIndex] : 0;
}

// Why: the memory dump writes named JSON fields while replay stores counters in
// fixed arrays. These helpers are the narrow enum-to-array translation boundary.
uint64_t ReplayTrajectoryBudgetCounter( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& stats,
                                        SkullbonezCore::Core::MainMemoryReplayBudgetPass pass )
{
    const std::size_t passIndex = static_cast<std::size_t>( pass );
    return passIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT ? stats.budgetExpiries[passIndex] : 0;
}

uint64_t ReplayTrajectoryRebuildCounter( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& stats,
                                         SkullbonezCore::Core::MainMemoryReplayRebuildCause cause )
{
    const std::size_t causeIndex = static_cast<std::size_t>( cause );
    return causeIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT ? stats.rebuildCauses[causeIndex] : 0;
}

uint64_t ReplayMemoryCategoryCounter( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                                      SkullbonezCore::Core::MainMemoryReplayByteCategory category )
{
    return SkullbonezCore::Core::MainMemoryReplayCategoryByte( replay.categoryBytes, category );
}

// Concept: byte categories are stored as fixed arrays in core stats; the dump
// owns the stable JSON labels that scripts and manual investigations read.
void WriteReplayMemoryCategories( FILE* file, const SkullbonezCore::Core::MainMemoryReplayStats& replay )
{
    fputs( "    \"memory_categories\": {\n", file );
    fprintf( file,
             "      \"presentation\": { \"owner\": %llu, \"sample_records\": %llu, \"checkpoints\": %llu, "
             "\"scratch\": %llu, \"bodies\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                                       PresentationSampleRecords ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                                       PresentationCheckpoints ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationScratch ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies ) ) );

    fprintf( file,
             "      \"solver\": { \"owner\": %llu, \"sample_records\": %llu, \"checkpoints\": %llu, "
             "\"scratch\": %llu, \"bodies\": %llu, \"world_state\": %llu, \"launcher_visuals\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverSampleRecords ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverCheckpoints ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverScratch ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverBodies ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverWorldState ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                                       SolverLauncherVisuals ) ) );

    fprintf( file, "      \"events\": { \"owner\": %llu, \"events\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::Events ) ) );

    fprintf( file, "      \"loaded_replay\": { \"owner\": %llu, \"sample_records\": %llu, \"bodies\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedSampleRecords ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedBodies ) ) );

    fprintf( file,
             "      \"prediction\": { \"owner\": %llu, \"engine\": %llu, \"world_state\": %llu, "
             "\"body_state\": %llu, \"frame_records\": %llu, \"frame_bodies\": %llu, "
             "\"debug_contacts\": %llu, \"future_tree\": %llu, \"solver_contact_evidence\": %llu, "
             "\"pipeline_evidence\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionEngine ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionWorldState ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionBodyState ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameRecords ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameBodies ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                                       PredictionDebugContacts ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFutureTree ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                                       PredictionSolverContactEvidence ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                                       PredictionPipelineEvidence ) ) );

    fprintf( file,
             "      \"path_and_cause\": { \"owner\": %llu, \"targets\": %llu, \"future_nodes\": %llu, "
             "\"cause_rows\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::PathTargets ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PathFutureNodes ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::PathCauseRows ) ) );

    fprintf( file,
             "      \"render_scratch\": { \"ghost_requests\": %llu, \"focus_mask\": %llu, "
             "\"launcher_backup\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderFocusMask ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderLauncherBackup ) ) );

    fprintf( file,
             "      \"trajectory\": { \"store\": %llu, \"records\": %llu, \"points\": %llu, "
             "\"published_points\": %llu, \"version_churn\": %llu }\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter( replay,
                                                                           SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore ) ),
             static_cast<unsigned long long>( replay.trajectory.recordCount ),
             static_cast<unsigned long long>( replay.trajectory.pointCount ),
             static_cast<unsigned long long>( replay.trajectory.publishedPointCount ),
             static_cast<unsigned long long>( replay.trajectory.versionChurn ) );

    fputs( "    },\n", file );
}

void WriteReplayPredictionEvidence( FILE* file, const SkullbonezCore::Core::MainMemoryReplayStats& replay )
{
    const SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceStats& evidence = replay.predictionEvidence;
    fprintf( file,
             "    \"prediction_evidence\": { \"current_capacity_bytes\": %llu, "
             "\"lifetime_peak_capacity_bytes\": %llu, \"release_checkpoint_count\": %llu, "
             "\"last_release_before_capacity_bytes\": %llu, \"last_release_after_capacity_bytes\": %llu, "
             "\"last_release_before_replay_total_bytes\": %llu, \"last_release_after_replay_total_bytes\": %llu, "
             "\"last_release_before_category_total_bytes\": %llu, \"last_release_after_category_total_bytes\": %llu, "
             "\"build\": { \"contact_capacity_bytes\": %llu, \"pipeline_capacity_bytes\": %llu, "
             "\"frame_capacity_bytes\": %llu, \"contacts\": %llu, \"pipeline_rows\": %llu, \"frames\": %llu }, "
             "\"committed\": { \"contact_capacity_bytes\": %llu, \"pipeline_capacity_bytes\": %llu, "
             "\"frame_capacity_bytes\": %llu, \"contacts\": %llu, \"pipeline_rows\": %llu, \"frames\": %llu } },\n",
             static_cast<unsigned long long>( evidence.currentCapacityBytes ),
             static_cast<unsigned long long>( evidence.lifetimePeakCapacityBytes ),
             static_cast<unsigned long long>( evidence.releaseCheckpointCount ),
             static_cast<unsigned long long>( evidence.lastReleaseBeforeCapacityBytes ),
             static_cast<unsigned long long>( evidence.lastReleaseAfterCapacityBytes ),
             static_cast<unsigned long long>( evidence.lastReleaseBeforeReplayTotalBytes ),
             static_cast<unsigned long long>( evidence.lastReleaseAfterReplayTotalBytes ),
             static_cast<unsigned long long>( evidence.lastReleaseBeforeCategoryTotalBytes ),
             static_cast<unsigned long long>( evidence.lastReleaseAfterCategoryTotalBytes ),
             static_cast<unsigned long long>( evidence.buildContactCapacityBytes ),
             static_cast<unsigned long long>( evidence.buildPipelineCapacityBytes ),
             static_cast<unsigned long long>( evidence.buildFrameCapacityBytes ),
             static_cast<unsigned long long>( evidence.buildContactCount ),
             static_cast<unsigned long long>( evidence.buildPipelineCount ),
             static_cast<unsigned long long>( evidence.buildFrameCount ),
             static_cast<unsigned long long>( evidence.committedContactCapacityBytes ),
             static_cast<unsigned long long>( evidence.committedPipelineCapacityBytes ),
             static_cast<unsigned long long>( evidence.committedFrameCapacityBytes ),
             static_cast<unsigned long long>( evidence.committedContactCount ),
             static_cast<unsigned long long>( evidence.committedPipelineCount ),
             static_cast<unsigned long long>( evidence.committedFrameCount ) );
}

void WriteReplayGrowthOwners( FILE* file, const SkullbonezCore::Core::MainMemoryReplayStats& replay )
{
    // Concept: each row pairs the committed sizing evidence with live allocator
    // counters, so dumps distinguish an intentional cap from observed use.
    fputs( "    \"growth_owners\": [\n", file );

    for ( std::size_t index = 0; index < replay.growthOwners.size(); ++index )
    {
        const SkullbonezCore::Core::MainMemoryReplayStats::GrowthOwner& owner = replay.growthOwners[index];
        fprintf( file,
                 "      { \"owner\": \"%s\", \"registered\": %s, \"hard_bytes\": %d, "
                 "\"measured_high_water_bytes\": %llu, \"allocator_high_water_bytes\": %llu, "
                 "\"reported_high_water_capacity\": %d, \"growths\": %llu, \"failed_growths\": %llu, "
                 "\"last_growth_frame\": %d }%s\n",
                 owner.ownerName ? owner.ownerName : "", owner.registered ? "true" : "false", owner.hardBytes,
                 static_cast<unsigned long long>( owner.measuredHighWaterBytes ),
                 static_cast<unsigned long long>( owner.allocatorHighWaterBytes ), owner.reportedHighWaterCapacity,
                 static_cast<unsigned long long>( owner.replayGrowths ),
                 static_cast<unsigned long long>( owner.failedGrowths ), owner.lastGrowthFrame,
                 index + 1u < replay.growthOwners.size() ? "," : "" );
    }

    fputs( "    ],\n", file );
}

void WriteReplayTrajectoryCounters( FILE* file, const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& trajectory )
{
    fprintf( file,
             "    \"trajectory\": {\n"
             "      \"store_bytes\": %llu,\n"
             "      \"record_count\": %llu,\n"
             "      \"point_count\": %llu,\n"
             "      \"published_point_count\": %llu,\n"
             "      \"version_churn\": %llu,\n"
             "      \"max_record_version\": %u,\n"
             "      \"segments_emitted\": {\n"
             "        \"past_root\": %llu,\n"
             "        \"future_root\": %llu,\n"
             "        \"future_child_incoming\": %llu,\n"
             "        \"future_child_outgoing\": %llu,\n"
             "        \"retained_trail\": %llu,\n"
             "        \"baseline_root\": %llu,\n"
             "        \"causal_marker\": %llu,\n"
             "        \"auxiliary_trail\": %llu\n"
             "      },\n"
             "      \"segments_dropped\": {\n"
             "        \"past_root\": %llu,\n"
             "        \"future_root\": %llu,\n"
             "        \"future_child_incoming\": %llu,\n"
             "        \"future_child_outgoing\": %llu,\n"
             "        \"retained_trail\": %llu,\n"
             "        \"baseline_root\": %llu,\n"
             "        \"causal_marker\": %llu,\n"
             "        \"auxiliary_trail\": %llu\n"
             "      },\n"
             "      \"budget_expiries\": {\n"
             "        \"prediction_begin\": %llu,\n"
             "        \"prediction_step\": %llu,\n"
             "        \"prediction_build_tree\": %llu,\n"
             "        \"retained_refresh\": %llu\n"
             "      },\n"
             "      \"rebuild_causes\": {\n"
             "        \"dirty\": %llu,\n"
             "        \"automatic_refresh\": %llu\n"
             "      }\n"
             "    }\n",
             static_cast<unsigned long long>( trajectory.storeBytes ),
             static_cast<unsigned long long>( trajectory.recordCount ),
             static_cast<unsigned long long>( trajectory.pointCount ),
             static_cast<unsigned long long>( trajectory.publishedPointCount ),
             static_cast<unsigned long long>( trajectory.versionChurn ), trajectory.maxRecordVersion,
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker ) ),
             static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                                                           SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail ) ),
             static_cast<unsigned long long>( ReplayTrajectoryBudgetCounter( trajectory,
                                                                             SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin ) ),
             static_cast<unsigned long long>( ReplayTrajectoryBudgetCounter( trajectory,
                                                                             SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep ) ),
             static_cast<unsigned long long>( ReplayTrajectoryBudgetCounter( trajectory,
                                                                             SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBuildTree ) ),
             static_cast<unsigned long long>( ReplayTrajectoryBudgetCounter( trajectory,
                                                                             SkullbonezCore::Core::MainMemoryReplayBudgetPass::RetainedRefresh ) ),
             static_cast<unsigned long long>( ReplayTrajectoryRebuildCounter( trajectory, SkullbonezCore::Core::MainMemoryReplayRebuildCause::Dirty ) ),
             static_cast<unsigned long long>( ReplayTrajectoryRebuildCounter( trajectory,
                                                                              SkullbonezCore::Core::MainMemoryReplayRebuildCause::AutomaticRefresh ) ) );
}
} // namespace

DiagnosticsUIKeyboardShortcutResult HandleDiagnosticsUIKeyboardShortcut( OverlayDebugState& debug,
                                                                         DiagnosticsUiKeyboardCommand command,
                                                                         bool wasPressed )
{
    DiagnosticsUIKeyboardShortcutResult result;

    result.handled = true;

    if ( !wasPressed )
    {
        return result;
    }

    result.triggered = true;
    result.releaseMouseToUI = true;

    switch ( command )
    {
    case DiagnosticsUiKeyboardCommand::ToggleVisibility:

        // Concept: The tabbed diagnostics UI owns overlay text once visible, so
        // the GameUI one-line overlay is cleared by the UI shortcut owner.
        result.markInteractiveRun = true;
        result.disableExitOnComplete = true;
        result.disableCaptureAutomationExit = true;
        debug.overlayMode = OverlayMode::None;
        return result;
    case DiagnosticsUiKeyboardCommand::TogglePerformanceHistogram:

        // F5/F6 are lightweight diagnostic overlays; they do not implicitly open
        // or close the broader diagnostics window.
        return result;
    case DiagnosticsUiKeyboardCommand::ToggleMemoryOverlay:
        return result;
    }

    return result;
}


void DiagnosticsRuntime::BindProfiler( SkullbonezCore::Core::Profiler* profiler )
{
    m_diagnostics.BindProfiler( profiler );
}


RunPerfLogState& DiagnosticsRuntime::PerfLog()
{
    return m_diagnostics.PerfLog();
}


void DiagnosticsRuntime::ClosePerfLog()
{
    m_diagnostics.ClosePerfLog();
}


void DiagnosticsRuntime::ClosePerfLogWithMemoryCheckpoint( int pass, const char* checkpoint )
{
    m_diagnostics.ClosePerfLogWithMemoryCheckpoint( pass, checkpoint );
}


void DiagnosticsRuntime::ResetPerfLogForSceneLoad()
{
    m_diagnostics.ResetPerfLogForSceneLoad();
}


void DiagnosticsRuntime::ResetForSceneLoad( int completedPerfPass )
{
    ClosePerfLogWithMemoryCheckpoint( completedPerfPass, "end" );
    ResetPerfLogForSceneLoad();
    m_uiStress.Reset();
}


void DiagnosticsRuntime::ConfigurePerfLogFlush( bool enabled, int interval )
{
    m_diagnostics.ConfigurePerfLogFlush( enabled, interval );
}


void DiagnosticsRuntime::OpenScenePerfLog( const char* path, int pass )
{
    m_diagnostics.OpenScenePerfLog( path, pass );
}


void DiagnosticsRuntime::ApplyScenePerfLogOptions( const char* path, int perfPass )
{
    if ( path && path[0] != '\0' )
    {
        OpenScenePerfLog( path, perfPass );
    }
}


bool DiagnosticsRuntime::PerfTestActive() const
{
    return m_diagnostics.PerfTestActive();
}


void DiagnosticsRuntime::TickPerfLog( int pass, int frame, float physicsTimeSeconds, float renderTimeSeconds )
{
    m_diagnostics.TickPerfLog( pass, frame, physicsTimeSeconds, renderTimeSeconds );
}


RuntimeProfilerFrameTimes DiagnosticsRuntime::SampleProfilerFrameTimes() const
{
    return m_diagnostics.SampleProfilerFrameTimes();
}


const SkullbonezCore::Core::MainMemoryStats&
DiagnosticsRuntime::RefreshMainMemoryStats( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                                            const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects,
                                            double nowSeconds, bool force, bool includePrivateWorkingSet )
{
    const bool sampleDue = m_lastMainMemorySampleSeconds < 0.0 ||
                           nowSeconds - m_lastMainMemorySampleSeconds >= MAIN_MEMORY_SAMPLE_INTERVAL_SECONDS;

    const bool deepSampleDue = includePrivateWorkingSet && !m_lastMainMemorySampleUsedPrivateWorkingSetQuery;

    if ( !force && !sampleDue && !deepSampleDue )
    {
        return m_mainMemoryStats;
    }

    SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Diagnostics );

    // Concept: The Memory tab wants cheap repeated reads, while shutdown dumps and
    // stress logs need the deep private-working-set query. Build one reconciled
    // snapshot, then cache both the timestamp and the sampling mode.
    SkullbonezCore::Core::MainMemoryStats stats;
    stats.sampleTimeSeconds = nowSeconds;
    stats.process = RuntimeDiagnostics::SampleProcessMemory( includePrivateWorkingSet );
    stats.replay = replay;
    stats.gameObjects = gameObjects;
    stats.foreignFreeCount = SkullbonezCore::Core::Allocation::RuntimeAllocationForeignFreeCount();
    stats.trackedEngineBytes = stats.replay.totalBytes + stats.gameObjects.totalBytes + stats.otherTrackedBytes;

    if ( stats.process.available )
    {
        // Why: Task Manager numbers include memory not tracked by replay or
        // scene stores. The reconciliation fields make that gap explicit
        // instead of hiding it in the engine bucket.
        if ( stats.process.taskManagerBytes >= stats.trackedEngineBytes )
        {
            stats.unattributedProcessBytes = stats.process.taskManagerBytes - stats.trackedEngineBytes;
            stats.reconciledTotalBytes = stats.process.taskManagerBytes;
        }
        else
        {
            stats.trackedOvershootBytes = stats.trackedEngineBytes - stats.process.taskManagerBytes;
        }

        stats.reconciledTotalBytes = stats.trackedEngineBytes + stats.unattributedProcessBytes - stats.trackedOvershootBytes;

        stats.reconciliationDeltaBytes = stats.reconciledTotalBytes >= stats.process.taskManagerBytes
                                             ? stats.reconciledTotalBytes - stats.process.taskManagerBytes
                                             : stats.process.taskManagerBytes - stats.reconciledTotalBytes;
    }
    else
    {
        stats.reconciledTotalBytes = stats.trackedEngineBytes;
    }

    m_mainMemoryStats = stats;
    m_lastMainMemorySampleSeconds = nowSeconds;
    m_lastMainMemorySampleUsedPrivateWorkingSetQuery = includePrivateWorkingSet;
    return m_mainMemoryStats;
}


const SkullbonezCore::Core::MainMemoryStats& DiagnosticsRuntime::MainMemoryStatsSnapshot() const
{
    return m_mainMemoryStats;
}


void DiagnosticsRuntime::SetMainMemoryDumpPath( const char* path )
{
    if ( !path )
    {
        m_mainMemoryDumpPath[0] = '\0';
        return;
    }

    strncpy_s( m_mainMemoryDumpPath, sizeof( m_mainMemoryDumpPath ), path, _TRUNCATE );
}


bool DiagnosticsRuntime::MainMemoryDumpRequested() const
{
    return m_mainMemoryDumpPath[0] != '\0';
}


bool DiagnosticsRuntime::WriteMainMemoryDump( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                                              const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects,
                                              const RuntimeSceneDiagnosticFacts& scene, const char* checkpoint,
                                              double nowSeconds )
{
    if ( !MainMemoryDumpRequested() )
    {
        return false;
    }

    const SkullbonezCore::Core::MainMemoryStats& stats = RefreshMainMemoryStats( replay, gameObjects, nowSeconds, true,
                                                                                 true );

    FILE* file = nullptr;

    if ( fopen_s( &file, m_mainMemoryDumpPath, "wb" ) != 0 || !file )
    {
        fprintf( stderr, "[memory] Failed to open memory dump: %s\n", m_mainMemoryDumpPath );
        return false;
    }

    // Invariant: skullbonez.main_memory.v1 is a script-facing artifact schema.
    // Add fields compatibly or bump the schema if a consumer must change.
    fputs( "{\n  \"schema\": \"skullbonez.main_memory.v1\",\n  \"checkpoint\": ", file );
    WriteJsonString( file, checkpoint && checkpoint[0] != '\0' ? checkpoint : "shutdown" );
    fprintf( file,
             ",\n  \"frame\": %d,\n"
             "  \"sample_time_seconds\": %.6f,\n"
             "  \"process\": {\n"
             "    \"available\": %s,\n"
             "    \"task_manager_metric_name\": \"%s\",\n"
             "    \"task_manager_memory_bytes\": %llu,\n"
             "    \"working_set_bytes\": %llu,\n"
             "    \"private_working_set_bytes\": %llu,\n"
             "    \"private_commit_bytes\": %llu,\n"
             "    \"pagefile_usage_bytes\": %llu\n"
             "  },\n"
             "  \"replay\": {\n"
             "    \"total_bytes\": %llu,\n"
             "    \"presentation_bytes\": %llu,\n"
             "    \"solver_bytes\": %llu,\n"
             "    \"events_bytes\": %llu,\n"
             "    \"loaded_replay_bytes\": %llu,\n"
             "    \"prediction_bytes\": %llu,\n"
             "    \"path_and_cause_bytes\": %llu,\n"
             "    \"render_scratch_bytes\": %llu,\n"
             "    \"presentation_samples\": %llu,\n"
             "    \"solver_samples\": %llu,\n"
             "    \"event_samples\": %llu,\n"
             "    \"loaded_replay_samples\": %llu,\n"
             "    \"prediction_frames\": %llu,\n"
             "    \"path_nodes\": %llu,\n"
             "    \"cause_rows\": %llu,\n"
             "    \"ghost_requests\": %llu,\n"
             "    \"memory_preset\": %d,\n"
             "    \"requested_retention_seconds\": %d,\n"
             "    \"requested_budget_mib\": %d,\n"
             "    \"presentation_retention_seconds\": %d,\n"
             "    \"solver_retention_seconds\": %d,\n"
             "    \"memory_budget_clamped\": %s,\n"
             "    \"solver_window_reduced\": %s,\n",
             scene.CurrentFrame(), stats.sampleTimeSeconds, stats.process.available ? "true" : "false",
             stats.process.taskManagerMetricName, static_cast<unsigned long long>( stats.process.taskManagerBytes ),
             static_cast<unsigned long long>( stats.process.workingSetBytes ),
             static_cast<unsigned long long>( stats.process.privateWorkingSetBytes ),
             static_cast<unsigned long long>( stats.process.privateCommitBytes ),
             static_cast<unsigned long long>( stats.process.pagefileUsageBytes ),
             static_cast<unsigned long long>( stats.replay.totalBytes ),
             static_cast<unsigned long long>( stats.replay.presentationBytes ),
             static_cast<unsigned long long>( stats.replay.solverBytes ),
             static_cast<unsigned long long>( stats.replay.eventsBytes ),
             static_cast<unsigned long long>( stats.replay.loadedReplayBytes ),
             static_cast<unsigned long long>( stats.replay.predictionBytes ),
             static_cast<unsigned long long>( stats.replay.pathAndCauseBytes ),
             static_cast<unsigned long long>( stats.replay.renderScratchBytes ),
             static_cast<unsigned long long>( stats.replay.presentationSamples ),
             static_cast<unsigned long long>( stats.replay.solverSamples ),
             static_cast<unsigned long long>( stats.replay.eventSamples ),
             static_cast<unsigned long long>( stats.replay.loadedReplaySamples ),
             static_cast<unsigned long long>( stats.replay.predictionFrames ),
             static_cast<unsigned long long>( stats.replay.pathNodes ),
             static_cast<unsigned long long>( stats.replay.causeRows ),
             static_cast<unsigned long long>( stats.replay.ghostRequests ), stats.replay.memoryPreset,
             stats.replay.requestedRetentionSeconds, stats.replay.requestedBudgetMiB,
             stats.replay.presentationRetentionSeconds, stats.replay.solverRetentionSeconds,
             stats.replay.memoryBudgetClamped ? "true" : "false", stats.replay.solverWindowReduced ? "true" : "false" );

    WriteReplayMemoryCategories( file, stats.replay );
    WriteReplayPredictionEvidence( file, stats.replay );
    WriteReplayGrowthOwners( file, stats.replay );
    WriteReplayTrajectoryCounters( file, stats.replay.trajectory );
    fprintf( file,
             "  },\n"
             "  \"game_objects\": {\n"
             "    \"total_bytes\": %llu,\n"
             "    \"model_bytes\": %llu,\n"
             "    \"model_vector_bytes\": %llu,\n"
             "    \"physics_store_bytes\": %llu,\n"
             "    \"collider_store_bytes\": %llu,\n"
             "    \"render_store_bytes\": %llu,\n"
             "    \"physics_world_bytes\": %llu,\n"
             "    \"gameplay_world_bytes\": %llu,\n"
             "    \"debug_and_broadphase_bytes\": %llu,\n"
             "    \"model_count\": %llu,\n"
             "    \"model_capacity\": %llu,\n"
             "    \"body_store_capacity\": %llu,\n"
             "    \"collider_store_capacity\": %llu,\n"
             "    \"render_store_capacity\": %llu\n"
             "  },\n"
             "  \"tracked_engine_bytes\": %llu,\n"
             "  \"other_tracked_bytes\": %llu,\n"
             "  \"unattributed_process_bytes\": %llu,\n"
             "  \"tracked_overshoot_bytes\": %llu,\n"
             "  \"reconciled_total_bytes\": %llu,\n"
             "  \"reconciliation_delta_bytes\": %llu,\n"
             "  \"foreign_free_count\": %llu,\n"
             "  \"scene\": {\n"
             "    \"current_frame\": %d,\n"
             "    \"target_frames\": %d,\n"
             "    \"model_count\": %d,\n"
             "    \"test_complete\": %s\n"
             "  }\n"
             "}\n",
             static_cast<unsigned long long>( stats.gameObjects.totalBytes ),
             static_cast<unsigned long long>( stats.gameObjects.modelVectorBytes ),
             static_cast<unsigned long long>( stats.gameObjects.modelVectorBytes ),
             static_cast<unsigned long long>( stats.gameObjects.physicsStoreBytes ),
             static_cast<unsigned long long>( stats.gameObjects.colliderStoreBytes ),
             static_cast<unsigned long long>( stats.gameObjects.renderStoreBytes ),
             static_cast<unsigned long long>( stats.gameObjects.physicsWorldBytes ),
             static_cast<unsigned long long>( stats.gameObjects.gameplayWorldBytes ),
             static_cast<unsigned long long>( stats.gameObjects.debugAndBroadphaseBytes ),
             static_cast<unsigned long long>( stats.gameObjects.modelCount ),
             static_cast<unsigned long long>( stats.gameObjects.modelCapacity ),
             static_cast<unsigned long long>( stats.gameObjects.bodyStoreCapacity ),
             static_cast<unsigned long long>( stats.gameObjects.colliderStoreCapacity ),
             static_cast<unsigned long long>( stats.gameObjects.renderStoreCapacity ),
             static_cast<unsigned long long>( stats.trackedEngineBytes ),
             static_cast<unsigned long long>( stats.otherTrackedBytes ),
             static_cast<unsigned long long>( stats.unattributedProcessBytes ),
             static_cast<unsigned long long>( stats.trackedOvershootBytes ),
             static_cast<unsigned long long>( stats.reconciledTotalBytes ),
             static_cast<unsigned long long>( stats.reconciliationDeltaBytes ),
             static_cast<unsigned long long>( stats.foreignFreeCount ), scene.CurrentFrame(), scene.TargetFrameCount(),
             scene.ModelCount(), scene.TestComplete() ? "true" : "false" );

    fclose( file );
    fprintf( stdout, "[memory] Wrote main memory dump: %s\n", m_mainMemoryDumpPath );
    return true;
}


#ifdef _DEBUG
RunPhysicsDiagnosticsState& DiagnosticsRuntime::PhysicsDiagnostics()
{
    return m_diagnostics.PhysicsDiagnostics();
}


void DiagnosticsRuntime::SetPhysicsRegressionLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsRegressionLogOverride( m_diagnostics.PerfLog(), path );
}


void DiagnosticsRuntime::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsCollisionTimeLogOverride( m_diagnostics.PerfLog(), path );
}


void DiagnosticsRuntime::SetPhysicsDiagnosticsPath( Physics::PhysicsEngine& physics, const char* path,
                                                    bool renderFrameLockstepForcedByDiagnostics )
{
    RuntimeDiagnostics::SetPhysicsDiagnosticsPath( m_diagnostics.PhysicsDiagnostics(), physics, path,
                                                   renderFrameLockstepForcedByDiagnostics );
}


bool DiagnosticsRuntime::LogSceneFinished( const RuntimeSceneDiagnosticFacts& scene, const char* scenePath,
                                           const Rendering::Dx12Diagnostics* renderDiagnostics, const char* reason )
{
    const char* rendererName = renderDiagnostics ? renderDiagnostics->GetRendererName() : "unknown";
    return RuntimeDiagnostics::LogSceneFinished( scene, scenePath, rendererName, reason );
}


void DiagnosticsRuntime::BeginPhysicsDiagnosticsRun( Physics::PhysicsEngine& physics,
                                                     const RuntimeSceneDiagnosticFacts& scene,
                                                     const SkullbonezCore::Core::EngineConfig& config, const char* scenePath,
                                                     const char* rendererName, bool explicitRenderFrameLockstep,
                                                     bool effectiveRenderFrameLockstep )
{
    // Lifetime: RuntimeDiagnostics owns the trace file/session. This boundary
    // only supplies current runtime state and never caches trace handles.
    RuntimeDiagnostics::BeginPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(), physics, scene, config, scenePath,
                                                    rendererName, explicitRenderFrameLockstep,
                                                    effectiveRenderFrameLockstep );
}


void DiagnosticsRuntime::LogReplayScrubProbe( const RuntimeSceneDiagnosticFacts& scene,
                                              const ReplayScrubProbeDiagnostic& probe )
{
    RuntimeDiagnostics::LogReplayScrubProbe( m_diagnostics.PhysicsDiagnostics(), scene, probe );
}


void DiagnosticsRuntime::LogReplayRestoreProbe( const RuntimeSceneDiagnosticFacts& scene,
                                                const ReplayRestoreProbeDiagnostic& probe )
{
    RuntimeDiagnostics::LogReplayRestoreProbe( m_diagnostics.PhysicsDiagnostics(), scene, probe );
}


void DiagnosticsRuntime::LogReplayRestoreResult( const RuntimeSceneDiagnosticFacts& scene,
                                                 const ReplayRestoreResultDiagnostic& result )
{
    // Invariant: Replay restore diagnostics are forwarded with their exact
    // hashes, counts, and flags so SkullScope queries can distinguish checkpoint
    // restores from fallback restores.
    RuntimeDiagnostics::LogReplayRestoreResult( m_diagnostics.PhysicsDiagnostics(), scene, result );
}


void DiagnosticsRuntime::EndPhysicsDiagnosticsRun( const RuntimeSceneDiagnosticFacts& scene, const char* status )
{
    RuntimeDiagnostics::EndPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(), scene, status );
}


#endif


void DiagnosticsRuntime::BeforeSceneUnload( int loadCount, int currentFrame, const char* scenePath )
{
    ReportStoreCapacityRows( loadCount, scenePath, "scene_unload" );
#ifdef _DEBUG
    const RuntimeSceneDiagnosticFacts facts( 0, 0, 0, currentFrame );
    EndPhysicsDiagnosticsRun( facts, "scene_reload" );
#else
    (void)currentFrame;
#endif
}

void DiagnosticsRuntime::ReportStoreCapacityRows( int loadCount, const char* scenePath, const char* status )
{
    if ( loadCount <= 0 )
    {
        return;
    }

    const char* sceneName = scenePath && scenePath[0] != '\0' ? DiagnosticFileNameFromPath( scenePath ) : "<generated>";
    SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::PrintCapacityRows( stdout, sceneName, status );
}


UIStressPolicyOwner& DiagnosticsRuntime::UIStress()
{
    return m_uiStress;
}


} // namespace Runtime
} // namespace SkullbonezCore
