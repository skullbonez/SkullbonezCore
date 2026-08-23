/*
File: SkullbonezSource/Runtime/App/ReplayRuntime.cpp
Purpose:
  Sequences Replay, Prediction, and Planning owners from Runtime/App.

Summary:
  ReplayRuntime is the application composition boundary between concrete
  Replay, Prediction, and Planning owners. It orders workspace input,
  transactional restore, prediction, artifact, publication, and validation
  behavior without storing sibling backpointers inside those owners. During a
  causal transition it retains the source replay ring across intermediate
  restores, then applies the normal reset only at the exact endpoint. Render
  publication exposes Planning's detached contact packet and copied solver rows
  only while detail is visible.

Glossary:
  Branch: Child replay timeline created from a restored source frame.
  Cause tree row: UI row derived from retained solver contacts or prediction
    future nodes.
  Hash log: Deterministic text stream that lets saved replay output be compared.
  Loaded presentation: Replay artifact data loaded from disk for scrub preview.
  Prediction worker: Amortized task that fills replay-owned prediction build
    frames outside the render thread.

Invariants:
  - Full owner-state accessors are private. External render, input, UI, and
    validation code receives only named read-only publications or commands.
  - Published spans and references are frame-local and must not survive the
    next replay update.
  - Solver hash-log paths derive from the presentation path so paired artifacts
    stay beside each other.
  - Scene and branch reset edges wait for prediction workers before clearing
    replay-owned scratch.
  - Intermediate causal restores cannot clear the timeline that owns their
    later exact-frame targets.
  - A non-preserved scene/timeline reset clears Planning's paired causal surface
    before old scene identities or exact-frame detail can be published again.
  - The render frame receives only the generic contact packet and synchronous
    spans into Planning-owned solver copies, never the transition owner or
    ReplayRecorder borrows.
  - Tracy plots sample existing owner stats only; they never traverse retained
    payloads or influence replay state.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayRuntime.h"
#include "SceneLoadApplication.h"
#include "../../Assets/AssetKeys.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "ReplayReserveInventory.h"
#include "../Replay/ReplayRestoreService.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Replay/ReplayV2Artifact.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/TracyClientOwner.h"
#include "../Tools/RuntimeFileWriter.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/RuntimePickService.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/AmortizedTask.h"
#include "../../Core/Profiler.h"
#include "../Scene/SceneController.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Runtime
{
using namespace ReplayScrubberOperations;
using namespace ReplayTimelineOperations;

namespace
{
using Math::Vector::Vector3;
using Math::Vector::VectorMagSquared;
using Physics::ColliderStore;
using Physics::PhysicsBodyHandle;
using Physics::PhysicsBodyRecord;
using Physics::PhysicsBodyStore;
using Physics::PhysicsEngine;
using Physics::PhysicsPipelineRecord;
using Physics::PhysicsPipelineStageName;

Vector3 ReplayRuntimeAuthoredPathColor( const SceneEntityStore& entities, Physics::PhysicsSceneObjectId sceneObjectId,
                                        const Vector3& fallback ) noexcept
{
    const SceneEntityRecord* entity = entities.TryGet( entities.FindBySceneObjectId( sceneObjectId ) );

    if ( !entity )
    {
        return fallback;
    }

    return Vector3( entity->renderMaterial.baseColor[0], entity->renderMaterial.baseColor[1],
                    entity->renderMaterial.baseColor[2] );
}

constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;

const ReplayPresentationSample*
ReplayRuntimeLoadedPresentationSampleAtNormalized( const std::vector<ReplayPresentationSample>& samples, float normalized )
{
    if ( samples.empty() )
    {
        return nullptr;
    }

    const float t = std::clamp( normalized, 0.0f, 1.0f );
    const std::size_t maxOffset = samples.size() - 1;
    const std::size_t offset = (std::min)( maxOffset, static_cast<std::size_t>( t * maxOffset + 0.5f ) );
    return &samples[offset];
}

float ReplayRuntimeScrubberRetainedPastSeconds( const ReplayRecorderStats& stats )
{
    if ( !stats.enabled || stats.sampleCount < 2 )
    {
        return PHYSICS_FIXED_DT;
    }

    return static_cast<float>( stats.sampleCount - 1 ) * PHYSICS_FIXED_DT;
}

const std::vector<RunReplayPredictionFrame>&
ReplayRuntimeActivePredictionFrames( const RunReplayPredictionState& prediction )
{
    if ( prediction.BuildFramesAreComplete() )
    {
        return prediction.build.buildFrames;
    }

    return prediction.simulation.frames;
}

const std::vector<RunReplayPredictionFrame>&
ReplayRuntimeTimelinePredictionFrames( const RunReplayPredictionState& prediction, std::size_t& outFrameCount )
{
    if ( prediction.BuildPrefixShouldBePresented() )
    {
        outFrameCount = prediction.PublishedBuildFrameCount();
        return prediction.build.buildFrames;
    }

    const std::vector<RunReplayPredictionFrame>& frames = ReplayRuntimeActivePredictionFrames( prediction );
    outFrameCount = prediction.CommittedFrameCount();
    return frames;
}

float ReplayRuntimePredictionAvailableFutureSeconds( const RunReplayPredictionState& prediction )
{
    std::size_t frameCount = 0;
    const std::vector<RunReplayPredictionFrame>& frames = ReplayRuntimeTimelinePredictionFrames( prediction, frameCount );

    if ( frameCount < 2 )
    {
        return 0.0f;
    }

    // Why: prediction.enabled controls whether the future may rebuild, and
    // BuildPrefixShouldBePresented controls whether the in-progress prefix is
    // coherent enough to draw. The scrubber timeline follows that same prefix
    // so the live marker drifts left while prediction unfolds instead of
    // snapping only after the final frame vector swaps in.
    return static_cast<float>( frames[frameCount - 1].frameIndex ) * PHYSICS_FIXED_DT;
}

float ReplayRuntimeScrubberPresentTrackPosition( const ReplayRecorderStats& stats,
                                                 const RunReplayPredictionState& prediction )
{
    const float pastSeconds = (std::max)( PHYSICS_FIXED_DT, ReplayRuntimeScrubberRetainedPastSeconds( stats ) );
    const float futureSeconds = ReplayRuntimePredictionAvailableFutureSeconds( prediction );

    if ( futureSeconds <= PHYSICS_FIXED_DT )
    {
        return 1.0f;
    }

    return std::clamp( pastSeconds / ( pastSeconds + futureSeconds ), 0.05f, 0.995f );
}

bool ReplayRuntimeModelIsRagdollPart( std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                      int modelIndex )
{
    // SimpleRagdoll children share replay visuals with their collection root.
    // This helper keeps that policy local to replay loading/restoration paths.
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( presentationRecords.size() ) )
    {
        return false;
    }

    return presentationRecords[static_cast<std::size_t>( modelIndex )].simpleRagdollPart;
}


} // namespace

ReplayRuntime::ReplayRuntime( Core::SbDiagnosticStore& resultDiagnostics, Core::Profiler* profiler )
    : m_resultDiagnostics( resultDiagnostics ), m_probeRunner( resultDiagnostics ), m_authoring( profiler ),
      m_predictionOwner( resultDiagnostics, profiler ), m_visualPresentation( profiler )
{
}


void ReplayRuntime::ApplyPredictionRevealRate( float revealRate )
{
    m_predictionOwner.SetRevealRatePreservingCursor( static_cast<double>( revealRate ) );
}


ReplayFrameIntentResult ReplayRuntime::ApplyFrameIntent( const ReplayFrameIntent& intent )
{
    ReplayFrameIntentResult result;

    if ( intent.setScrubberVisibility )
    {
        m_scrubberOwner.SetVisible( intent.scrubberVisible, intent.scrubberNow, intent.scrubberHoldSeconds );
    }

    if ( intent.setPredictionEnabled )
    {
        m_predictionOwner.SetEnabled( intent.predictionEnabled );
    }

    if ( intent.setPredictionHorizon )
    {
        m_predictionOwner.SetHorizonSeconds( intent.predictionHorizonSeconds );
    }

    if ( intent.prepareVelocityMutationBaseline )
    {
        result.velocityMutationBaselinePrepared = m_predictionOwner.PrepareVelocityMutationBaseline();
    }

    if ( intent.commitVelocityMutation )
    {
        m_predictionOwner.CommitVelocityMutation();
    }

    if ( intent.clearVelocityEditInputState )
    {
        m_authoring.ClearVelocityEditInputState();
    }

    if ( intent.queryDeterministicRevealReady )
    {
        result.deterministicRevealReady = m_predictionOwner.ReadyForDeterministicReveal();
    }

    if ( intent.armDeterministicReveal )
    {
        m_predictionOwner.ArmDeterministicReveal( intent.revealFrame, intent.resetPresentedRevealFrame );
    }

    if ( intent.applyPredictionRevealRate )
    {
        m_predictionOwner.SetRevealRatePreservingCursor( intent.predictionRevealRate );
    }

    if ( intent.setPathTarget &&
         m_visualPresentation.SetPathTarget( intent.pathTargetId, intent.pathTargetModelRow, intent.pathTargetName ) )
    {
        m_predictionOwner.ClearCache();
        m_predictionOwner.MarkDirty();
    }

    if ( intent.setInterceptTarget )
    {
        m_planningOwner.SetInterceptTarget( intent.interceptTargetId, intent.interceptTargetModelRow );
    }

    if ( intent.hasTripPlannerCommand )
    {
        (void)m_planningOwner.QueueTripPlannerCommand( intent.tripPlannerCommand );
    }

    return result;
}


void ReplayRuntime::RestoreInteractionRecordingBaseline( RunReplayTrack track, float presentationTrackPosition,
                                                         float solverTrackPosition, bool historicalPaused,
                                                         bool liveAdvanceHeld )
{
    m_scrubberOwner.SetTrackPosition( RunReplayTrack::Presentation, std::clamp( presentationTrackPosition, 0.0f, 1.0f ) );
    m_scrubberOwner.SetTrackPosition( RunReplayTrack::Solver, std::clamp( solverTrackPosition, 0.0f, 1.0f ) );
    m_scrubberOwner.SelectTrack( track );
    m_scrubberOwner.SetHistoricalSamplePaused( historicalPaused );
    (void)m_scrubberOwner.SetLiveAdvanceHeld( liveAdvanceHeld );
}

bool ReplayRuntime::RestoreInteractionRecordingCauseBaseline( const ReplayInteractionRecordingCauseState& baseline,
                                                              double now, const ReplayWorkspaceFrameInput& input,
                                                              InputRouter& inputRouter,
                                                              RuntimeInteractionController& interaction, SceneWorld& world,
                                                              AttachedCameraController& attachedCamera,
                                                              CameraControlState& camera, RunMousePickupState& mousePickup )
{
    if ( baseline.mode == ReplayCauseInspectionMode::Inactive )
    {
        m_planningOwner.CauseInspection().Reset();
        return baseline.selectedRow < 0;
    }

    if ( baseline.selectedRow < 0 )
    {
        return false;
    }

    int focusedCameraRow = -1;
    const bool rowsAvailable = m_predictionOwner.BuildCauseTreeRows( m_authoring, m_visualPresentation.PathVisualizer(),
                                                                     CurrentSolverScrubSample(),
                                                                     world.RenderPresentationRecords(), world.BodyStore(),
                                                                     m_visualPresentation.CameraView(), focusedCameraRow );

    if ( !rowsAvailable || baseline.selectedRow >= static_cast<int>( m_authoring.CauseTree().rows.size() ) )
    {
        return false;
    }

    ReplayWorkspaceOutput output;
    ApplyCauseTreeSelection( baseline.selectedRow, input, inputRouter, interaction, world, attachedCamera, camera,
                             mousePickup, output );

    const ReplayCauseInspectionView selected = m_planningOwner.CauseInspectionView();

    if ( selected.mode == ReplayCauseInspectionMode::Inactive || selected.selectedRow != baseline.selectedRow )
    {
        return false;
    }

    ReplayCauseInspectionRecordingState restored;
    restored.mode = baseline.mode;
    restored.activeTab = baseline.activeTab;
    restored.selectedRow = baseline.selectedRow;
    restored.selectedDetailContactRow = baseline.selectedDetailContactRow;
    restored.solverDetailFirstRow = baseline.solverDetailFirstRow;
    restored.rawRecordFirstRow = baseline.rawRecordFirstRow;
    restored.iterationsFirstRow = baseline.iterationsFirstRow;
    restored.sourceFrame = baseline.sourceFrame;
    restored.targetFrame = baseline.targetFrame;
    restored.presentedFrame = baseline.presentedFrame;
    restored.detailVisible = baseline.detailVisible;
    restored.ownsPause = baseline.ownsPause;
    restored.transportPending = baseline.transportPending;
    restored.transportInFlight = baseline.transportInFlight;
    restored.returnIssued = baseline.returnIssued;
    restored.easedProgress = baseline.easedProgress;
    restored.drawerProgress = baseline.drawerProgress;
    m_planningOwner.CauseInspection().RestoreInteractionRecordingBaseline( restored, now );
    return true;
}


bool ReplayRuntime::SaveInteractionRecordingBaseline( const char* path ) const
{
    return path && path[0] != '\0' && SavePresentationWithSolverHashes( path );
}


ReplaySceneTimelineResetInput ReplayTimelineOperations::DescribeReplaySceneTimeline( const SceneController& sceneController, const SkullbonezCore::UI::RunSceneUIOverrideState& uiOverrides,
                                                                                     const SceneSessionState& scene, int sceneObjectCapacity, uint32_t generatedObjectTypeOverride )
{
    const std::string* scenePath = sceneController.CurrentPath();
    const char* sceneLabel = scenePath && !scenePath->empty() ? scenePath->c_str() : "generated";
    ReplaySceneTimelineResetInput replayReset;
    replayReset.sceneLabel = sceneLabel;
    replayReset.isSceneMode = scene.isSceneMode;
    replayReset.modelCount = scene.modelCount;
    replayReset.solverBallCount = scene.solverBallCount;
    replayReset.solverBoxCount = scene.solverBoxCount;
    replayReset.rngSeed = scene.rngSeed;
    replayReset.sceneObjectCapacity = sceneObjectCapacity;
    replayReset.generatedObjectTypeOverride = generatedObjectTypeOverride;
    replayReset.hasUiModelCountOverride = uiOverrides.modelCountOverride >= 0;
    replayReset.hasUiSolverCountOverride = uiOverrides.solverBallCountOverride >= 0 ||
                                           uiOverrides.solverBoxCountOverride >= 0;

    return replayReset;
}


bool ReplayRuntime::RestoreSolverSampleAsLive( ReplayRestoreTransaction& transaction, SceneWorld& world,
                                               SceneSessionState& scene, OverlayDebugState& debug,
                                               RuntimeTools& runtimeTools, const ReplaySolverFrameSample& sample )
{
    SKORE_TRACY_SCOPED_OWNER_ZONE( "Frame/Replay/Restore", ::HashStr( "Frame/Replay/Restore" ) );

    // Invariant: every product and probe entry reaches the same cancellation
    // barrier before mutating live physics authority.
    m_predictionOwner.CancelJob( false );
    transaction.SelectArtifact( 0, 0 );
    ReplaySolverFrameSample liveBackup;

    if ( !ReplayRestoreService::CaptureCurrentSolverSample( world, scene, debug, runtimeTools, sample, liveBackup ) )
    {
        transaction.FailBeforeMutation( "failed to capture live replay backup" );
        return false;
    }

    transaction.CaptureLiveBackup( std::move( liveBackup ) );

    const uint64_t materializedSolverHash = ReplaySolverHashForSample( sample );

    if ( materializedSolverHash != sample.solverHash )
    {
        char payloadReason[224] = {};

        sprintf_s( payloadReason, sizeof( payloadReason ),
                   "restore checkpoint payload hash mismatch: materialized=0x%016llX recorded=0x%016llX",
                   static_cast<unsigned long long>( materializedSolverHash ),
                   static_cast<unsigned long long>( sample.solverHash ) );

        transaction.FailBeforeMutation( payloadReason );
        return false;
    }

    transaction.MarkTopologyPrepared( false, false );
    char applyReason[128] = {};

    if ( !ReplayRestoreService::ApplySolverSampleState( world, scene, debug, runtimeTools, sample, applyReason,
                                                        sizeof( applyReason ) ) )
    {
        transaction.FailBeforeMutation( applyReason[0] != '\0' ? applyReason : "restore apply failed" );
        return false;
    }

    transaction.MarkCheckpointApplied();

    ReplaySolverFrameSample restoredSample;
    const bool hashCaptured = ReplayRestoreService::CaptureCurrentSolverSample( world, scene, debug, runtimeTools, sample,
                                                                                restoredSample );

    transaction.MarkTargetStepped( sample.frameIndex, sample.eventCursor, 0 );

    const uint64_t restoredSolverHash = hashCaptured ? restoredSample.solverHash : 0;
    const uint64_t restoredPresentationHash = hashCaptured ? restoredSample.presentationHash : 0;
    const std::size_t restoredBodyCount = hashCaptured ? restoredSample.bodies.size() : 0;
    (void)restoredPresentationHash;
    const bool hashMatched = hashCaptured && restoredSolverHash == sample.solverHash;
    bool fallbackRestored = false;

    if ( !hashMatched )
    {
        char fallbackReason[128] = {};

        fallbackRestored = ReplayRestoreService::ApplySolverSampleState( world, scene, debug, runtimeTools,
                                                                         transaction.LiveBackup(), fallbackReason,
                                                                         sizeof( fallbackReason ) );
    }

#ifdef _DEBUG
    ReplayRestoreProbeDiagnostic restoreProbe;
    restoreProbe.targetReplayFrame = sample.frameIndex;
    restoreProbe.targetSceneFrame = sample.sceneFrame;
    restoreProbe.targetSolverHash = sample.solverHash;
    restoreProbe.targetPresentationHash = sample.presentationHash;
    restoreProbe.targetBodyCount = sample.bodies.size();
    restoreProbe.restoredSolverHash = restoredSolverHash;
    restoreProbe.restoredPresentationHash = restoredPresentationHash;
    restoreProbe.restoredBodyCount = restoredBodyCount;
    restoreProbe.contactCount = sample.contactCount;
    restoreProbe.pipelineRecordCount = sample.pipelineRecordCount;
    restoreProbe.checkpointBoundary = sample.checkpointBoundary;
    restoreProbe.hashCaptured = hashCaptured;
    restoreProbe.hashMatched = hashMatched;
    restoreProbe.fallbackAttempted = !hashMatched;
    restoreProbe.fallbackRestored = fallbackRestored;
    transaction.RecordRestoreProbeDiagnostic( restoreProbe );
#endif

    // Hazard: a recoverable restore failure may return only after the live
    // backup was reapplied. Continuing from a half-restored solver would make
    // later physics output nondeterministic, so rollback failure is a fatal invariant failure.
    if ( !hashMatched && !fallbackRestored )
    {
        SB_FATAL( "Runtime/ReplayRestore", "Replay restore verification failed and the live backup could not be restored" );
    }

    if ( !hashMatched )
    {
        transaction.MarkLiveBackupApplied();
    }

    if ( !hashCaptured )
    {
        transaction.MarkRolledBack( "restore hash capture failed" );
        return false;
    }

    if ( !hashMatched )
    {
        char mismatchReason[256] = {};

        // Why: this result crosses the automation boundary. Preserve the exact
        // expected/actual values in the returned reason so the caller's captured
        // log is actionable without attaching a debugger or opening SkullScope.
        const ReplaySolverHashBreakdown expectedBreakdown = ReplaySolverHashBreakdownForSample( sample );
        const ReplaySolverHashBreakdown restoredBreakdown = ReplaySolverHashBreakdownForSample( restoredSample );
        const char* mismatchStage = expectedBreakdown.world != restoredBreakdown.world         ? "world"
                                    : expectedBreakdown.counts != restoredBreakdown.counts     ? "counts"
                                    : expectedBreakdown.launcher != restoredBreakdown.launcher ? "launcher"
                                    : expectedBreakdown.snapshot != restoredBreakdown.snapshot ? "snapshot"
                                                                                               : "bodies";

        sprintf_s( mismatchReason, sizeof( mismatchReason ),
                   "restore solver hash mismatch stage=%s restored=0x%016llX expected=0x%016llX bodies=%llu "
                   "expected_bodies=%llu; %s",
                   mismatchStage, static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( sample.solverHash ),
                   static_cast<unsigned long long>( restoredBodyCount ),
                   static_cast<unsigned long long>( sample.bodies.size() ),
                   fallbackRestored ? "live state restored" : "fallback unavailable" );

        transaction.MarkRolledBack( mismatchReason );
        return false;
    }

    transaction.MarkTargetVerified();
    const uint32_t parentBranchId = m_authoring.BeginRestoredBranch( sample.branch, sample.frameIndex, sample.solverHash );

    transaction.PrepareTimelineReset( parentBranchId, sample.sceneFrame, sample.solverHash );
    return true;
}

#ifdef _DEBUG
void ReplayRuntime::PublishRestoreDiagnostic( const ReplayRestoreTransaction& transaction,
                                              DiagnosticsRuntime& diagnosticsRuntime, const SceneSessionState& scene ) const
{
    // Lifetime: transaction-owned diagnostic strings remain valid for this
    // synchronous write; neither DiagnosticsRuntime nor Scene is retained.
    if ( transaction.HasRestoreProbeDiagnostic() )
    {
        diagnosticsRuntime.LogReplayRestoreProbe( ProjectSceneDiagnosticFacts( scene ),
                                                  transaction.RestoreProbeDiagnostic() );
    }

    if ( transaction.HasRestoreResultDiagnostic() )
    {
        diagnosticsRuntime.LogReplayRestoreResult( ProjectSceneDiagnosticFacts( scene ),
                                                   transaction.RestoreResultDiagnostic() );
    }
}
#endif


void ReplayRuntime::AppendOverlayTrace( PhysicsEngine& physics, const SceneEntityStore& entities, EditorTracer& tracer,
                                        const ReplayPredictionPresentationView& prediction,
                                        const ReplayOverlayBuildInput& input, bool drawPredictionOverlay )
{
    const ReplaySolverFrameSample* currentSolverSample = CurrentSolverScrubSample();
    const ReplaySolverFrameSample* presentSample = currentSolverSample;

    if ( !presentSample )
    {
        presentSample = m_timeline.Solver().LatestSample();
    }

    m_predictionOwner.PresentationOwner().RenderPathVisualizer( prediction, m_visualPresentation.PathVisualizer(),
                                                                presentSample, physics, entities, tracer,
                                                                drawPredictionOverlay );

    const PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    m_predictionOwner.PresentationOwner().RenderCauseFocusOverlay( m_visualPresentation.CameraView(),
                                                                   m_authoring.CauseTree(), prediction, currentSolverSample,
                                                                   bodyStore, colliderStore, entities, tracer );

    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    m_authoring.AppendVelocityEditOverlay( path.targetId, path.targetModelRow, physics, input.editorModeEnabled,
                                           input.gesture, tracer );

    const ReplayInterceptView intercept = m_planningOwner.InterceptView();

    if ( intercept.valid )
    {
        const Vector3 color = intercept.intercept ? Vector3( 0.18f, 0.95f, 0.42f ) : Vector3( 1.0f, 0.58f, 0.16f );
        tracer.AddReplayContactMarker( intercept.shipPosition, Math::Vector::ZERO_VECTOR, color.x, color.y, color.z );
        tracer.AddReplayContactMarker( intercept.targetPosition, Math::Vector::ZERO_VECTOR, color.x, color.y, color.z );
        tracer.AddReplayPathSegment( intercept.shipPosition, intercept.targetPosition, color.x, color.y, color.z );
    }

    const ReplayTripPlannerView& planner = m_planningOwner.TripPlannerView();
    const Vector3 shipPathColor = ReplayRuntimeAuthoredPathColor( entities, planner.shipId, Vector3( 0.18f, 0.42f, 0.80f ) );

    for ( std::size_t ghostIndex = 0; ghostIndex < planner.ghostCount; ++ghostIndex )
    {
        const ReplayTripPlannerGhostArc& ghost = planner.ghosts[ghostIndex];
        const float opacity = 0.12f + 0.08f * static_cast<float>( ghostIndex );

        for ( std::size_t pointIndex = 1; pointIndex < ghost.pointCount; ++pointIndex )
        {
            tracer.AddReplayBaselinePathSegment( ghost.points[pointIndex - 1], ghost.points[pointIndex], shipPathColor.x,
                                                 shipPathColor.y, shipPathColor.z, opacity );
        }
    }

    const ReplayGuideArcsView guideArcs = m_planningOwner.GuideArcsView();

    if ( guideArcs.enabled && guideArcs.valid )
    {
        const Vector3 earthPathColor = ReplayRuntimeAuthoredPathColor( entities, guideArcs.earthId,
                                                                       Vector3( 0.06f, 0.16f, 0.28f ) );

        const Vector3 marsPathColor = ReplayRuntimeAuthoredPathColor( entities, guideArcs.marsId,
                                                                      Vector3( 0.25f, 0.09f, 0.03f ) );

        // Concept: guide rings use the thin baseline ribbon lane so they remain
        // subordinate to the brighter simulated ship prediction.
        for ( std::size_t pointIndex = 0; pointIndex < REPLAY_GUIDE_ARC_POINT_COUNT; ++pointIndex )
        {
            const std::size_t nextIndex = ( pointIndex + 1u ) % REPLAY_GUIDE_ARC_POINT_COUNT;
            tracer.AddReplayBaselinePathSegment( guideArcs.earthPoints[pointIndex], guideArcs.earthPoints[nextIndex],
                                                 earthPathColor.x, earthPathColor.y, earthPathColor.z, 0.35f );

            tracer.AddReplayBaselinePathSegment( guideArcs.marsPoints[pointIndex], guideArcs.marsPoints[nextIndex],
                                                 marsPathColor.x, marsPathColor.y, marsPathColor.z, 0.35f );
        }
    }
}


ReplayInputView ReplayRuntime::BuildInputView() const noexcept
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const RunReplayCameraState camera = m_visualPresentation.CameraView();
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();

    ReplayInputView view;
    view.activeInteraction = HasActiveInteractionState();
    view.inspectionCameraActive = camera.active;
    view.inspectionActive = camera.active || scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;
    view.restoreConsumedThisFrame = scrubber.restoreConsumedThisFrame;
    view.scrubPaused = scrubber.historicalSamplePaused;
    view.liveAdvanceHeld = scrubber.liveAdvanceHeld;
    view.velocityEditEnabled = m_authoring.VelocityEdit().enabled;
    view.predictionEnabled = m_predictionOwner.State().enabled;
    view.captureEnabled = m_timeline.Presentation().IsEnabled() || m_timeline.Solver().IsEnabled();
    view.hasPathTarget = path.hasTarget;
    view.hasCameraFocus = camera.focusKind != RunReplayCameraFocusKind::None;
    view.restoreCameraMode = camera.restoreCameraMode;
    view.pathTargetModelRow = path.hasTarget ? path.targetModelRow.value : -1;
    view.activeTrack = scrubber.activeTrack;
    view.presentationTrackPosition = scrubber.presentationPosition;
    view.solverTrackPosition = m_scrubberOwner.TrackPosition( RunReplayTrack::Solver );
    view.solverPresentTrackPosition = SolverPresentTrackPosition();
    view.predictionRevealAvailable = m_predictionOwner.RevealProgress01( view.predictionRevealProgress );
    return view;
}


const RunReplayCauseTreeState& ReplayRuntime::CauseTree() const noexcept
{
    return m_authoring.CauseTree();
}


ReplayCauseInspectionView ReplayRuntime::CauseInspectionView() const noexcept
{
    return m_planningOwner.CauseInspectionView();
}


#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
ReplayAutomationView ReplayRuntime::BuildAutomationView() const
{
    return { m_predictionOwner.State(),
             m_predictionOwner.AutomationCommittedSolverEvidence(),
             m_predictionOwner.AutomationDetailMode(),
             m_planningOwner.PorkchopView(),
             m_planningOwner.TripPlannerView(),
             m_authoring.CauseTree(),
             m_planningOwner.CauseInspectionView(),
             m_visualPresentation.PathVisualizer(),
             m_planningOwner.InterceptView(),
             m_timeline.Presentation(),
             m_timeline.Solver(),
             m_timeline.Events(),
             m_predictionOwner.ActiveFrames(),
             m_scrubberOwner.View(),
             m_timeline.Solver().GetStats(),
             m_timeline.Solver().LatestSample(),
             CurrentSolverScrubSample(),
             CurrentPredictionScrubFrame(),
             m_predictionOwner.PresentationOwner().PublishedVisualPacketView(),
             m_predictionOwner.PresentationOwner().TrajectorySubmissionProbeSnapshot(),
             m_predictionOwner.PresentationOwner().AppearanceInvalidationCount(),
             m_predictionOwner.SolverEvidenceCaptureStats(),
             m_predictionOwner.CollectMemoryStats().evidence,
             CollectMemoryStats(),
             BuildInputView(),
             m_scrubberOwner.TrackPosition( RunReplayTrack::Solver ),
             SolverPresentTrackPosition() };
}
#endif


ReplayOverlay::ReplayOverlayStateView ReplayRuntime::BuildOverlayStateView( bool editorModeEnabled, bool uiVisible, bool uiMinimized, RuntimeInteractionGestureKind gesture,
                                                                            std::span<const Rendering::RenderInstancePresentationRecord> presentation, const PhysicsBodyStore& bodyStore )
{
    int focusedCameraRow = -1;
    (void)m_predictionOwner.BuildCauseTreeRows( m_authoring, m_visualPresentation.PathVisualizer(),
                                                CurrentSolverScrubSample(), presentation, bodyStore,
                                                m_visualPresentation.CameraView(), focusedCameraRow );

    if ( focusedCameraRow >= 0 )
    {
        m_visualPresentation.SetCameraFocusedRow( focusedCameraRow );
    }

    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const ReplayFrameSelection selection = BuildPresentationSelection();

    return { scrubber,
             m_predictionOwner.PresentationView(),
             m_planningOwner.InterceptView(),
             m_planningOwner.PorkchopView(),
             m_planningOwner.TripPlannerView(),
             m_visualPresentation.PathVisualizer(),
             m_authoring.VelocityEdit(),
             m_authoring.CauseTree(),
             m_planningOwner.CauseInspectionView(),
             m_timeline.Solver().GetStats(),
             selection.replay,
             selection.selectedPrediction,
             selection.predictionTimelineAvailable,
             ShouldRenderScrubber( editorModeEnabled, uiVisible, uiMinimized, gesture ),
             m_timeline.RecordingConfigured(),
             m_timeline.RecordingEnabled(),
             m_timeline.RecordingLockedByHashLog() };
}


ReplayFrameSelection ReplayRuntime::BuildPresentationSelection() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const bool loadedPresentation = HasLoadedPresentation();
    const RunReplayTrack track = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const float trackPosition = m_scrubberOwner.TrackPosition( track );
    const float solverPresentTrackPosition = SolverPresentTrackPosition();
    const bool futureSelected = !loadedPresentation &&
                                ReplayTrackPositionIsFuture( trackPosition, solverPresentTrackPosition );

    ReplayFrameSelection selection;
    selection.replay.selectedPresentation = loadedPresentation ? LoadedPresentationSampleAtNormalized( trackPosition )
                                                               : nullptr;

    selection.replay.latestPresentation = loadedPresentation ? LoadedPresentationLatestSample() : nullptr;
    selection.replay.selectedSolver = ( loadedPresentation || futureSelected )
                                          ? nullptr
                                          : m_timeline.Solver().SampleAtNormalized( ReplaySolverNormalizedFromTrack( trackPosition,
                                                                                                                     solverPresentTrackPosition ) );

    selection.replay.latestSolver = loadedPresentation ? nullptr : m_timeline.Solver().LatestSample();
    selection.selectedPrediction = futureSelected ? CurrentPredictionScrubFrame() : nullptr;
    selection.replay.currentPresentation = CurrentScrubSample();
    selection.replay.currentSolver = scrubber.activeTrack == RunReplayTrack::Solver && IsScrubPaused()
                                         ? selection.replay.selectedSolver
                                         : nullptr;

    selection.replay.solverPresentTrackPosition = solverPresentTrackPosition;
    selection.replay.loadedSampleCount = loadedPresentation ? m_timeline.LoadedPresentation().samples.size() : 0u;
    selection.replay.loadedPresentation = loadedPresentation;
    selection.predictionTimelineAvailable = m_predictionOwner.ActiveFrames().size() >= 2 ||
                                            m_predictionOwner.State().BuildPrefixShouldBePresented();

    return selection;
}

ReplayFrameSelection ReplayRuntime::ApplyRenderPose( Rendering::RenderInstanceStore& renderInstances, PhysicsEngine& physics,
                                                     RuntimeTools& runtimeTools )
{
    const ReplayFrameSelection selection = BuildPresentationSelection();
    const RunReplayPredictionFrame* predictionFrame = selection.selectedPrediction;
    const ReplayPresentationSample* presentationSample = selection.replay.currentPresentation;
    const ReplaySolverFrameSample* solverSample = selection.replay.currentSolver;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );

        if ( predictionFrame )
        {
            m_predictionOwner.PresentationOwner().ApplyFrameForRender( renderInstances, PhysicsEngine::ReadBodies( physics ),
                                                                       PhysicsEngine::ReadColliders( physics ),
                                                                       *predictionFrame );
        }
        else if ( presentationSample )
        {
            m_visualPresentation.ApplyPresentationSampleForRender( renderInstances, PhysicsEngine::ReadBodies( physics ),
                                                                   PhysicsEngine::ReadColliders( physics ),
                                                                   *presentationSample );
        }
        else if ( solverSample )
        {
            m_visualPresentation.ApplySolverSampleForRender( renderInstances, PhysicsEngine::ReadBodies( physics ),
                                                             PhysicsEngine::ReadColliders( physics ), *solverSample );

            if ( !m_visualPresentation.HasLauncherVisualBackup() )
            {
                m_visualPresentation.StoreLauncherVisualBackupFrom( runtimeTools );
                runtimeTools.RestoreReplayLauncherVisualSample( solverSample->launcherVisual );
            }
        }
    }
    return selection;
}


void ReplayRuntime::PrepareRenderOverlay( PhysicsEngine& physics, const SceneEntityStore& entities, EditorTracer& tracer,
                                          const Core::ReplayTrajectoryAppearanceConfig& trajectoryAppearance,
                                          bool editorModeEnabled, const RuntimeInteractionGesture& gesture, int sceneFrame,
                                          std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords )
{
    (void)tracer.SetReplayTrajectoryAppearance( trajectoryAppearance );

    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    const bool retainedRenderingActive = m_predictionOwner.PresentationOwner()
                                             .PrepareRetainedGeometryDrawList( prediction,
                                                                               m_visualPresentation.PathVisualizer(),
                                                                               entities,
                                                                               PhysicsEngine::ReadColliders( physics ),
                                                                               tracer, trajectoryAppearance );

    AppendOverlayTrace( physics, entities, tracer, prediction,
                        ReplayOverlayBuildInput { editorModeEnabled, gesture, sceneFrame }, !retainedRenderingActive );

    (void)m_predictionOwner.PresentationOwner().BuildGhostDrawRequests( prediction, presentationRecords,
                                                                        PhysicsEngine::ReadBodies( physics ) );
}


void ReplayRuntime::PublishRenderPacket( EditorTracer& tracer, const Math::Vector::Vector3& cameraTranslation,
                                         const Math::Vector::Vector3& cameraUp, uint64_t replayReserveGrowthEvents )
{
    PROFILE_SCOPED( "Frame/Replay/PublishRenderPacket" );
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    ReplayVisualPacket packet;

    {
        PROFILE_SCOPED( "Frame/Replay/PublishRenderPacket/BuildFrameLocalPacket" );
        packet = tracer.BuildReplayVisualPacket( cameraTranslation, cameraUp );
    }

    {
        PROFILE_SCOPED( "Frame/Replay/PublishRenderPacket/AttachRetained" );
        m_predictionOwner.PresentationOwner().AttachRetainedPredictionGeometry( packet, cameraTranslation, cameraUp );
    }

    {
        PROFILE_SCOPED( "Frame/Replay/PublishRenderPacket/PublishMetadata" );
        m_predictionOwner.PresentationOwner().PublishVisualPacket( packet, prediction,
                                                                   m_visualPresentation.PathVisualizer().targetId,
                                                                   m_timeline.Solver().LatestSample(),
                                                                   replayReserveGrowthEvents );
    }
}


ReplayRenderFrameView ReplayRuntime::BuildRenderFrameView( const ReplayFrameSelection& selection, PhysicsEngine& physics,
                                                           int modelCount, bool collisionVisualizer,
                                                           bool debugTransparentBodyPass )
{
    const RunReplayPredictionFrame* predictionFrame = selection.selectedPrediction;
    const ReplayPresentationSample* presentationSample = selection.replay.currentPresentation;
    const ReplaySolverFrameSample* solverSample = selection.replay.currentSolver;
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    const ReplayInputView inputView = BuildInputView();
    const ReplayCauseInspectionView causeInspection = m_planningOwner.CauseInspectionView();
    bool focusFadeActive = false;

    if ( !inputView.predictionEnabled && !collisionVisualizer && !debugTransparentBodyPass )
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );
        const std::span<const RunReplayPathTraceNode> focusNodes = prediction.enabled
                                                                       ? prediction.futureNodes
                                                                       : std::span<const RunReplayPathTraceNode> {};

        focusFadeActive = m_predictionOwner.PresentationOwner().BuildFocusModelMask( m_visualPresentation.PathVisualizer(),
                                                                                     PhysicsEngine::ReadBodies( physics ),
                                                                                     modelCount, focusNodes );
    }

    return { presentationSample,
             solverSample,
             ( presentationSample || solverSample ) ? nullptr : predictionFrame,
             &m_predictionOwner.PresentationOwner().PublishedVisualPacketView(),
             focusFadeActive ? &m_predictionOwner.PresentationOwner().FocusModelMaskView() : nullptr,
             causeInspection.detailVisible ? causeInspection.contactPresentation : Rendering::ContactManifoldPresentation {},
             inputView.predictionEnabled,
             inputView.liveAdvanceHeld,
             focusFadeActive };
}

void ReplayRuntime::CompleteRenderFrame( bool submissionRendered, int sceneFrame, uint64_t replayReserveGrowthEvents,
                                         RuntimeTools& runtimeTools )
{
    if ( submissionRendered )
    {
        m_predictionOwner.PresentationOwner()
            .RecordTrajectorySubmissionFrame( m_predictionOwner.PresentationOwner().PublishedVisualPacketView().submission,
                                              sceneFrame, replayReserveGrowthEvents );
    }

    CancelRenderFrame( runtimeTools );
}

void ReplayRuntime::CancelRenderFrame( RuntimeTools& runtimeTools )
{
    if ( m_visualPresentation.HasLauncherVisualBackup() )
    {
        m_visualPresentation.RestoreAndClearLauncherVisualBackup( runtimeTools );
    }
}


void ReplayRuntime::ApplyAuthoringPredictionRequest()
{
    const ReplayAuthoringPredictionRequest request = m_authoring.TakePredictionRequest();
    m_predictionOwner.ApplyAuthoringRequest( request, ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS,
                                             ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS );
}


void ReplayRuntime::ClearPathVisualizerState()
{
    m_visualPresentation.ClearPathState();
    m_planningOwner.ClearInterceptTarget();
    m_authoring.ResetCauseTreeRows();
    m_predictionOwner.ClearCache();
    m_predictionOwner.MarkDirty();
}


ReplayPathColorMode ReplayRuntime::CyclePathColorMode() noexcept
{
    return m_visualPresentation.CyclePathColorMode();
}


void ReplayRuntime::ToggleGuideArcs() noexcept
{
    m_planningOwner.ToggleGuideArcs();
}


void ReplayRuntime::SetGuideArcsEnabled( bool enabled ) noexcept
{
    m_planningOwner.SetGuideArcsEnabled( enabled );
}

void ReplayRuntime::TogglePorkchopPanel() noexcept
{
    m_planningOwner.TogglePorkchopPanel();
}

bool ReplayRuntime::QueueTripPlannerCommand( const ReplayTripPlannerCommand& command ) noexcept
{
    return m_planningOwner.QueueTripPlannerCommand( command );
}


ReplayPathPickResult
ReplayRuntime::ApplyPathPick( const ReplayPathPickInput& input, const SceneEntityStore& entities,
                              const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                              std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords )
{
    const ReplayPathPickResult result = m_visualPresentation.TryPickPathTarget( input, entities, bodyStore, colliderStore,
                                                                                presentationRecords,
                                                                                CurrentSolverScrubSample() );

    if ( result.picked )
    {
        m_predictionOwner.ClearCache();
        m_predictionOwner.MarkDirty();
    }
    else if ( result.exitInspectionCamera )
    {
        ClearCameraFocusForRestore();
        ClearPathVisualizerState();
    }

    return result;
}


ReplayPathPickResult ReplayRuntime::ApplyInterceptTargetPick( const ReplayPathPickInput& input,
                                                              const PhysicsBodyStore& bodyStore,
                                                              const ColliderStore& colliderStore )
{
    return m_planningOwner.TryPickInterceptTarget( input, bodyStore, colliderStore );
}


bool ReplayRuntime::RouteWorldPointer( const ReplayWorldPointerInput& input, const SceneEntityStore& entities,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       const Physics::ColliderStore& colliderStore,
                                       std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                       Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                       CameraControlState& camera, RuntimeInteractionController& interaction,
                                       InputRouter& inputRouter )
{
    if ( !input.leftPressed || input.suppressWorldAction || input.editorMode ||
         ( !input.controlDown && input.launcherMode ) )
    {
        return false;
    }

    // Concept: Ctrl+Left outside launcher mode owns the secondary intercept
    // target. Launcher mode retains its established path-root selection.
    const ReplayPathPickResult pickResult = input.controlDown && !input.launcherMode
                                                ? ApplyInterceptTargetPick( input.pick, bodyStore, colliderStore )
                                                : ApplyPathPick( input.pick, entities, bodyStore, colliderStore,
                                                                 presentation );

    if ( pickResult.exitInspectionCamera )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation, m_authoring, cameras, terrain, camera,
                                                            input.restoreCameraMode, input.attachedCameraFollow,
                                                            input.directorGrabbed, interaction, inputRouter );
    }

    return true;
}

bool ReplayRuntime::HasActiveInteractionState() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const RunReplayCameraState camera = m_visualPresentation.CameraView();
    return camera.active || camera.focusKind != RunReplayCameraFocusKind::None || scrubber.historicalSamplePaused ||
           scrubber.liveAdvanceHeld || m_visualPresentation.PathVisualizer().hasTarget ||
           m_planningOwner.HasInterceptTarget() || !m_visualPresentation.PathVisualizer().targets.empty() ||
           m_predictionOwner.State().enabled || m_predictionOwner.State().build.building ||
           m_authoring.VelocityEdit().enabled || m_authoring.CauseTree().selectedRow >= 0 ||
           !m_authoring.CauseTree().rows.empty() || m_planningOwner.HasActiveState();
}


bool ReplayRuntime::ApplyInteractionExit( const ReplayInteractionExitInput& input, PhysicsEngine& physics,
                                          Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                          CameraControlState& camera, RuntimeInteractionController& interaction,
                                          InputRouter& inputRouter )
{
    if ( !input.leavingReplayWorkspace || ( !HasActiveInteractionState() && !input.previousOwnerWasReplay ) )
    {
        return false;
    }

    // Invariant: leaving Replay is a cancellation edge. Restore the pre-plan
    // velocity through the same typed mutation path before reset hides the owner.
    (void)m_planningOwner.CancelActivePlan( physics, m_predictionOwner );

    if ( ClearInteractionForRuntimeTransition( interaction, inputRouter ) )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation, m_authoring, cameras, terrain, camera,
                                                            input.normalizedRestoreMode, input.attachedFollow,
                                                            input.directorGrabbed, interaction, inputRouter );
    }

    return true;
}


void ReplayRuntime::ApplyInputFocusLoss( Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                         CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                                         bool attachedFollow, bool directorGrabbed,
                                         RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );

    if ( m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active ) )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation, m_authoring, cameras, terrain, camera,
                                                            normalizedRestoreMode, attachedFollow, directorGrabbed,
                                                            interaction, inputRouter );
    }

    m_authoring.ClearVelocityEditInputState();
}


void ReplayRuntime::ClearInteractionForSceneLoad( RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    // Invariant: guide visibility is scene-local and always returns to its
    // zero-cost default before an early interaction-cleanup return.
    m_planningOwner.ClearState();
    const RuntimeInteractionTransition transition = interaction.ResetForScene( InteractionExitReason::LoadScene );
    const bool previousOwnerWasReplay = transition.previousOwner == WorldInteractionOwner::ReplayScrub ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayVelocityEdit ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayPrediction ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayBranchTarget ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayCauseTree;

    if ( !HasActiveInteractionState() && !previousOwnerWasReplay )
    {
        return;
    }

    if ( ClearInteractionForRuntimeTransition( interaction, inputRouter ) )
    {
        // Why: scene clear already invalidated the old camera collection and
        // the detached scene camera will replace its policy below. End Replay's
        // presentation ownership without touching the newly populated world.
        m_visualPresentation.EndCameraInspection();
        inputRouter.RequestCursorVisible( true );
    }
}


void ReplayRuntime::ObserveSceneLifecycleAfterClear( const SceneLifecyclePacket& packet,
                                                     RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    if ( m_sceneClearObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        ClearInteractionForSceneLoad( interaction, inputRouter );
    }
}


void ReplayRuntime::ObserveSceneLifecycleAfterActivation( const SceneLifecyclePacket& packet, const ReplaySceneTimelineResetInput& input, InputRouter& inputRouter,
                                                          RuntimeInteractionController& interaction, Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                                          CameraControlState& camera, RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed )
{
    if ( m_sceneActivationObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneActivated ) )
    {
        ResetSceneTimeline( input, inputRouter, interaction, cameras, terrain, camera, normalizedRestoreMode, attachedFollow,
                            directorGrabbed );
    }
}

bool ReplayRuntime::ClearInteractionForRuntimeTransition( RuntimeInteractionController& interaction,
                                                          InputRouter& inputRouter )
{
    ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );
    m_scrubberOwner.SetLiveAdvanceHeld( false );
    m_visualPresentation.SetCameraPauseOwnership( false );
    const bool exitInspectionCamera = m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active ) ||
                                      m_visualPresentation.CameraView().active;

    m_scrubberOwner.SetAllTrackPositions( 1.0f );
    m_scrubberOwner.HideSurface();
    ClearCameraFocusForRestore();
    ClearPathVisualizerState();
    m_predictionOwner.DisableAndClearCache();
    m_planningOwner.ResetTransientPlanState();
    m_authoring.ResetVelocityEdit();
    m_authoring.ResetCauseTreeRows();
    return exitInspectionCamera;
}

ReplayKeyboardVelocityEditResult ReplayRuntime::ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input )
{
    ReplayKeyboardVelocityEditResult result = m_authoring.ApplyKeyboardVelocityEdit( input, m_scrubberOwner,
                                                                                     m_visualPresentation );

    ApplyAuthoringPredictionRequest();
    return result;
}

float ReplayRuntime::SolverPresentTrackPosition() const
{
    return ReplayRuntimeScrubberPresentTrackPosition( m_timeline.Solver().GetStats(), m_predictionOwner.State() );
}

bool ReplayRuntime::ShouldRenderScrubber( bool editorModeEnabled, bool uiVisible, bool uiMinimized,
                                          RuntimeInteractionGestureKind gesture ) const
{
    if ( editorModeEnabled || !uiVisible || !uiMinimized )
    {
        return false;
    }

    const bool loadedPresentation = HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = m_timeline.Solver().GetStats();
    const bool solverReplayEnabled = solverReplayStats.enabled;
    const ReplayScrubberView scrubber = m_scrubberOwner.View();

    // Why: visibility is about whether a replay control surface is armed, not
    // whether enough retained frames exist to enable scrub/prediction tools.
    return ( loadedPresentation || solverReplayEnabled ) &&
           ( scrubber.visible || gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
             gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag || scrubber.historicalSamplePaused ||
             scrubber.liveAdvanceHeld );
}

void ReplayRuntime::ClearCameraFocusForRestore()
{
    const bool ownedSimulationPause = m_visualPresentation.ClearCameraFocus();
    m_authoring.ClearCauseTreeFocus();

    const ReplayScrubberView scrubber = m_scrubberOwner.View();

    if ( ownedSimulationPause && scrubber.liveAdvanceHeld && !scrubber.historicalSamplePaused )
    {
        m_scrubberOwner.SetLiveAdvanceHeld( false );
    }
}

ReplayRecordingActivationResult ReplayRuntime::ConfigureRecording( bool enabled, int retentionSeconds,
                                                                   const char* hashLogPath, int runtimeBodyCapacity )
{
    ReplayRecordingActivationResult activation;
    m_visualPresentation.ReserveLauncherVisualCaptureBuffers();
    activation.configuration = m_timeline.ConfigureRecording( enabled, retentionSeconds, hashLogPath, runtimeBodyCapacity );

    if ( activation.configuration.presentationConfig.enabled )
    {
        // Runtime allocation policy: presentation buffers reserve during replay
        // setup, before steady gameplay begins.
        m_authoring.ReserveCauseTreeRows( REPLAY_CAUSE_TREE_ROW_CAPACITY );
        m_predictionOwner.PresentationOwner().ReserveRecordingBuffers();
    }

    activation.exitInspectionCamera = m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active );
    return activation;
}

bool ReplayRuntime::ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request )
{
    const ReplayMemoryPolicyApplyResult result = m_timeline.ApplyMemoryPolicyRequest( request );

    if ( result.recordersReset )
    {
        m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active );
        m_scrubberOwner.SetAllTrackPositions( 1.0f );
    }

    return result.changed;
}

ReplayShutdownReport ReplayRuntime::FinishShutdown()
{
    m_timeline.FlushHashLogs();
    ReplayShutdownReport report;
    report.presentation = m_timeline.Presentation().GetStats();
    report.solver = m_timeline.Solver().GetStats();
    return report;
}

ReplaySceneTimelineResetResult ReplayRuntime::BeginSceneTimelineReset( const ReplaySceneTimelineResetInput& input )
{
    ReplaySceneTimelineResetResult result;

    // Hazard: scene reset can rebuild live model, body, and collider storage.
    // Prediction workers hold only replay-owned values, but cancellation still
    // waits here before old private-engine snapshots are cleared or replaced.
    m_predictionOwner.CancelJob( true );

    if ( !input.preserveReplayInspection )
    {
        // Scene and generated-timeline resets share this edge. Clearing only
        // ReplayAuthoring rows would leave Planning's old panel/manifold visible.
        m_planningOwner.CauseInspection().Reset();
    }

    if ( SceneTimelineResetClearsBranch( input ) )
    {
        m_authoring.ResetBranch();
    }

    if ( !input.preserveReplayInspection && m_scrubberOwner.LiveAdvanceHeld() )
    {
        m_scrubberOwner.SetLiveAdvanceHeld( false );
        m_visualPresentation.SetCameraPauseOwnership( false );
    }

    if ( !input.preserveReplayInspection && m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active ) )
    {
        result.exitInspectionCamera = true;
    }

    return result;
}


ReplaySceneTimelineResetResult ReplayRuntime::FinishSceneTimelineReset( const ReplaySceneTimelineResetInput& input )
{
    ReplaySceneTimelineResetResult result;

    if ( input.preserveReplaySourceTimeline )
    {
        // Invariant: synchronized causal transport may restore several
        // coalesced intermediate frames. Keep the retained source ring and
        // loaded cursor intact until the Planning transition reaches its exact
        // endpoint; resetting here would erase every later restore target.
        m_authoring.ResetVelocityEdit();
        return result;
    }

    m_timeline.ClearLoadedPresentation();

    if ( !input.preserveReplayInspection )
    {
        ClearCameraFocusForRestore();
        result.exitInspectionCamera = true;
        ClearPathVisualizerState();
    }

    m_authoring.ResetVelocityEdit();

    if ( !m_timeline.Presentation().IsEnabled() )
    {
        return result;
    }

    const char* sceneLabel = input.sceneLabel && input.sceneLabel[0] != '\0' ? input.sceneLabel : "generated";
    m_timeline.Reset( sceneLabel );
    SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::TimelineStart, 0, false, 0, 0, 0, 0, 0, 0,
                                                             sceneLabel ) );

    result.timelineStarted = true;

    // Why: mismatch diagnostics are scoped to the active replay timeline so a
    // noisy prior scene does not suppress the first useful report in this scene.
    m_timeline.ResetCaptureMismatchDiagnostics();

    if ( SceneTimelineRecordsGeneratedConfig( input ) )
    {
        const uint32_t flags = SceneTimelineGeneratedConfigFlags( input );

        SubmitEvent( ReplayEventCommandOperations::BuildGeneratedSceneConfig( flags, input.modelCount, input.solverBallCount,
                                                                              input.solverBoxCount, input.rngSeed,
                                                                              input.sceneObjectCapacity,
                                                                              input.generatedObjectTypeOverride ) );
    }

    return result;
}


void ReplayRuntime::ApplyPastTrajectoryUpdate( const ReplayPastTrajectoryUpdate& update )
{
    if ( !update.apply )
    {
        return;
    }

    m_visualPresentation.ApplyPastTrajectoryUpdate( update.targetId, update.firstFrame, update.builtThroughFrame,
                                                    update.totalFramesEvicted, update.fullRebuildCount,
                                                    update.incrementalTrimCount, update.valid, update.targetModelRow,
                                                    update.targetModelRowRepaired );
}

void ReplayRuntime::AppendSolverTrajectorySampleToStore( const ReplaySolverFrameSample& sample )
{
    ReplayPastTrajectoryUpdate update;
    m_predictionOwner.AppendPastTrajectorySample( m_timeline.Solver().GetStats(), m_visualPresentation.PastTrajectoryView(),
                                                  sample, update );

    ApplyPastTrajectoryUpdate( update );
}

void ReplayRuntime::CaptureFrame( int sceneFrame, float physicsDt, const ReplayWorldPresentationSample& world,
                                  const ReplayCameraSample& camera, Physics::PhysicsEngine& physics,
                                  const Gameplay::TornadoGameplay& tornadoGameplay, const SceneEntityStore& entities,
                                  const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                                  RuntimeTools& runtimeTools )
{
    // Invariant: presentation, solver, and event timelines share the same
    // branch and event cursor for this frame. Save/export code depends on that
    // alignment when it pairs visual frames with restore checkpoints.
    const ReplayLauncherVisualSample& launcherVisual = m_visualPresentation.CaptureLauncherVisual( runtimeTools );
    const ReplaySolverFrameSample* solverSample = m_timeline.CaptureFrame( sceneFrame, physicsDt, world, camera,
                                                                           launcherVisual, physics, tornadoGameplay,
                                                                           entities, bodyStore, colliderStore,
                                                                           m_authoring.Branch() );

    if ( solverSample )
    {
        AppendSolverTrajectorySampleToStore( *solverSample );
    }

#if defined( TRACY_ENABLE )

    if ( SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus().viewerConnected )
    {
        // Why: recorder counts and reserve rows are already-owned fixed
        // snapshots. No-viewer runs skip even these value copies, and the
        // name-based reserve registry is sampled only once per 60 frames.
        const ReplayRecorderStats presentationStats = m_timeline.Presentation().GetStats();
        const ReplayRecorderStats solverStats = m_timeline.Solver().GetStats();
        const ReplayEventRecorderStats eventStats = m_timeline.Events().GetStats();
        SKORE_TRACY_PLOT_VALUE( "Counter/Replay/PresentationRetainedSamples", presentationStats.sampleCount );
        SKORE_TRACY_PLOT_VALUE( "Counter/Replay/PresentationSampleCapacity", presentationStats.sampleCapacity );
        SKORE_TRACY_PLOT_VALUE( "Counter/Replay/SolverRetainedSamples", solverStats.sampleCount );
        SKORE_TRACY_PLOT_VALUE( "Counter/Replay/SolverSampleCapacity", solverStats.sampleCapacity );
        SKORE_TRACY_PLOT_VALUE( "Counter/Replay/RetainedEvents", eventStats.eventCount );
        SKORE_TRACY_PLOT_VALUE( "Counter/Replay/EventCapacity", eventStats.eventCapacity );

        if ( solverStats.nextFrameIndex % 60u == 0u )
        {
            constexpr const char* reserveHighWaterPlots[] = { "Counter/Replay/RecorderReserveHighWaterBytes",
                                                              "Counter/Replay/SolverReserveHighWaterBytes",
                                                              "Counter/Replay/PredictionReserveHighWaterBytes" };

            constexpr const char* reserveCapacityPlots[] = { "Counter/Replay/RecorderReserveHighWaterCapacity",
                                                             "Counter/Replay/SolverReserveHighWaterCapacity",
                                                             "Counter/Replay/PredictionReserveHighWaterCapacity" };

            for ( std::size_t index = 0; index < REPLAY_GROWTH_OWNER_POLICIES.size(); ++index )
            {
                SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView reserveStats;

                if ( SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::
                         CopyOwnerStatsByName( REPLAY_GROWTH_OWNER_POLICIES[index].ownerName, reserveStats ) )
                {
                    SKORE_TRACY_PLOT_VALUE( reserveHighWaterPlots[index], reserveStats.highWaterBytes );
                    SKORE_TRACY_PLOT_VALUE( reserveCapacityPlots[index], reserveStats.highWaterCapacity );
                }
            }
        }
    }
#endif
}

bool ReplayRuntime::HasLoadedPresentation() const
{
    return m_timeline.LoadedPresentation().enabled && m_timeline.LoadedPresentation().samples.size() >= 2;
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationSampleAtNormalized( float normalized ) const
{
    if ( !HasLoadedPresentation() )
    {
        return nullptr;
    }

    return ReplayRuntimeLoadedPresentationSampleAtNormalized( m_timeline.LoadedPresentation().samples, normalized );
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationLatestSample() const
{
    return HasLoadedPresentation() ? &m_timeline.LoadedPresentation().samples.back() : nullptr;
}


bool ReplayRuntime::IsScrubPaused() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();

    if ( !scrubber.historicalSamplePaused )
    {
        return false;
    }

    if ( scrubber.activeTrack == RunReplayTrack::Presentation && HasLoadedPresentation() )
    {
        return !m_timeline.LoadedPresentation().samples.empty();
    }

    const float position = m_scrubberOwner.TrackPosition( scrubber.activeTrack );
    const float presentT = scrubber.activeTrack == RunReplayTrack::Solver ? SolverPresentTrackPosition() : 1.0f;

    if ( ReplayAtPresentTrackPosition( position, presentT ) )
    {
        return false;
    }

    if ( scrubber.activeTrack == RunReplayTrack::Presentation )
    {
        return m_timeline.Presentation().IsEnabled() && m_timeline.Presentation().GetStats().sampleCount != 0u;
    }

    if ( ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return CurrentPredictionScrubFrame() != nullptr;
    }

    return m_timeline.Solver().IsEnabled() && m_timeline.Solver().GetStats().sampleCount != 0u;
}


const ReplayPresentationSample* ReplayRuntime::CurrentScrubSample() const
{
    // Concept: a scrub sample is available only when the active track is paused
    // away from live time. Live presentation should continue drawing the live
    // scene instead of borrowing old retained samples.
    const ReplayScrubberView scrubber = m_scrubberOwner.View();

    if ( scrubber.activeTrack != RunReplayTrack::Presentation )
    {
        return nullptr;
    }

    if ( HasLoadedPresentation() )
    {
        return scrubber.historicalSamplePaused
                   ? LoadedPresentationSampleAtNormalized( m_scrubberOwner.TrackPosition( RunReplayTrack::Presentation ) )
                   : nullptr;
    }

    if ( !IsScrubPaused() )
    {
        return nullptr;
    }

    return m_timeline.Presentation().SampleAtNormalized( m_scrubberOwner.TrackPosition( RunReplayTrack::Presentation ) );
}


const ReplaySolverFrameSample* ReplayRuntime::CurrentSolverScrubSample() const
{
    if ( m_scrubberOwner.View().activeTrack != RunReplayTrack::Solver || !IsScrubPaused() )
    {
        return nullptr;
    }

    const float position = m_scrubberOwner.TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();

    if ( ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    return m_timeline.Solver().SampleAtNormalized( ReplaySolverNormalizedFromTrack( position, presentT ) );
}


const RunReplayPredictionFrame* ReplayRuntime::CurrentPredictionScrubFrame() const
{
    // Concept: prediction frames extend the solver track past the present
    // marker. They are not retained history, so only the future side of the
    // normalized track can resolve to a prediction frame. Prediction.enabled is
    // deliberately not checked here: an explicit transition back to live time
    // can freeze rebuilds while keeping the committed future scrubbable.
    std::size_t frameCount = 0;
    const std::vector<RunReplayPredictionFrame>& frames = ReplayRuntimeTimelinePredictionFrames( m_predictionOwner.State(),
                                                                                                 frameCount );

    const ReplayScrubberView scrubber = m_scrubberOwner.View();

    if ( scrubber.activeTrack != RunReplayTrack::Solver || !scrubber.historicalSamplePaused || frameCount < 2 )
    {
        return nullptr;
    }

    const float position = m_scrubberOwner.TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();

    if ( !ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    const float predictionT = ReplayPredictionNormalizedFromTrack( position, presentT );
    const std::size_t frameIndex = (std::min)( frameCount - 1, static_cast<std::size_t>( std::round( predictionT * static_cast<float>( frameCount - 1 ) ) ) );

    return &frames[frameIndex];
}
SkullbonezCore::Core::MainMemoryReplayStats ReplayRuntime::CollectMemoryStats() const
{
    SkullbonezCore::Core::MainMemoryReplayStats stats;
    const ReplayTimelineMemoryStats timelineMemory = m_timeline.CollectMemoryStats();
    const ReplayPredictionMemoryStats predictionMemory = m_predictionOwner.CollectMemoryStats();
    const ReplayPredictionPresentationMemoryStats predictionVisualMemory = m_predictionOwner.PresentationOwner()
                                                                               .CollectMemoryStats();

    const ReplayPresentationMemoryStats visualMemory = m_visualPresentation.CollectMemoryStats();
    const ReplayAuthoringMemoryStats authoringMemory = m_authoring.CollectMemoryStats();

    // Concept: each concrete owner reports its own capacities into the shared
    // fixed category table. The composition root only merges those value
    // snapshots and allocator-registration facts; it never traverses owner
    // storage to rediscover memory authority.
    stats.categoryBytes = timelineMemory.categoryBytes;

    for ( std::size_t index = 0; index < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_BYTE_CATEGORY_COUNT; ++index )
    {
        stats.categoryBytes.bytes[index] += predictionMemory.categoryBytes.bytes[index];
    }

    stats.presentationBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner );

    stats.solverBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner );

    stats.eventsBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner );

    stats.presentationSamples = timelineMemory.presentationSamples;
    stats.solverSamples = timelineMemory.solverSamples;
    stats.eventSamples = timelineMemory.eventSamples;
    stats.loadedReplaySamples = timelineMemory.loadedSamples;
    stats.memoryPreset = static_cast<int>( timelineMemory.policy.preset );
    stats.requestedRetentionSeconds = timelineMemory.policy.requestedRetentionSeconds;
    stats.requestedBudgetMiB = timelineMemory.policy.requestedBudgetMiB;
    stats.presentationRetentionSeconds = timelineMemory.policy.presentationRetentionSeconds;
    stats.solverRetentionSeconds = timelineMemory.policy.solverRetentionSeconds;
    stats.memoryBudgetClamped = timelineMemory.policy.budgetClamped;
    stats.solverWindowReduced = timelineMemory.policy.solverWindowReduced;

    // The policy table is stable and fixed-size; diagnostics never discovers
    // replay owners by scanning recent-event text or allocating a report map.
    for ( std::size_t index = 0; index < REPLAY_GROWTH_OWNER_POLICIES.size(); ++index )
    {
        const ReplayGrowthOwnerPolicy& policy = REPLAY_GROWTH_OWNER_POLICIES[index];
        SkullbonezCore::Core::MainMemoryReplayStats::GrowthOwner& growth = stats.growthOwners[index];
        growth.ownerName = policy.ownerName;
        growth.hardBytes = policy.hardBytes;
        growth.measuredHighWaterBytes = policy.measuredHighWaterBytes;
        SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView ownerStats = {};

        growth
            .registered = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CopyOwnerStatsByName( policy.ownerName,
                                                                                                           ownerStats );

        if ( growth.registered )
        {
            growth.allocatorHighWaterBytes = ownerStats.highWaterBytes;
            growth.replayGrowths = ownerStats.replayGrowths;
            growth.failedGrowths = ownerStats.failedGrowths;
            growth.reportedHighWaterCapacity = ownerStats.highWaterCapacity;
            growth.lastGrowthFrame = ownerStats.lastGrowthFrame;
        }
    }

    stats.loadedReplayBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner );

    stats.predictionBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner );

    stats.predictionFrames = predictionMemory.frameCount;
    stats.predictionEvidence.buildContactCapacityBytes = predictionMemory.evidence.build.contactCapacityBytes;
    stats.predictionEvidence.buildPipelineCapacityBytes = predictionMemory.evidence.build.pipelineCapacityBytes;
    stats.predictionEvidence.buildFrameCapacityBytes = predictionMemory.evidence.build.frameCapacityBytes;
    stats.predictionEvidence.committedContactCapacityBytes = predictionMemory.evidence.committed.contactCapacityBytes;
    stats.predictionEvidence.committedPipelineCapacityBytes = predictionMemory.evidence.committed.pipelineCapacityBytes;
    stats.predictionEvidence.committedFrameCapacityBytes = predictionMemory.evidence.committed.frameCapacityBytes;
    stats.predictionEvidence.currentCapacityBytes = predictionMemory.evidence.currentCapacityBytes;
    stats.predictionEvidence.lifetimePeakCapacityBytes = predictionMemory.evidence.lifetimePeakCapacityBytes;
    stats.predictionEvidence.releaseCheckpointCount = predictionMemory.evidence.releaseCheckpointCount;
    stats.predictionEvidence.lastReleaseBeforeCapacityBytes = predictionMemory.evidence.lastReleaseBeforeCapacityBytes;
    stats.predictionEvidence.lastReleaseAfterCapacityBytes = predictionMemory.evidence.lastReleaseAfterCapacityBytes;
    stats.predictionEvidence.lastReleaseBeforeReplayTotalBytes = m_predictionEvidenceReleaseBeforeReplayTotalBytes;
    stats.predictionEvidence.lastReleaseAfterReplayTotalBytes = m_predictionEvidenceReleaseAfterReplayTotalBytes;
    stats.predictionEvidence.lastReleaseBeforeCategoryTotalBytes = m_predictionEvidenceReleaseBeforeCategoryTotalBytes;
    stats.predictionEvidence.lastReleaseAfterCategoryTotalBytes = m_predictionEvidenceReleaseAfterCategoryTotalBytes;
    stats.predictionEvidence.buildContactCount = predictionMemory.evidence.build.contactCount;
    stats.predictionEvidence.buildPipelineCount = predictionMemory.evidence.build.pipelineCount;
    stats.predictionEvidence.buildFrameCount = predictionMemory.evidence.build.publishedFrameCount;
    stats.predictionEvidence.committedContactCount = predictionMemory.evidence.committed.contactCount;
    stats.predictionEvidence.committedPipelineCount = predictionMemory.evidence.committed.pipelineCount;
    stats.predictionEvidence.committedFrameCount = predictionMemory.evidence.committed.publishedFrameCount;

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner,
                                                            visualMemory.pathOwnerBytes + authoringMemory.ownerBytes );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PathFutureNodes,
                                          uint64_t { 0 } );

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PathTargets,
                                                            visualMemory.pathTargetCapacityBytes );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PathCauseRows,
                                          authoringMemory.causeRowCapacityBytes );

    stats.pathAndCauseBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests );

    stats.pathNodes = predictionMemory.futureNodeCount;
    stats.causeRows = authoringMemory.causeRowCount;

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests,
                                          predictionVisualMemory.ghostRequestCapacityBytes );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderFocusMask,
                                          predictionVisualMemory.focusModelMaskCapacityBytes );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderLauncherBackup,
                                          visualMemory.launcherVisualBytes );

    stats.renderScratchBytes = SkullbonezCore::Core::
        MainMemoryReplayCategoryRangeBytes( stats.categoryBytes,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests,
                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore );

    stats.ghostRequests = predictionVisualMemory.ghostRequestCount;
    stats.trajectory = predictionVisualMemory.trajectory;
    stats.trajectory.storeBytes = predictionMemory.trajectory.storeBytes;
    stats.trajectory.recordCount = predictionMemory.trajectory.recordCount;
    stats.trajectory.pointCount = predictionMemory.trajectory.pointCount;
    stats.trajectory.publishedPointCount = predictionMemory.trajectory.publishedPointCount;
    stats.trajectory.versionChurn = predictionMemory.trajectory.versionChurn;
    stats.trajectory.maxRecordVersion = predictionMemory.trajectory.maxRecordVersion;

    stats.totalBytes = stats.presentationBytes + stats.solverBytes + stats.eventsBytes + stats.loadedReplayBytes +
                       stats.predictionBytes + stats.pathAndCauseBytes + stats.renderScratchBytes +
                       stats.trajectory.storeBytes;

    return stats;
}


ReplayHudStatus ReplayRuntime::BuildHudStatus( bool includeMemoryStats ) const
{
    ReplayHudStatus status;
    const ReplayMemoryPolicy& policy = m_timeline.MemoryPolicy();
    status.memoryPreset = static_cast<int>( policy.preset );
    status.requestedRetentionSeconds = policy.requestedRetentionSeconds;
    status.requestedBudgetMiB = policy.requestedBudgetMiB;
    status.presentationRetentionSeconds = policy.presentationRetentionSeconds;
    status.solverRetentionSeconds = policy.solverRetentionSeconds;
    status.memoryBudgetClamped = policy.budgetClamped;
    status.solverWindowReduced = policy.solverWindowReduced;
    status.divergenceUnits = m_predictionOwner.State().baseline.divergenceUnits;
    status.divergenceValid = m_predictionOwner.State().baseline.divergenceValid;
    status.predictionRevealRate = static_cast<float>( m_predictionOwner.State().revealClock.secondsPerSecond );

    if ( includeMemoryStats )
    {
        status.memoryStats = CollectMemoryStats();
        status.memoryStatsValid = true;
    }

    return status;
}

void ReplayRuntime::SubmitEvent( const ReplayEventCommand& command )
{
    m_timeline.SubmitEvent( command, m_authoring.Branch() );
}

bool ReplayRuntime::SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result,
                                                      std::span<const ReplayVisualArchiveSample> visualPackets,
                                                      std::span<const uint8_t> visualPredictionState ) const
{
    SKORE_TRACY_SCOPED_OWNER_ZONE( "Frame/Replay/ColdIO/SavePresentation",
                                   ::HashStr( "Frame/Replay/ColdIO/SavePresentation" ) );

    std::vector<uint8_t> fallbackPredictionState;

    if ( !visualPackets.empty() && visualPredictionState.empty() &&
         !m_predictionOwner.BuildArchive( m_visualPresentation.PathVisualizer(), fallbackPredictionState ) )
    {
        return false;
    }

    const std::span<const uint8_t> predictionState = !visualPredictionState.empty()
                                                         ? visualPredictionState
                                                         : std::span<const uint8_t>( fallbackPredictionState );

    return ReplayV2Artifact::SavePresentationWithSolverHashes( m_timeline.Presentation(), m_timeline.Solver(),
                                                               m_timeline.Events(), visualPackets, predictionState, path,
                                                               result );
}

void ReplayRuntime::UpdatePrediction( PhysicsEngine& physics, const Gameplay::TornadoGameplay& tornadoGameplay,
                                      const SceneEntityStore& entities, const SkullbonezCore::Core::EngineConfig& config,
                                      const Physics::PhysicsWorldForces& worldForces,
                                      ReplayPredictionPathPresentation pathPresentation, Threading::WorkerPool& workerPool,
                                      bool scenePhysicsEnabled, double simulationTimeSinceLastStart,
                                      double simulationTotalTime )
{
    // Concept: the composition root samples owner values, then prediction
    // advances without a ReplayRuntime reach-back. Its value-only result is
    // applied after the worker/publication transition returns.
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    m_planningOwner.BeginFrameBeforePrediction( physics, entities, worldForces, path, m_predictionOwner.PresentationView(),
                                                scrubber.liveAdvanceHeld, m_predictionOwner );

    const ReplaySolverFrameSample* latestSolverSample = m_timeline.Solver().LatestSample();
    const float solverTrackPosition = m_scrubberOwner.TrackPosition( RunReplayTrack::Solver );
    const float solverPresentTrackPosition = SolverPresentTrackPosition();
    const auto budgetStart = std::chrono::steady_clock::now();
    ReplayPredictionUpdateResult result;
    bool wasDirty = false;
    bool wasPendingLatestRestart = false;
    const ReplayPredictionFrameSourceAction sourceAction = m_predictionOwner.SelectFrameSource( latestSolverSample,
                                                                                                path.targetId,
                                                                                                path.hasTarget,
                                                                                                scrubber.liveAdvanceHeld,
                                                                                                simulationTotalTime,
                                                                                                wasDirty,
                                                                                                wasPendingLatestRestart );

    bool stopFrame = sourceAction == ReplayPredictionFrameSourceAction::Stop;

    if ( sourceAction == ReplayPredictionFrameSourceAction::Begin )
    {
        stopFrame = m_predictionOwner.BeginFrameBudgetExpired( budgetStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS,
                                                               result );

        if ( !stopFrame )
        {
            m_predictionOwner.PrepareFrameRebuild( path.targetId, path.targetModelRow, result );
            const ReplayPredictionSourcePreparation
                preparation = m_predictionOwner.BeginFrameSource( physics, config, scenePhysicsEnabled,
                                                                  simulationTimeSinceLastStart, simulationTotalTime,
                                                                  latestSolverSample, path.targetId, path.targetModelRow,
                                                                  path.hasTarget, budgetStart,
                                                                  REPLAY_PREDICTION_MAX_WORK_MILLISECONDS, result );

            const bool began = preparation != ReplayPredictionSourcePreparation::Declined &&
                               m_predictionOwner.BeginFrameSimulation( physics, tornadoGameplay, entities, config,
                                                                       worldForces, pathPresentation, workerPool,
                                                                       preparation );

            m_predictionOwner.CompleteFrameSourceBegin( began, wasDirty, wasPendingLatestRestart );
            stopFrame = m_predictionOwner.BeginFrameBudgetExpired( budgetStart, REPLAY_PREDICTION_MAX_WORK_MILLISECONDS,
                                                                   result );
        }
    }

    if ( !stopFrame &&
         m_predictionOwner.AdvanceFrameWorker( workerPool, simulationTotalTime, scrubber.historicalSamplePaused,
                                               solverTrackPosition, solverPresentTrackPosition, budgetStart,
                                               REPLAY_PREDICTION_MAX_WORK_MILLISECONDS, result ) )
    {
        m_predictionOwner.PublishCompletedFrame( entities, path.targetId );
    }

    ApplyPredictionUpdateResult( result );
    PreparePredictionPresentation( physics, entities );
    m_planningOwner.FinishFrameAfterPrediction( physics, entities, worldForces, simulationTotalTime, path,
                                                m_predictionOwner.PresentationView(), scrubber.liveAdvanceHeld,
                                                m_predictionOwner );
}


void ReplayRuntime::ApplyPredictionUpdateResult( const ReplayPredictionUpdateResult& result )
{
    if ( result.targetModelRowRepaired )
    {
        m_visualPresentation.SetPathTargetModelRow( result.repairedTargetModelRow );
    }

    if ( result.pinSolverScrubberToPresent )
    {
        m_scrubberOwner.SetTrackPosition( RunReplayTrack::Solver, SolverPresentTrackPosition() );

        if ( m_scrubberOwner.View().activeTrack == RunReplayTrack::Solver )
        {
            m_scrubberOwner.SetHistoricalSamplePaused( false );
        }
    }

    for ( std::size_t passIndex = 0; passIndex < result.budgetExpiries.size(); ++passIndex )
    {
        for ( uint32_t count = 0; count < result.budgetExpiries[passIndex]; ++count )
        {
            m_predictionOwner.PresentationOwner().RecordTrajectoryBudgetExpiry( static_cast<SkullbonezCore::Core::MainMemoryReplayBudgetPass>( passIndex ) );
        }
    }

    for ( std::size_t causeIndex = 0; causeIndex < result.rebuildCauses.size(); ++causeIndex )
    {
        for ( uint32_t count = 0; count < result.rebuildCauses[causeIndex]; ++count )
        {
            m_predictionOwner.PresentationOwner().RecordTrajectoryRebuildCause( static_cast<SkullbonezCore::Core::MainMemoryReplayRebuildCause>( causeIndex ) );
        }
    }
}


void ReplayRuntime::PreparePredictionPresentation( PhysicsEngine& physics, const SceneEntityStore& entities )
{
    // Why: live frames and CPU-only archive projection share this publication
    // command. Keeping it separate from drawing prevents validation from
    // becoming a privileged back door into the prediction owner's state.
    const ColliderStore& colliderStore = PhysicsEngine::ReadColliders( physics );
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    ReplayPredictionUpdateResult result;
    m_predictionOwner.PreparePresentation( entities, colliderStore, path.targetId, path.targetModelRow, path.hasTarget,
                                           REPLAY_PREDICTION_MAX_WORK_MILLISECONDS, result );

    ApplyPredictionUpdateResult( result );

    if ( m_predictionOwner.PresentationView().generationPermitted )
    {
        ApplyPastTrajectoryUpdate( m_predictionOwner.RefreshPastTrajectoryStore( m_timeline.Solver(), m_visualPresentation.PastTrajectoryView() ) );
    }

    m_visualPresentation.PreparePathDrawing( PhysicsEngine::ReadBodies( physics ) );
}


} // namespace SkullbonezCore::Runtime
