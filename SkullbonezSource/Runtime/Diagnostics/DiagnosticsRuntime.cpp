/*
File: SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp
Purpose:
  Provides the runtime diagnostics ownership boundary.

Summary:
  DiagnosticsRuntime sequences capture, performance, memory, and physics
  diagnostic work. Artifact-specific controllers own their formats while this
  owner retains the process memory cache and shutdown memory-dump lifecycle.

Glossary:
  Artifact path: Stable validation/debug output path written for tools or
    command-line flags.
  Physics diagnostic command: One-frame key or UI request that changes debug
    presentation state, not simulation state.
  Reconciled memory: Tracked engine bytes plus any process memory not accounted
    for by replay or model collection snapshots.
  SkullScope: Queryable physics diagnostics trace owned by RuntimeDiagnostics.
  JSON (JavaScript Object Notation): Text artifact format used for memory dumps.
  Private working set: Resident process pages not shared with other processes;
    matching it requires a page-level OS query.

Invariants:
  - DiagnosticsRuntime is a boundary; artifact schema and heavy logging formats
    stay in RuntimeDiagnostics or CaptureController unless this file owns them.
  - Memory sampling is cached for diagnostics reads; deep process samples are
    reserved for explicit dumps and stress/perf evidence.
  - Debug-only physics diagnostics stay behind _DEBUG.

Related:
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
  - SkullbonezSource/Runtime/DiagnosticsController.cpp
  - SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp
*/
#include "DiagnosticsRuntime.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../InputController.h"
#include "../RunDebugState.h"
#include "../Scene/SceneRuntime.h"
#include "../Scene/SceneController.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Scene/AuthoredScene.h"
#include "../../UI/UICommands.h"
#include "../../UI/UI.h"

#include <algorithm>
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

uint64_t ReplayTrajectoryLaneCounter( const uint64_t* counters,
                                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
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
    return causeIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT ? stats.rebuildCauses[causeIndex]
                                                                                     : 0;
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
             static_cast<unsigned long long>(
                 ReplayMemoryCategoryCounter( replay,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationSampleRecords ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationCheckpoints ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationScratch ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies ) ) );
    fprintf(
        file,
        "      \"solver\": { \"owner\": %llu, \"sample_records\": %llu, \"checkpoints\": %llu, "
        "\"scratch\": %llu, \"bodies\": %llu, \"world_state\": %llu, \"launcher_visuals\": %llu },\n",
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay,
                                         SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverSampleRecords ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay,
                                         SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverCheckpoints ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverScratch ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverBodies ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay,
                                         SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverWorldState ) ),
        static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
            replay,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverLauncherVisuals ) ) );
    fprintf(
        file,
        "      \"events\": { \"owner\": %llu, \"events\": %llu },\n",
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::Events ) ) );
    fprintf(
        file,
        "      \"loaded_replay\": { \"owner\": %llu, \"sample_records\": %llu, \"bodies\": %llu },\n",
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay,
                                         SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedSampleRecords ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedBodies ) ) );
    fprintf( file,
             "      \"prediction\": { \"owner\": %llu, \"engine\": %llu, \"world_state\": %llu, "
             "\"body_state\": %llu, \"frame_records\": %llu, \"frame_bodies\": %llu, "
             "\"debug_contacts\": %llu, \"future_tree\": %llu },\n",
             static_cast<unsigned long long>(
                 ReplayMemoryCategoryCounter( replay,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner ) ),
             static_cast<unsigned long long>(
                 ReplayMemoryCategoryCounter( replay,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionEngine ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionWorldState ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionBodyState ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameRecords ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameBodies ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionDebugContacts ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFutureTree ) ) );
    fprintf(
        file,
        "      \"path_and_cause\": { \"owner\": %llu, \"targets\": %llu, \"future_nodes\": %llu, "
        "\"cause_rows\": %llu },\n",
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::PathTargets ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay,
                                         SkullbonezCore::Core::MainMemoryReplayByteCategory::PathFutureNodes ) ),
        static_cast<unsigned long long>(
            ReplayMemoryCategoryCounter( replay,
                                         SkullbonezCore::Core::MainMemoryReplayByteCategory::PathCauseRows ) ) );
    fprintf( file,
             "      \"render_scratch\": { \"ghost_requests\": %llu, \"focus_mask\": %llu, "
             "\"launcher_backup\": %llu },\n",
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests ) ),
             static_cast<unsigned long long>(
                 ReplayMemoryCategoryCounter( replay,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderFocusMask ) ),
             static_cast<unsigned long long>( ReplayMemoryCategoryCounter(
                 replay,
                 SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderLauncherBackup ) ) );
    fprintf( file,
             "      \"trajectory\": { \"store\": %llu, \"records\": %llu, \"points\": %llu, "
             "\"published_points\": %llu, \"version_churn\": %llu }\n",
             static_cast<unsigned long long>(
                 ReplayMemoryCategoryCounter( replay,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore ) ),
             static_cast<unsigned long long>( replay.trajectory.recordCount ),
             static_cast<unsigned long long>( replay.trajectory.pointCount ),
             static_cast<unsigned long long>( replay.trajectory.publishedPointCount ),
             static_cast<unsigned long long>( replay.trajectory.versionChurn ) );
    fputs( "    },\n", file );
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
                 owner.ownerName ? owner.ownerName : "",
                 owner.registered ? "true" : "false",
                 owner.hardBytes,
                 static_cast<unsigned long long>( owner.measuredHighWaterBytes ),
                 static_cast<unsigned long long>( owner.allocatorHighWaterBytes ),
                 owner.reportedHighWaterCapacity,
                 static_cast<unsigned long long>( owner.replayGrowths ),
                 static_cast<unsigned long long>( owner.failedGrowths ),
                 owner.lastGrowthFrame,
                 index + 1u < replay.growthOwners.size() ? "," : "" );
    }
    fputs( "    ],\n", file );
}

void WriteReplayTrajectoryCounters( FILE* file,
                                    const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& trajectory )
{
    fprintf(
        file,
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
        static_cast<unsigned long long>( trajectory.versionChurn ),
        trajectory.maxRecordVersion,
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.emittedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryLaneCounter( trajectory.droppedSegments,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryBudgetCounter( trajectory,
                                           SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryBudgetCounter( trajectory,
                                           SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryBudgetCounter( trajectory,
                                           SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBuildTree ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryBudgetCounter( trajectory,
                                           SkullbonezCore::Core::MainMemoryReplayBudgetPass::RetainedRefresh ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryRebuildCounter( trajectory, SkullbonezCore::Core::MainMemoryReplayRebuildCause::Dirty ) ),
        static_cast<unsigned long long>(
            ReplayTrajectoryRebuildCounter( trajectory,
                                            SkullbonezCore::Core::MainMemoryReplayRebuildCause::AutomaticRefresh ) ) );
}
} // namespace

void StepDiagnosticsPhysicsPipelineStage( RunDebugState& debug, int direction )
{
    const int stageCount = static_cast<int>( Physics::PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    debug.physicsDebugFlags |= Physics::PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    debug.physicsDebugPipelineStageCursor = nextStage;
}


bool HandleDiagnosticsKeyboardShortcut( DiagnosticsKeyboardShortcutContext context,
                                        RuntimeInputAction action,
                                        bool wasPressed )
{
    if ( !wasPressed )
    {
        switch ( action )
        {
        case RuntimeInputAction::ToggleWaterFreeze:
        case RuntimeInputAction::CycleWaterReflection:
        case RuntimeInputAction::ToggleWaterFlat:
        case RuntimeInputAction::ToggleTerrainHidden:
        case RuntimeInputAction::ToggleWaterHidden:
        case RuntimeInputAction::ToggleCollisionVisualizer:
        case RuntimeInputAction::CyclePhysicsDebugOverlay:
        case RuntimeInputAction::ToggleTerrainContactProbe:
        case RuntimeInputAction::StepPhysicsPipelinePrevious:
        case RuntimeInputAction::StepPhysicsPipelineNext:
        case RuntimeInputAction::TogglePhysicsDebugTransparent:
        case RuntimeInputAction::ReportRendererRuntimeRetired:
        case RuntimeInputAction::ToggleBroadphaseOverlay:
            return true;
        default:
            return false;
        }
    }

    RunDebugState& debug = context.debug;
    switch ( action )
    {
    case RuntimeInputAction::ToggleWaterFreeze:
        // Numeric water and terrain toggles are visual diagnostics only; they
        // must not feed back into simulation or scene ownership.
        debug.isWaterFreezeDebug = !debug.isWaterFreezeDebug;
        if ( debug.isWaterFreezeDebug )
        {
            debug.frozenWaterTime = static_cast<float>( context.simulationSeconds );
        }
        return true;
    case RuntimeInputAction::CycleWaterReflection:
    {
        // Key '2' cycles FBO mirror rendering, DXR reflection when supported,
        // no reflection, then back to FBO. Machines without DXR skip the
        // unsupported mode instead of leaving the toggle in a dead state.
        const bool dxrReflectionSupported =
            context.renderDiagnostics && context.renderDiagnostics->GetCapabilities().supportsDxrReflection;
        if ( !debug.isWaterRTReflect && !debug.isWaterNoReflect )
        {
            if ( dxrReflectionSupported )
            {
                debug.isWaterRTReflect = true;
            }
            else
            {
                debug.isWaterNoReflect = true;
            }
        }
        else if ( debug.isWaterRTReflect )
        {
            debug.isWaterRTReflect = false;
            debug.isWaterNoReflect = true;
        }
        else
        {
            debug.isWaterNoReflect = false;
        }
        return true;
    }
    case RuntimeInputAction::ToggleWaterFlat:
        debug.isWaterFlatDebug = !debug.isWaterFlatDebug;
        return true;
    case RuntimeInputAction::ToggleTerrainHidden:
        debug.isTerrainHidden = !debug.isTerrainHidden;
        return true;
    case RuntimeInputAction::ToggleWaterHidden:
        debug.isWaterHidden = !debug.isWaterHidden;
        return true;
    case RuntimeInputAction::ToggleCollisionVisualizer:
        debug.isCollisionVisualizer = !debug.isCollisionVisualizer;
        return true;
    case RuntimeInputAction::CyclePhysicsDebugOverlay:
        // C key: None -> Axes -> Contacts -> Sleep -> All -> None.
        switch ( debug.physicsDebugFlags )
        {
        case Physics::PHYSICS_DEBUG_NONE:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_AXES;
            break;
        case Physics::PHYSICS_DEBUG_AXES:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_CONTACTS;
            break;
        case Physics::PHYSICS_DEBUG_CONTACTS:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_SLEEP;
            break;
        case Physics::PHYSICS_DEBUG_SLEEP:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_ALL;
            break;
        default:
            debug.physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE;
            break;
        }
        return true;
    case RuntimeInputAction::ToggleTerrainContactProbe:
        // O key layers the terrain polygon/contact probe over the C-key debug
        // cycle, so it is toggled independently of the cycle state.
        debug.physicsDebugFlags ^= Physics::PHYSICS_DEBUG_TERRAIN_CONTACT;
        return true;
    case RuntimeInputAction::StepPhysicsPipelinePrevious:
        // F7/F8 inspect the bounded physics pipeline stage trace captured by
        // the most recent physics tick; they do not advance simulation.
        StepDiagnosticsPhysicsPipelineStage( debug, -1 );
        return true;
    case RuntimeInputAction::StepPhysicsPipelineNext:
        StepDiagnosticsPhysicsPipelineStage( debug, 1 );
        return true;
    case RuntimeInputAction::TogglePhysicsDebugTransparent:
        // Transparent volumes make contact rows readable inside bodies without
        // changing the collision visualizer's solid debug pass.
        debug.isPhysicsDebugTransparent = !debug.isPhysicsDebugTransparent;
        return true;
    case RuntimeInputAction::ReportRendererRuntimeRetired:
        // Q used to cycle legacy renderers; keep the key as a bounded
        // diagnostic report because DX12 is now the sole runtime backend.
        fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
        return true;
    case RuntimeInputAction::ToggleBroadphaseOverlay:
        // G cycles the tracked ball while the broadphase overlay is off; once
        // the overlay is active, the same key owns overlay visibility.
        if ( context.sceneMode && context.cameraTrackBallIndex >= 0 && !debug.isBroadphaseOverlay )
        {
            if ( context.sceneEntityCount > 0 )
            {
                context.cameraTrackBallIndex = ( context.cameraTrackBallIndex + 1 ) % context.sceneEntityCount;
            }
        }
        else
        {
            debug.isBroadphaseOverlay = !debug.isBroadphaseOverlay;
        }
        return true;
    default:
        return false;
    }
}


DiagnosticsUIKeyboardShortcutResult HandleDiagnosticsUIKeyboardShortcut( DiagnosticsUIKeyboardShortcutContext context,
                                                                         RuntimeInputAction action,
                                                                         bool wasPressed )
{
    DiagnosticsUIKeyboardShortcutResult result;
    switch ( action )
    {
    case RuntimeInputAction::ToggleUIVisibility:
    case RuntimeInputAction::TogglePerformanceHistogram:
    case RuntimeInputAction::ToggleMemoryOverlay:
        result.handled = true;
        break;
    default:
        return result;
    }

    if ( !wasPressed )
    {
        return result;
    }

    result.triggered = true;
    result.releaseMouseToUI = true;
    switch ( action )
    {
    case RuntimeInputAction::ToggleUIVisibility:
        // Concept: The tabbed diagnostics UI owns overlay text once visible, so
        // the legacy one-line overlay is cleared by the UI shortcut owner.
        context.scene.isInteractiveRun = true;
        context.scene.isExitOnComplete = false;
        context.capture.Screenshot().isScreenshotAndExit = false;
        context.ui.ToggleVisible( context.nowSeconds );
        context.debug.overlayMode = OverlayMode::None;
        return result;
    case RuntimeInputAction::TogglePerformanceHistogram:
        // F5/F6 are lightweight diagnostic overlays; they do not implicitly open
        // or close the broader diagnostics window.
        context.ui.TogglePerformanceHistogramEnabled();
        return result;
    case RuntimeInputAction::ToggleMemoryOverlay:
        context.ui.ToggleMemoryOverlayEnabled();
        return result;
    default:
        return result;
    }
}


DiagnosticsPhysicsOverlayUICommandResult
ApplyDiagnosticsPhysicsOverlayUICommands( RunDebugState& debug, const UI::UIPhysicsCommands& commands )
{
    // Why: Physics-tab diagnostics mutate presentation/debug state only. Keeping
    // them here prevents UI command application from reopening direct debug-field
    // ownership in RunInput.
    DiagnosticsPhysicsOverlayUICommandResult result;
    if ( commands.toggleCollisionVisualizer )
    {
        debug.isCollisionVisualizer = !debug.isCollisionVisualizer;
        result.toggledCollisionVisualizer = true;
    }
    if ( commands.togglePhysicsDebugFlags != 0 )
    {
        debug.physicsDebugFlags ^= ( commands.togglePhysicsDebugFlags & Physics::PHYSICS_DEBUG_ALL );
        result.toggledPhysicsDebugFlags = true;
    }
    if ( commands.stepPhysicsPipelinePrevious )
    {
        StepDiagnosticsPhysicsPipelineStage( debug, -1 );
        result.steppedPipelinePrevious = true;
    }
    if ( commands.stepPhysicsPipelineNext )
    {
        StepDiagnosticsPhysicsPipelineStage( debug, 1 );
        result.steppedPipelineNext = true;
    }
    if ( commands.togglePhysicsDebugTransparent )
    {
        debug.isPhysicsDebugTransparent = !debug.isPhysicsDebugTransparent;
        result.toggledPhysicsDebugTransparent = true;
    }
    if ( commands.toggleBroadphaseOverlay )
    {
        debug.isBroadphaseOverlay = !debug.isBroadphaseOverlay;
        result.toggledBroadphaseOverlay = true;
    }
    return result;
}


bool ApplyDiagnosticsTerrainContactProbeUICommand( RunDebugState& debug, const UI::UIPhysicsCommands& commands )
{
    if ( !commands.toggleTerrainContactProbe )
    {
        return false;
    }

    debug.physicsDebugFlags ^= Physics::PHYSICS_DEBUG_TERRAIN_CONTACT;
    return true;
}


DiagnosticsPhysicsDebugValueUICommandResult
ApplyDiagnosticsPhysicsDebugValueUICommands( RunDebugState& debug, const UI::UIPhysicsCommands& commands )
{
    DiagnosticsPhysicsDebugValueUICommandResult result;
    if ( commands.requestedPhysicsDebugAlpha >= 0.0f )
    {
        debug.physicsDebugAlpha = std::clamp( commands.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
        result.setAlpha = true;
    }
    if ( commands.requestedPhysicsDebugContactLinger >= 0.0f )
    {
        debug.physicsDebugContactLinger = std::clamp( commands.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
        result.setContactLinger = true;
    }
    return result;
}


CaptureController& DiagnosticsRuntime::Capture()
{
    return m_capture;
}


const CaptureController& DiagnosticsRuntime::Capture() const
{
    return m_capture;
}


DiagnosticsController& DiagnosticsRuntime::Diagnostics()
{
    return m_diagnostics;
}


const DiagnosticsController& DiagnosticsRuntime::Diagnostics() const
{
    return m_diagnostics;
}


void DiagnosticsRuntime::BindProfiler( SkullbonezCore::Core::Profiler* profiler )
{
    m_diagnostics.BindProfiler( profiler );
}


RunPerfLogState& DiagnosticsRuntime::PerfLog()
{
    return m_diagnostics.PerfLog();
}


const RunPerfLogState& DiagnosticsRuntime::PerfLog() const
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


void DiagnosticsRuntime::LogPerfMemory( int pass, const char* checkpoint )
{
    m_diagnostics.LogPerfMemory( pass, checkpoint );
}


void DiagnosticsRuntime::ResetPerfLogForSceneLoad()
{
    m_diagnostics.ResetPerfLogForSceneLoad();
}


void DiagnosticsRuntime::ResetForSceneLoad( int completedPerfPass )
{
    ClosePerfLogWithMemoryCheckpoint( completedPerfPass, "end" );
    ResetPerfLogForSceneLoad();
    m_capture.ResetScreenshot();
    m_uiStress = UIStressState{};
}


void DiagnosticsRuntime::ConfigurePerfLogFlush( bool enabled, int interval )
{
    m_diagnostics.ConfigurePerfLogFlush( enabled, interval );
}


void DiagnosticsRuntime::OpenScenePerfLog( const char* path, int pass )
{
    m_diagnostics.OpenScenePerfLog( path, pass );
}


void DiagnosticsRuntime::ApplySceneAutomationOptions( const AuthoredScene& scene,
                                                      bool suppressAutomationExit,
                                                      int perfPass )
{
    // Concept: Scene-authored screenshot and perf-log directives are
    // diagnostics automation. Keep the artifact state with DiagnosticsRuntime
    // while scene loading decides when to call it.
    RunScreenshotState& screenshot = m_capture.Screenshot();
    screenshot.screenshotFrame = scene.GetScreenshotFrame();
    screenshot.screenshotMs = scene.GetScreenshotMs();
    screenshot.isScreenshotAndExit = suppressAutomationExit ? false : scene.IsScreenshotAndExit();

    if ( scene.GetScreenshotPath()[0] != '\0' )
    {
        strcpy_s( screenshot.screenshotPath, sizeof( screenshot.screenshotPath ), scene.GetScreenshotPath() );
    }

    screenshot.screenshotInterval = scene.GetScreenshotInterval();
    if ( scene.GetScreenshotDir()[0] != '\0' )
    {
        strcpy_s( screenshot.screenshotDir, sizeof( screenshot.screenshotDir ), scene.GetScreenshotDir() );
        CreateDirectoryA( screenshot.screenshotDir, nullptr );
    }

    const char* perfPath = scene.GetPerfLogPath();
    if ( perfPath[0] != '\0' )
    {
        OpenScenePerfLog( perfPath, perfPass );
    }
}


bool DiagnosticsRuntime::PerfTestActive() const
{
    return m_diagnostics.PerfTestActive();
}


void DiagnosticsRuntime::TickPerfLog( const RuntimePerfTickContext& context )
{
    m_diagnostics.TickPerfLog( context );
}


RuntimeProfilerFrameTimes DiagnosticsRuntime::SampleProfilerFrameTimes() const
{
    return m_diagnostics.SampleProfilerFrameTimes();
}


const SkullbonezCore::Core::MainMemoryStats&
DiagnosticsRuntime::RefreshMainMemoryStats( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                                            const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects,
                                            double nowSeconds,
                                            bool force,
                                            bool includePrivateWorkingSet )
{
    const bool sampleDue = m_lastMainMemorySampleSeconds < 0.0 ||
                           nowSeconds - m_lastMainMemorySampleSeconds >= MAIN_MEMORY_SAMPLE_INTERVAL_SECONDS;
    const bool deepSampleDue = includePrivateWorkingSet && !m_lastMainMemorySampleUsedPrivateWorkingSetQuery;
    if ( !force && !sampleDue && !deepSampleDue )
    {
        return m_mainMemoryStats;
    }

    SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope(
        SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Diagnostics );

    // Concept: The Memory tab wants cheap repeated reads, while shutdown dumps and
    // stress logs need the deep private-working-set query. Build one reconciled
    // snapshot, then cache both the timestamp and the sampling mode.
    SkullbonezCore::Core::MainMemoryStats stats;
    stats.sampleTimeSeconds = nowSeconds;
    stats.process = RuntimeDiagnostics::SampleProcessMemory( includePrivateWorkingSet );
    stats.replay = replay;
    stats.gameObjects = gameObjects;
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
        stats.reconciledTotalBytes =
            stats.trackedEngineBytes + stats.unattributedProcessBytes - stats.trackedOvershootBytes;
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


const char* DiagnosticsRuntime::MainMemoryDumpPath() const
{
    return m_mainMemoryDumpPath;
}


bool DiagnosticsRuntime::MainMemoryDumpRequested() const
{
    return m_mainMemoryDumpPath[0] != '\0';
}


bool DiagnosticsRuntime::WriteMainMemoryDump( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                                              const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects,
                                              const RunSceneState& scene,
                                              const char* checkpoint,
                                              double nowSeconds )
{
    if ( !MainMemoryDumpRequested() )
    {
        return false;
    }

    const SkullbonezCore::Core::MainMemoryStats& stats =
        RefreshMainMemoryStats( replay, gameObjects, nowSeconds, true, true );
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
             scene.currentFrame,
             stats.sampleTimeSeconds,
             stats.process.available ? "true" : "false",
             stats.process.taskManagerMetricName,
             static_cast<unsigned long long>( stats.process.taskManagerBytes ),
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
             static_cast<unsigned long long>( stats.replay.ghostRequests ),
             stats.replay.memoryPreset,
             stats.replay.requestedRetentionSeconds,
             stats.replay.requestedBudgetMiB,
             stats.replay.presentationRetentionSeconds,
             stats.replay.solverRetentionSeconds,
             stats.replay.memoryBudgetClamped ? "true" : "false",
             stats.replay.solverWindowReduced ? "true" : "false" );
    WriteReplayMemoryCategories( file, stats.replay );
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
             scene.currentFrame,
             scene.targetFrameCount,
             scene.modelCount,
             scene.isTestComplete ? "true" : "false" );

    fclose( file );
    fprintf( stdout, "[memory] Wrote main memory dump: %s\n", m_mainMemoryDumpPath );
    return true;
}


#ifdef _DEBUG
RunPhysicsDiagnosticsState& DiagnosticsRuntime::PhysicsDiagnostics()
{
    return m_diagnostics.PhysicsDiagnostics();
}


const RunPhysicsDiagnosticsState& DiagnosticsRuntime::PhysicsDiagnostics() const
{
    return m_diagnostics.PhysicsDiagnostics();
}


bool DiagnosticsRuntime::PhysicsDiagnosticsEnabled() const
{
    return m_diagnostics.PhysicsDiagnosticsEnabled();
}


void DiagnosticsRuntime::SetPhysicsRegressionLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsRegressionLogOverride( m_diagnostics.PerfLog(), path );
}


void DiagnosticsRuntime::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    RuntimeDiagnostics::SetPhysicsCollisionTimeLogOverride( m_diagnostics.PerfLog(), path );
}


void DiagnosticsRuntime::SetPhysicsDiagnosticsPath( Physics::PhysicsEngine& physics,
                                                    const char* path,
                                                    bool fixedStepForcedByDiagnostics )
{
    RuntimeDiagnostics::SetPhysicsDiagnosticsPath( m_diagnostics.PhysicsDiagnostics(),
                                                   physics,
                                                   path,
                                                   fixedStepForcedByDiagnostics );
}


void DiagnosticsRuntime::LogSceneFinished( SceneController& scene,
                                           const Rendering::Dx12Diagnostics* renderDiagnostics,
                                           const char* reason )
{
    const std::string* currentPath = scene.CurrentPath();
    const char* scenePath = currentPath && !currentPath->empty() ? currentPath->c_str() : "generated";
    const char* rendererName = renderDiagnostics ? renderDiagnostics->GetRendererName() : "unknown";
    RuntimeDiagnostics::LogSceneFinished( scene.State(), scenePath, rendererName, reason );
}


void DiagnosticsRuntime::BeginPhysicsDiagnosticsRun( Physics::PhysicsEngine& physics,
                                                     const RunSceneState& scene,
                                                     const SkullbonezCore::Core::EngineConfig& config,
                                                     const char* scenePath,
                                                     const char* rendererName )
{
    // Lifetime: RuntimeDiagnostics owns the trace file/session. This boundary
    // only supplies current runtime state and never caches trace handles.
    RuntimeDiagnostics::BeginPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(),
                                                    physics,
                                                    scene,
                                                    config,
                                                    scenePath,
                                                    rendererName );
}


void DiagnosticsRuntime::LogReplayScrubProbe( const RunSceneState& scene, const ReplayScrubProbeDiagnostic& probe )
{
    RuntimeDiagnostics::LogReplayScrubProbe( m_diagnostics.PhysicsDiagnostics(), scene, probe );
}


void DiagnosticsRuntime::LogReplayRestoreProbe( const RunSceneState& scene, const ReplayRestoreProbeDiagnostic& probe )
{
    RuntimeDiagnostics::LogReplayRestoreProbe( m_diagnostics.PhysicsDiagnostics(), scene, probe );
}


void DiagnosticsRuntime::LogReplayRestoreResult( const RunSceneState& scene,
                                                 const ReplayRestoreResultDiagnostic& result )
{
    // Invariant: Replay restore diagnostics are forwarded with their exact
    // hashes, counts, and flags so SkullScope queries can distinguish checkpoint
    // restores from fallback restores.
    RuntimeDiagnostics::LogReplayRestoreResult( m_diagnostics.PhysicsDiagnostics(), scene, result );
}


void DiagnosticsRuntime::EndPhysicsDiagnosticsRun( const RunSceneState& scene, const char* status )
{
    RuntimeDiagnostics::EndPhysicsDiagnosticsRun( m_diagnostics.PhysicsDiagnostics(), scene, status );
}


#endif


void DiagnosticsRuntime::BeforeSceneUnload( const RunSceneState& scene )
{
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( scene, "scene_reload" );
#else
    (void)scene;
#endif
}


DiagnosticsRuntime::UIStressState& DiagnosticsRuntime::UIStress()
{
    return m_uiStress;
}


const DiagnosticsRuntime::UIStressState& DiagnosticsRuntime::UIStress() const
{
    return m_uiStress;
}
} // namespace Runtime
} // namespace SkullbonezCore
