/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
Purpose:
  Sequences replay owners across recording, workspace, restore, prediction, and probes.

Mental model:
  ReplayRuntime is the composition boundary between concrete replay owners. The
  application shell supplies value commands and explicit synchronous owners;
  this file orders workspace input, transactional restore, prediction,
  artifact, publication, and validation behavior.

Glossary:
  Branch: Child replay timeline created from a restored source frame.
  Body store: Physics-owned live body records used for pose and velocity
    authority while legacy object-record mirrors are retired.
  Cause tree row: UI row derived from retained solver contacts or prediction
    future nodes.
  Collider store: Physics-owned shape, material, and radius records paired with
    body handles.
  Hash log: Deterministic text stream that lets saved replay output be compared.
  Loaded presentation: Replay artifact data loaded from disk for scrub preview.
  Prediction worker: Amortized task that fills replay-owned prediction build
    frames outside the render thread.
  Ragdoll part: One body inside a multi-body SimpleRagdoll collection.
  Velocity edit: Replay tool state for selecting one path-target body and
    editing its linear or angular velocity vectors.

Invariants:
  - Full owner-state accessors are private. External render, input, UI, and
    validation code receives only named read-only publications or commands.
  - Published spans and references are frame-local and must not survive the
    next replay update.
  - Solver hash-log paths derive from the presentation path so paired artifacts
    stay beside each other.
  - Scene and branch reset edges wait for prediction workers before clearing
    replay-owned scratch.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
*/
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "ReplayOverlayLayout.h"
#include "ReplayRetainedMemory.h"
#include "ReplayRestoreService.h"
#include "ReplayRestoreTransactions.h"
#include "ReplayV2Artifact.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../RuntimeFileWriter.h"
#include "../InputRouter.h"
#include "../RuntimeInteractionCommands.h"
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

constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;

const ReplayPresentationSample*
ReplayRuntimeLoadedPresentationSampleAtNormalized( const std::vector<ReplayPresentationSample>& samples,
                                                   float normalized )
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
    outFrameCount = frames.size();
    return frames;
}

float ReplayRuntimePredictionAvailableFutureSeconds( const RunReplayPredictionState& prediction )
{
    std::size_t frameCount = 0;
    const std::vector<RunReplayPredictionFrame>& frames =
        ReplayRuntimeTimelinePredictionFrames( prediction, frameCount );
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

ReplayRuntime::ReplayRuntime() = default;


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
    return result;
}


ReplaySceneTimelineResetInput
ReplayTimelineOperations::DescribeReplaySceneTimeline( const SceneController& sceneController,
                                                       const RunSceneState& scene,
                                                       int gameModelCapacity,
                                                       uint32_t generatedObjectTypeOverride )
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
    replayReset.gameModelCapacity = gameModelCapacity;
    replayReset.generatedObjectTypeOverride = generatedObjectTypeOverride;
    replayReset.hasUiModelCountOverride = sceneController.UIOverrides().modelCountOverride >= 0;
    replayReset.hasUiSolverCountOverride = sceneController.UIOverrides().solverBallCountOverride >= 0 ||
                                           sceneController.UIOverrides().solverBoxCountOverride >= 0;
    return replayReset;
}


bool ReplayRuntime::RestoreSolverSampleAsLive( const ReplayRestoreTransaction& transaction,
                                               const ReplaySolverFrameSample& sample,
                                               char* outReason,
                                               std::size_t reasonSize )
{
    auto writeReason = [outReason, reasonSize]( const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, message ? message : "restore failed", _TRUNCATE );
        }
    };

    ReplaySolverFrameSample liveBackup;
    if ( !ReplayRestoreService::CaptureCurrentSolverSample( transaction.sampleOwners, sample, liveBackup ) )
    {
        writeReason( "failed to capture live replay backup" );
        return false;
    }

    char applyReason[128] = {};
    if ( !ReplayRestoreService::ApplySolverSampleState( transaction.sampleOwners,
                                                        sample,
                                                        applyReason,
                                                        sizeof( applyReason ) ) )
    {
        writeReason( applyReason[0] != '\0' ? applyReason : "restore apply failed" );
        return false;
    }

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    const bool hashCaptured = ReplayRestoreService::CaptureCurrentSolverHash( transaction.sampleOwners,
                                                                              sample,
                                                                              restoredSolverHash,
                                                                              restoredPresentationHash,
                                                                              restoredBodyCount );
    const bool hashMatched = hashCaptured && restoredSolverHash == sample.solverHash;
    bool fallbackRestored = false;
    if ( !hashMatched )
    {
        char fallbackReason[128] = {};
        fallbackRestored = ReplayRestoreService::ApplySolverSampleState( transaction.sampleOwners,
                                                                         liveBackup,
                                                                         fallbackReason,
                                                                         sizeof( fallbackReason ) );
    }

#ifdef _DEBUG
    transaction.diagnostics.LogReplayRestoreProbe( transaction.sampleOwners.scene,
                                                   sample,
                                                   restoredSolverHash,
                                                   restoredPresentationHash,
                                                   restoredBodyCount,
                                                   hashCaptured,
                                                   hashMatched,
                                                   !hashMatched,
                                                   fallbackRestored );
#endif

    // Hazard: a recoverable restore failure may return only after the live
    // backup was reapplied. Continuing from a half-restored solver would make
    // later physics output nondeterministic, so rollback failure is Lane F.
    if ( !hashMatched && !fallbackRestored )
    {
        SB_FATAL( "Runtime/ReplayRestore",
                  "Replay restore verification failed and the live backup could not be restored" );
    }
    if ( !hashCaptured )
    {
        writeReason( "restore hash capture failed" );
        return false;
    }
    if ( !hashMatched )
    {
        writeReason( fallbackRestored ? "restore hash mismatch; live state restored"
                                      : "restore hash mismatch; fallback unavailable" );
        return false;
    }

    const uint32_t parentBranchId =
        m_authoring.BeginRestoredBranch( sample.branch, sample.frameIndex, sample.solverHash );
    ReplaySceneTimelineResetInput reset = transaction.timelineReset;
    reset.preserveBranchMetadata = true;
    ResetSceneTimeline( reset, transaction.timelineOwners );
    SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::BranchRestore,
                                                             0,
                                                             false,
                                                             0,
                                                             static_cast<int32_t>( parentBranchId ),
                                                             sample.sceneFrame,
                                                             0,
                                                             0,
                                                             sample.solverHash,
                                                             "hash-verified solver restore" ) );
    writeReason( "restored hash match" );
    return true;
}


void ReplayRuntime::AppendOverlayTrace( PhysicsEngine& physics,
                                        const SceneEntityStore& entities,
                                        RunEditorTracer& tracer,
                                        const ReplayOverlayBuildInput& input )
{
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    const ReplaySolverFrameSample* currentSolverSample = CurrentSolverScrubSample();
    const ReplaySolverFrameSample* presentSample = currentSolverSample;
    if ( !presentSample )
    {
        presentSample = m_timeline.Solver().LatestSample();
    }
    m_visualPresentation.RenderPathVisualizer( prediction, presentSample, physics, entities, tracer );
    const PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    m_visualPresentation.RenderCauseFocusOverlay( m_authoring.CauseTree(),
                                                  prediction,
                                                  currentSolverSample,
                                                  bodyStore,
                                                  colliderStore,
                                                  entities,
                                                  tracer );
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    m_authoring.AppendVelocityEditOverlay( path.targetId,
                                           path.targetModelRow,
                                           physics,
                                           input.editorModeEnabled,
                                           input.gesture,
                                           tracer );
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
    view.solverPresentTrackPosition = SolverPresentTrackPosition();
    view.predictionRevealAvailable = m_predictionOwner.RevealProgress01( view.predictionRevealProgress );
    return view;
}


#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
ReplayAutomationView ReplayRuntime::BuildAutomationView() const
{
    return { m_predictionOwner.State(),
             m_visualPresentation.PathVisualizer(),
             m_timeline.Presentation(),
             m_timeline.Solver(),
             m_timeline.Events(),
             m_predictionOwner.ActiveFrames(),
             m_scrubberOwner.View(),
             m_timeline.Solver().GetStats(),
             m_timeline.Solver().LatestSample(),
             CurrentSolverScrubSample(),
             CurrentPredictionScrubFrame(),
             m_visualPresentation.PublishedVisualPacketView(),
             m_visualPresentation.TrajectorySubmissionProbeSnapshot(),
             CollectMemoryStats(),
             BuildInputView(),
             m_scrubberOwner.TrackPosition( RunReplayTrack::Solver ),
             SolverPresentTrackPosition() };
}
#endif


ReplayOverlay::ReplayOverlayStateView
ReplayRuntime::BuildOverlayStateView( bool editorModeEnabled,
                                      bool uiVisible,
                                      bool uiMinimized,
                                      RuntimeInteractionGestureKind gesture,
                                      std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                      const PhysicsBodyStore& bodyStore )
{
    int focusedCameraRow = -1;
    (void)m_authoring.BuildCauseTreeRows( m_visualPresentation.PathVisualizer(),
                                          m_predictionOwner.State(),
                                          m_predictionOwner.ActiveFrames(),
                                          CurrentSolverScrubSample(),
                                          presentation,
                                          bodyStore,
                                          m_visualPresentation.CameraView(),
                                          focusedCameraRow );
    if ( focusedCameraRow >= 0 )
    {
        m_visualPresentation.SetCameraFocusedRow( focusedCameraRow );
    }

    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const bool loadedPresentation = HasLoadedPresentation();
    const RunReplayTrack overlayTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const float overlayTrackPosition = m_scrubberOwner.TrackPosition( overlayTrack );
    const float solverPresentTrackPosition = SolverPresentTrackPosition();
    const bool futureSelected =
        !loadedPresentation && ReplayTrackPositionIsFuture( overlayTrackPosition, solverPresentTrackPosition );
    const ReplayPresentationSample* selectedPresentation =
        loadedPresentation ? LoadedPresentationSampleAtNormalized( overlayTrackPosition ) : nullptr;
    const ReplayPresentationSample* latestPresentation =
        loadedPresentation ? LoadedPresentationLatestSample() : nullptr;
    const ReplaySolverFrameSample* selectedSolver =
        ( loadedPresentation || futureSelected )
            ? nullptr
            : m_timeline.Solver().SampleAtNormalized(
                  ReplaySolverNormalizedFromTrack( overlayTrackPosition, solverPresentTrackPosition ) );
    const ReplaySolverFrameSample* latestSolver = loadedPresentation ? nullptr : m_timeline.Solver().LatestSample();

    return { scrubber,
             m_predictionOwner.PresentationView(),
             m_visualPresentation.PathVisualizer(),
             m_authoring.VelocityEdit(),
             m_authoring.CauseTree(),
             m_timeline.Solver().GetStats(),
             selectedPresentation,
             latestPresentation,
             selectedSolver,
             latestSolver,
             futureSelected ? CurrentPredictionScrubFrame() : nullptr,
             CurrentScrubSample(),
             CurrentSolverScrubSample(),
             solverPresentTrackPosition,
             loadedPresentation,
             m_predictionOwner.ActiveFrames().size() >= 2 || m_predictionOwner.State().BuildPrefixShouldBePresented(),
             ShouldRenderScrubber( editorModeEnabled, uiVisible, uiMinimized, gesture ) };
}


ReplayRenderSelectionView ReplayRuntime::BuildRenderSelectionView() const
{
    const ReplayPresentationSample* presentationSample = CurrentScrubSample();
    return { presentationSample,
             presentationSample ? nullptr : CurrentSolverScrubSample(),
             CurrentPredictionScrubFrame() };
}

ReplayRenderFrameView
ReplayRuntime::PrepareRenderFrame( Rendering::RenderInstanceStore& renderInstances,
                                   std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                   PhysicsEngine& physics,
                                   const SceneEntityStore& entities,
                                   RuntimeTools& runtimeTools,
                                   RunEditorTracer& tracer,
                                   int modelCount,
                                   bool editorModeEnabled,
                                   const RuntimeInteractionGesture& gesture,
                                   int sceneFrame,
                                   bool collisionVisualizer,
                                   bool debugTransparentBodyPass,
                                   const Math::Vector::Vector3& cameraTranslation,
                                   const Math::Vector::Vector3& cameraUp,
                                   uint64_t replayReserveGrowthEvents )
{
    const ReplayRenderSelectionView selection = BuildRenderSelectionView();
    const RunReplayPredictionFrame* predictionFrame = selection.predictionFrame;
    const ReplayPresentationSample* presentationSample = selection.presentationSample;
    const ReplaySolverFrameSample* solverSample = selection.solverSample;
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();

    {
        Allocation::RuntimeAllocationScope replayAllocationScope( Allocation::RuntimeAllocationPhase::Replay );
        if ( predictionFrame )
        {
            m_visualPresentation.ApplyPredictionFrameForRender( renderInstances,
                                                                PhysicsEngine::ReadBodies( physics ),
                                                                PhysicsEngine::ReadColliders( physics ),
                                                                *predictionFrame );
        }
        else if ( presentationSample )
        {
            m_visualPresentation.ApplyPresentationSampleForRender( renderInstances,
                                                                   PhysicsEngine::ReadBodies( physics ),
                                                                   PhysicsEngine::ReadColliders( physics ),
                                                                   *presentationSample );
        }
        else if ( solverSample )
        {
            m_visualPresentation.ApplySolverSampleForRender( renderInstances,
                                                             PhysicsEngine::ReadBodies( physics ),
                                                             PhysicsEngine::ReadColliders( physics ),
                                                             *solverSample );
            if ( !m_visualPresentation.HasLauncherVisualBackup() )
            {
                m_visualPresentation.StoreLauncherVisualBackupFrom( runtimeTools );
                runtimeTools.RestoreReplayLauncherVisualSample( solverSample->launcherVisual );
            }
        }
    }

    AppendOverlayTrace( physics, entities, tracer, ReplayOverlayBuildInput{ editorModeEnabled, gesture, sceneFrame } );
    (void)m_visualPresentation.BuildPredictionGhostDrawRequests( prediction,
                                                                 presentationRecords,
                                                                 PhysicsEngine::ReadBodies( physics ) );
    ReplayVisualPacket packet = tracer.BuildReplayVisualPacket( cameraTranslation, cameraUp );
    m_visualPresentation.PublishVisualPacket( packet,
                                              prediction,
                                              m_timeline.Solver().LatestSample(),
                                              replayReserveGrowthEvents );

    const ReplayInputView input = BuildInputView();
    bool focusFadeActive = false;
    if ( !input.predictionEnabled && !collisionVisualizer && !debugTransparentBodyPass )
    {
        Allocation::RuntimeAllocationScope replayAllocationScope( Allocation::RuntimeAllocationPhase::Replay );
        const std::span<const RunReplayPathTraceNode> focusNodes =
            prediction.enabled
                ? prediction.futureNodes
                : std::span<const RunReplayPathTraceNode>( m_visualPresentation.PathVisualizer().futureNodes );
        focusFadeActive =
            m_visualPresentation.BuildFocusModelMask( PhysicsEngine::ReadBodies( physics ), modelCount, focusNodes );
    }

    return { presentationSample,
             solverSample,
             ( presentationSample || solverSample ) ? nullptr : predictionFrame,
             &m_visualPresentation.PublishedVisualPacketView(),
             focusFadeActive ? &m_visualPresentation.FocusModelMaskView() : nullptr,
             input.predictionEnabled,
             input.liveAdvanceHeld,
             focusFadeActive };
}

void ReplayRuntime::CompleteRenderFrame( bool submissionRendered,
                                         int sceneFrame,
                                         uint64_t replayReserveGrowthEvents,
                                         RuntimeTools& runtimeTools )
{
    if ( submissionRendered )
    {
        m_visualPresentation.RecordTrajectorySubmissionFrame(
            m_visualPresentation.PublishedVisualPacketView().submission,
            sceneFrame,
            replayReserveGrowthEvents );
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

ReplayVisualPacket ReplayRuntime::BuildVisualProjectionForValidation(
    PhysicsEngine& physics,
    const SceneEntityStore& entities,
    std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
    const PhysicsBodyStore& bodyStore,
    RuntimeTools& runtimeTools,
    const Math::Vector::Vector3& cameraEye,
    const Math::Vector::Vector3& cameraUp,
    uint64_t replayReserveGrowthEvents )
{
    RunEditorTracer& tracer = runtimeTools.EditorTracer();
    AppendOverlayTrace(
        physics,
        entities,
        tracer,
        ReplayOverlayBuildInput{ runtimeTools.Editor().editorModeEnabled, RuntimeInteractionGesture{}, 0 } );
    const ReplayPredictionPresentationView prediction = m_predictionOwner.PresentationView();
    (void)m_visualPresentation.BuildPredictionGhostDrawRequests( prediction, presentationRecords, bodyStore );
    ReplayVisualPacket packet = tracer.BuildReplayVisualPacket( cameraEye, cameraUp );
    m_visualPresentation.PublishVisualPacket( packet,
                                              prediction,
                                              m_timeline.Solver().LatestSample(),
                                              replayReserveGrowthEvents );
    return m_visualPresentation.PublishedVisualPacketView();
}

void ReplayRuntime::ApplyAuthoringPredictionRequest()
{
    const ReplayAuthoringPredictionRequest request = m_authoring.TakePredictionRequest();
    if ( request.prepareVelocityMutationBaseline )
    {
        (void)m_predictionOwner.PrepareVelocityMutationBaseline();
    }
    if ( request.clearPredictionCache )
    {
        m_predictionOwner.ClearCache();
    }
    m_predictionOwner.ApplyAuthoringRequest( request.enablePrediction,
                                             request.refreshPrediction,
                                             ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS,
                                             ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS );
}

void ReplayRuntime::EnterOfflinePredictionVerification()
{
    // Invariant: this is a one-way terminal capability transition for a CLI
    // validation process. It does not clear the frozen prediction because the
    // caller immediately replaces it from RVPD, and no render frame follows.
    m_predictionOwner.EnterOfflineVerification();
    m_visualPresentation.ResetTrajectoryVisualStats();
}


bool ReplayRuntime::LoadPredictionArchiveForVerification( std::span<const uint8_t> bytes,
                                                          char* outReason,
                                                          std::size_t reasonSize )
{
    RunReplayPathVisualizerState archivePath;
    if ( !m_predictionOwner.LoadArchive( bytes, archivePath, outReason, reasonSize ) )
    {
        return false;
    }
    m_visualPresentation.ApplyArchivePathState( archivePath );
    return true;
}


bool ReplayRuntime::BuildPredictionArchiveForValidation( std::vector<uint8_t>& outBytes ) const
{
    return m_predictionOwner.BuildArchive( m_visualPresentation.PathVisualizer(), outBytes );
}


void ReplayRuntime::ResetPredictionPresentationVerification()
{
    m_visualPresentation.ResetTrajectoryVisualStats();
    m_predictionOwner.ResetVerificationMarkers();
}


void ReplayRuntime::ClearPathVisualizerState()
{
    m_visualPresentation.ClearPathState();
    m_authoring.ResetCauseTreeRows();
    m_predictionOwner.ClearCache();
    m_predictionOwner.MarkDirty();
}


ReplayPathColorMode ReplayRuntime::CyclePathColorMode() noexcept
{
    return m_visualPresentation.CyclePathColorMode();
}

ReplayPathPickResult
ReplayRuntime::ApplyPathPick( const ReplayPathPickInput& input,
                              const SceneEntityStore& entities,
                              const PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords )
{
    const ReplayPathPickResult result = m_visualPresentation.TryPickPathTarget( input,
                                                                                entities,
                                                                                bodyStore,
                                                                                colliderStore,
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


bool ReplayRuntime::RouteWorldPointer( const ReplayWorldPointerInput& input,
                                       const SceneEntityStore& entities,
                                       const Physics::PhysicsBodyStore& bodyStore,
                                       const Physics::ColliderStore& colliderStore,
                                       std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                       Environment::CameraCollection* cameras,
                                       Geometry::Terrain* terrain,
                                       RunCameraState& camera,
                                       RuntimeInteractionController& interaction,
                                       InputRouter& inputRouter )
{
    if ( !input.leftPressed || input.suppressWorldAction || input.editorMode || input.uiWantsNativeCursor ||
         ( !input.controlDown && input.launcherMode ) )
    {
        return false;
    }

    const ReplayPathPickResult pickResult =
        ApplyPathPick( input.pick, entities, bodyStore, colliderStore, presentation );
    if ( pickResult.exitInspectionCamera )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            cameras,
                                                            terrain,
                                                            camera,
                                                            input.restoreCameraMode,
                                                            input.attachedCameraFollow,
                                                            input.directorGrabbed,
                                                            interaction,
                                                            inputRouter );
    }
    return true;
}

bool ReplayRuntime::HasActiveInteractionState() const
{
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    const RunReplayCameraState camera = m_visualPresentation.CameraView();
    return camera.active || camera.focusKind != RunReplayCameraFocusKind::None || scrubber.historicalSamplePaused ||
           scrubber.liveAdvanceHeld || m_visualPresentation.PathVisualizer().hasTarget ||
           !m_visualPresentation.PathVisualizer().targets.empty() || m_predictionOwner.State().enabled ||
           m_predictionOwner.State().build.building || m_authoring.VelocityEdit().enabled ||
           m_authoring.CauseTree().selectedRow >= 0 || !m_authoring.CauseTree().rows.empty();
}


bool ReplayRuntime::ApplyInteractionExit( const ReplayInteractionExitInput& input,
                                          Environment::CameraCollection* cameras,
                                          Geometry::Terrain* terrain,
                                          RunCameraState& camera,
                                          RuntimeInteractionController& interaction,
                                          InputRouter& inputRouter )
{
    if ( !input.leavingReplayWorkspace || ( !HasActiveInteractionState() && !input.previousOwnerWasReplay ) )
    {
        return false;
    }

    if ( ClearInteractionForRuntimeTransition( interaction, inputRouter ) )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            cameras,
                                                            terrain,
                                                            camera,
                                                            input.normalizedRestoreMode,
                                                            input.attachedFollow,
                                                            input.directorGrabbed,
                                                            interaction,
                                                            inputRouter );
    }
    return true;
}


void ReplayRuntime::ApplyInputFocusLoss( Environment::CameraCollection* cameras,
                                         Geometry::Terrain* terrain,
                                         RunCameraState& camera,
                                         RunCameraMode normalizedRestoreMode,
                                         bool attachedFollow,
                                         bool directorGrabbed,
                                         RuntimeInteractionController& interaction,
                                         InputRouter& inputRouter )
{
    ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );
    if ( m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active ) )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            cameras,
                                                            terrain,
                                                            camera,
                                                            normalizedRestoreMode,
                                                            attachedFollow,
                                                            directorGrabbed,
                                                            interaction,
                                                            inputRouter );
    }
    m_authoring.ClearVelocityEditInputState();
}


void ReplayRuntime::ClearInteractionForSceneLoad( const ReplaySceneTimelineResetOwners& owners )
{
    const RuntimeInteractionTransition transition =
        owners.interaction.ResetForScene( InteractionExitReason::LoadScene );
    const bool previousOwnerWasReplay = transition.previousOwner == WorldInteractionOwner::ReplayScrub ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayVelocityEdit ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayPrediction ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayBranchTarget ||
                                        transition.previousOwner == WorldInteractionOwner::ReplayCauseTree;
    if ( !HasActiveInteractionState() && !previousOwnerWasReplay )
    {
        return;
    }
    if ( ClearInteractionForRuntimeTransition( owners.interaction, owners.inputRouter ) )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            owners.cameras,
                                                            owners.terrain,
                                                            owners.camera,
                                                            owners.normalizedRestoreMode,
                                                            owners.attachedFollow,
                                                            owners.directorGrabbed,
                                                            owners.interaction,
                                                            owners.inputRouter );
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
    m_authoring.ResetVelocityEdit();
    m_authoring.ResetCauseTreeRows();
    return exitInspectionCamera;
}

ReplayKeyboardVelocityEditResult
ReplayRuntime::ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input )
{
    ReplayKeyboardVelocityEditResult result =
        m_authoring.ApplyKeyboardVelocityEdit( input, m_scrubberOwner, m_visualPresentation );
    ApplyAuthoringPredictionRequest();
    return result;
}

float ReplayRuntime::SolverPresentTrackPosition() const
{
    return ReplayRuntimeScrubberPresentTrackPosition( m_timeline.Solver().GetStats(), m_predictionOwner.State() );
}

bool ReplayRuntime::ShouldRenderScrubber( bool editorModeEnabled,
                                          bool uiVisible,
                                          bool uiMinimized,
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

ReplayRecordingActivationResult ReplayRuntime::ConfigureRecording( bool enabled,
                                                                   int retentionSeconds,
                                                                   const char* hashLogPath,
                                                                   int runtimeBodyCapacity )
{
    ReplayRecordingActivationResult activation;
    m_visualPresentation.ReserveLauncherVisualCaptureBuffers();
    activation.configuration =
        m_timeline.ConfigureRecording( enabled, retentionSeconds, hashLogPath, runtimeBodyCapacity );
    if ( activation.configuration.presentationConfig.enabled )
    {
        // Runtime allocation policy: presentation buffers reserve during replay
        // setup, before steady gameplay begins.
        m_authoring.ReserveCauseTreeRows( REPLAY_CAUSE_TREE_ROW_CAPACITY );
        m_visualPresentation.ReserveRecordingBuffers();
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
    if ( SceneTimelineResetClearsBranch( input ) )
    {
        m_authoring.ResetBranch();
    }
    if ( m_scrubberOwner.LiveAdvanceHeld() )
    {
        m_scrubberOwner.SetLiveAdvanceHeld( false );
        m_visualPresentation.SetCameraPauseOwnership( false );
    }
    if ( m_scrubberOwner.ResetState( m_visualPresentation.CameraView().active ) )
    {
        result.exitInspectionCamera = true;
    }
    return result;
}


ReplaySceneTimelineResetResult ReplayRuntime::FinishSceneTimelineReset( const ReplaySceneTimelineResetInput& input )
{
    ReplaySceneTimelineResetResult result;
    m_timeline.ClearLoadedPresentation();
    ClearCameraFocusForRestore();
    result.exitInspectionCamera = true;
    ClearPathVisualizerState();
    m_authoring.ResetVelocityEdit();
    if ( !m_timeline.Presentation().IsEnabled() )
    {
        return result;
    }

    const char* sceneLabel = input.sceneLabel && input.sceneLabel[0] != '\0' ? input.sceneLabel : "generated";
    m_timeline.Reset( sceneLabel );
    SubmitEvent( ReplayEventCommandOperations::
                     BuildCommand( ReplayEventKind::TimelineStart, 0, false, 0, 0, 0, 0, 0, 0, sceneLabel ) );
    result.timelineStarted = true;
    // Why: mismatch diagnostics are scoped to the active replay timeline so a
    // noisy prior scene does not suppress the first useful report in this scene.
    m_timeline.ResetCaptureMismatchDiagnostics();

    if ( SceneTimelineRecordsGeneratedConfig( input ) )
    {
        const uint32_t flags = SceneTimelineGeneratedConfigFlags( input );

        SubmitEvent( ReplayEventCommandOperations::BuildGeneratedSceneConfig( flags,
                                                                              input.modelCount,
                                                                              input.solverBallCount,
                                                                              input.solverBoxCount,
                                                                              input.rngSeed,
                                                                              input.gameModelCapacity,
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
    m_visualPresentation.ApplyPastTrajectoryUpdate( update.targetId,
                                                    update.firstFrame,
                                                    update.builtThroughFrame,
                                                    update.totalFramesEvicted,
                                                    update.fullRebuildCount,
                                                    update.incrementalTrimCount,
                                                    update.valid,
                                                    update.targetModelRow,
                                                    update.targetModelRowRepaired );
}

void ReplayRuntime::AppendSolverTrajectorySampleToStore( const ReplaySolverFrameSample& sample )
{
    ReplayPastTrajectoryUpdate update;
    m_predictionOwner.AppendPastTrajectorySample( m_timeline.Solver().GetStats(),
                                                  m_visualPresentation.PastTrajectoryView(),
                                                  sample,
                                                  update );
    ApplyPastTrajectoryUpdate( update );
}

void ReplayRuntime::CaptureFrame( ReplayCaptureInput input, RuntimeTools& runtimeTools )
{
    // Invariant: presentation, solver, and event timelines share the same
    // branch and event cursor for this frame. Save/export code depends on that
    // alignment when it pairs visual frames with restore checkpoints.
    m_visualPresentation.PopulateLauncherVisualCapture( input, runtimeTools );
    input.branch = m_authoring.Branch();
    const ReplayTimelineCaptureResult result = m_timeline.CaptureFrame( input );
    if ( result.solverSample )
    {
        AppendSolverTrajectorySampleToStore( *result.solverSample );
    }
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
        return LoadedPresentationSampleAtNormalized( m_scrubberOwner.TrackPosition( RunReplayTrack::Presentation ) ) !=
               nullptr;
    }

    const float position = m_scrubberOwner.TrackPosition( scrubber.activeTrack );
    const float presentT = scrubber.activeTrack == RunReplayTrack::Solver ? SolverPresentTrackPosition() : 1.0f;
    if ( ReplayAtPresentTrackPosition( position, presentT ) )
    {
        return false;
    }

    if ( scrubber.activeTrack == RunReplayTrack::Presentation )
    {
        return m_timeline.Presentation().IsEnabled() &&
               m_timeline.Presentation().SampleAtNormalized( position ) != nullptr;
    }

    if ( ReplayTrackPositionIsFuture( position, presentT ) )
    {
        return CurrentPredictionScrubFrame() != nullptr;
    }

    return m_timeline.Solver().IsEnabled() &&
           m_timeline.Solver().SampleAtNormalized( ReplaySolverNormalizedFromTrack( position, presentT ) ) != nullptr;
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
        return scrubber.historicalSamplePaused ? LoadedPresentationSampleAtNormalized(
                                                     m_scrubberOwner.TrackPosition( RunReplayTrack::Presentation ) )
                                               : nullptr;
    }

    if ( !IsScrubPaused() )
    {
        return nullptr;
    }

    return m_timeline.Presentation().SampleAtNormalized(
        m_scrubberOwner.TrackPosition( RunReplayTrack::Presentation ) );
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
    // deliberately not checked here: Play can freeze rebuilds while keeping the
    // committed future scrubbable.
    std::size_t frameCount = 0;
    const std::vector<RunReplayPredictionFrame>& frames =
        ReplayRuntimeTimelinePredictionFrames( m_predictionOwner.State(), frameCount );
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
    const std::size_t frameIndex =
        (std::min)( frameCount - 1,
                    static_cast<std::size_t>( std::round( predictionT * static_cast<float>( frameCount - 1 ) ) ) );
    return &frames[frameIndex];
}
SkullbonezCore::Core::MainMemoryReplayStats ReplayRuntime::CollectMemoryStats() const
{
    SkullbonezCore::Core::MainMemoryReplayStats stats;
    const ReplayTimelineMemoryStats timelineMemory = m_timeline.CollectMemoryStats();
    const ReplayPredictionMemoryStats predictionMemory = m_predictionOwner.CollectMemoryStats();
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
    stats.presentationBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner );
    stats.solverBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::EventsOwner );
    stats.eventsBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
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
        Runtime::Allocation::RuntimeReserveOwnerStatsView ownerStats = {};
        growth.registered =
            Runtime::Allocation::RuntimeReserveAllocator::CopyOwnerStatsByName( policy.ownerName, ownerStats );
        if ( growth.registered )
        {
            growth.allocatorHighWaterBytes = ownerStats.highWaterBytes;
            growth.replayGrowths = ownerStats.replayGrowths;
            growth.failedGrowths = ownerStats.failedGrowths;
            growth.reportedHighWaterCapacity = ownerStats.highWaterCapacity;
            growth.lastGrowthFrame = ownerStats.lastGrowthFrame;
        }
    }

    stats.loadedReplayBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner );
    stats.predictionBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner );
    stats.predictionFrames = predictionMemory.frameCount;

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner,
        visualMemory.pathOwnerBytes + authoringMemory.ownerBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathFutureNodes,
        visualMemory.pathFutureNodeCapacityBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathTargets,
        visualMemory.pathTargetCapacityBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathCauseRows,
        authoringMemory.causeRowCapacityBytes );
    stats.pathAndCauseBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PathOwner,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests );
    stats.pathNodes = static_cast<std::size_t>( visualMemory.pathNodeCount ) + predictionMemory.futureNodeCount;
    stats.causeRows = authoringMemory.causeRowCount;

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests,
        visualMemory.ghostRequestCapacityBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderFocusMask,
        visualMemory.focusModelMaskCapacityBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderLauncherBackup,
        visualMemory.launcherVisualBytes );
    stats.renderScratchBytes = SkullbonezCore::Core::MainMemoryReplayCategoryRangeBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore );
    stats.ghostRequests = visualMemory.ghostRequestCount;
    stats.trajectory = visualMemory.trajectory;
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

bool ReplayRuntime::SavePresentationWithSolverHashes( const char* path,
                                                      ReplayV2SaveResult* result,
                                                      std::span<const ReplayVisualArchiveSample> visualPackets,
                                                      std::span<const uint8_t> visualPredictionState ) const
{
    std::vector<uint8_t> fallbackPredictionState;
    if ( !visualPackets.empty() && visualPredictionState.empty() &&
         !m_predictionOwner.BuildArchive( m_visualPresentation.PathVisualizer(), fallbackPredictionState ) )
    {
        return false;
    }
    const std::span<const uint8_t> predictionState =
        !visualPredictionState.empty() ? visualPredictionState : std::span<const uint8_t>( fallbackPredictionState );
    return ReplayV2Artifact::SavePresentationWithSolverHashes( m_timeline.Presentation(),
                                                               m_timeline.Solver(),
                                                               m_timeline.Events(),
                                                               visualPackets,
                                                               predictionState,
                                                               path,
                                                               result );
}

void ReplayRuntime::UpdatePrediction( PhysicsEngine& physics,
                                      const SceneEntityStore& entities,
                                      const SkullbonezCore::Core::EngineConfig& config,
                                      const Physics::PhysicsWorldForces& worldForces,
                                      Threading::WorkerPool& workerPool,
                                      bool scenePhysicsEnabled,
                                      double simulationTimeSinceLastStart,
                                      double simulationTotalTime )
{
    // Concept: the composition root samples owner values, then prediction
    // advances without a ReplayRuntime reach-back. Its value-only result is
    // applied after the worker/publication transition returns.
    const RunReplayPathVisualizerState& path = m_visualPresentation.PathVisualizer();
    const ReplayScrubberView scrubber = m_scrubberOwner.View();
    ReplayPredictionUpdateResult result;
    m_predictionOwner.UpdateFrame( physics,
                                   entities,
                                   config,
                                   worldForces,
                                   workerPool,
                                   m_timeline.Solver().LatestSample(),
                                   path.targetId,
                                   path.targetModelRow,
                                   path.hasTarget,
                                   scrubber.liveAdvanceHeld,
                                   scrubber.historicalSamplePaused,
                                   m_scrubberOwner.TrackPosition( RunReplayTrack::Solver ),
                                   SolverPresentTrackPosition(),
                                   scenePhysicsEnabled,
                                   simulationTimeSinceLastStart,
                                   simulationTotalTime,
                                   REPLAY_PREDICTION_MAX_WORK_MILLISECONDS,
                                   result );
    ApplyPredictionUpdateResult( result );
    PreparePredictionPresentation( physics, entities );
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
            m_visualPresentation.RecordTrajectoryBudgetExpiry(
                static_cast<SkullbonezCore::Core::MainMemoryReplayBudgetPass>( passIndex ) );
        }
    }
    for ( std::size_t causeIndex = 0; causeIndex < result.rebuildCauses.size(); ++causeIndex )
    {
        for ( uint32_t count = 0; count < result.rebuildCauses[causeIndex]; ++count )
        {
            m_visualPresentation.RecordTrajectoryRebuildCause(
                static_cast<SkullbonezCore::Core::MainMemoryReplayRebuildCause>( causeIndex ) );
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
    m_predictionOwner.PreparePresentation( entities,
                                           colliderStore,
                                           path.targetId,
                                           path.targetModelRow,
                                           path.hasTarget,
                                           REPLAY_PREDICTION_MAX_WORK_MILLISECONDS,
                                           result );
    ApplyPredictionUpdateResult( result );
    if ( m_predictionOwner.PresentationView().generationPermitted )
    {
        ApplyPastTrajectoryUpdate(
            m_predictionOwner.RefreshPastTrajectoryStore( m_timeline.Solver(),
                                                          m_visualPresentation.PastTrajectoryView() ) );
    }
    m_visualPresentation.PreparePathDrawing( PhysicsEngine::ReadBodies( physics ) );
}


} // namespace SkullbonezCore::Runtime
