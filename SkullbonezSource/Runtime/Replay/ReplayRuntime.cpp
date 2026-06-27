/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
Purpose:
  Provides the replay subsystem ownership boundary for legacy Run replay callers.

Mental model:
  ReplayRuntime is mostly an accessor and coordination shell. It keeps recorder,
  loaded-artifact, tool, branch, and camera state in one owned object while Run
  still performs most replay behavior.

Glossary:
  Branch: Child replay timeline created from a restored source frame.
  Cause tree row: UI row derived from retained solver contacts or prediction
    future nodes.
  Hash log: Deterministic text stream that lets saved replay output be compared.
  Loaded presentation: Replay artifact data loaded from disk for scrub preview.
  Ragdoll part: One body inside a multi-body SimpleRagdoll collection.

Invariants:
  - Accessors return owned state; callers must not store references past
    ReplayRuntime lifetime.
  - Solver hash-log paths derive from the presentation path so paired artifacts
    stay beside each other.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayExporter.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
*/
#include "ReplayRuntime.h"
#include "ReplayExporter.h"
#include "ReplayV2Artifact.h"
#include "../../GameObjects/GameModel.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../Core/Profiler.h"
#include "../../Physics/CollisionShape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Basics
{
namespace
{
constexpr float REPLAY_RUNTIME_SCRUBBER_LIVE_THRESHOLD = 0.995f;
constexpr float REPLAY_RUNTIME_SCRUBBER_PRESENT_EPSILON = 0.0035f;

using GameObjects::GameModel;
using Math::CollisionDetection::GetShapeBoundingRadius;
using Math::Vector::Vector3;
using Math::Vector::VectorMagSquared;
using Physics::PhysicsPipelineRecord;
using Physics::PhysicsPipelineStageName;

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

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
    if ( prediction.building && prediction.buildFrames.size() >= 2 &&
         ( prediction.frames.empty() || prediction.buildFrames.size() >= prediction.frames.size() ) )
    {
        return prediction.buildFrames;
    }
    return prediction.frames;
}

float ReplayRuntimePredictionAvailableFutureSeconds( const RunReplayPredictionState& prediction )
{
    const std::vector<RunReplayPredictionFrame>& frames = ReplayRuntimeActivePredictionFrames( prediction );
    if ( !prediction.enabled || frames.size() < 2 )
    {
        return 0.0f;
    }
    return static_cast<float>( frames.back().frameIndex ) * PHYSICS_FIXED_DT;
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

uint64_t LauncherVisualMemoryBytes( const ReplayLauncherVisualSample& visual )
{
    return VectorCapacityBytes( visual.rayLines ) + VectorCapacityBytes( visual.laserShots );
}

uint64_t SolverWorldSnapshotMemoryBytes( const ReplaySolverWorldSnapshot& snapshot )
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( snapshot.timeRemaining );
    bytes += VectorCapacityBytes( snapshot.sleepSupportedThisFrame );
    bytes += VectorCapacityBytes( snapshot.sleepInhibitedThisFrame );
    bytes += VectorCapacityBytes( snapshot.sleepState );
    bytes += VectorCapacityBytes( snapshot.sleepCounter );
    bytes += VectorCapacityBytes( snapshot.underwaterSleepLocked );
    bytes += VectorCapacityBytes( snapshot.tornadoCaptureSeconds );
    bytes += VectorCapacityBytes( snapshot.tornadoEjectCooldownSeconds );
    bytes += VectorCapacityBytes( snapshot.collisionVisualContacts );
    bytes += VectorCapacityBytes( snapshot.sleepIslandVisualId );
    bytes += VectorCapacityBytes( snapshot.sleepIslandAssignedVisualId );
    bytes += VectorCapacityBytes( snapshot.sleepSupportEdges );
    bytes += VectorCapacityBytes( snapshot.sleepIslandParent );
    bytes += VectorCapacityBytes( snapshot.sleepIslandRank );
    bytes += VectorCapacityBytes( snapshot.sleepIslandHasAwake );
    bytes += VectorCapacityBytes( snapshot.sleepIslandHasSupportAnchor );
    bytes += VectorCapacityBytes( snapshot.sleepIslandEligible );
    bytes += VectorCapacityBytes( snapshot.sleepIslandCanSleep );
    bytes += VectorCapacityBytes( snapshot.persistentContacts );
    bytes += VectorCapacityBytes( snapshot.persistentContactCache );
    bytes += VectorCapacityBytes( snapshot.persistentContactCounts );
    bytes += VectorCapacityBytes( snapshot.persistentRestingContactCounts );
    bytes += VectorCapacityBytes( snapshot.debugContacts );
    bytes += VectorCapacityBytes( snapshot.pipelineTrace );
    bytes += VectorCapacityBytes( snapshot.collisionCellKeys );
    return bytes;
}

uint64_t PresentationSampleMemoryBytes( const ReplayPresentationSample& sample )
{
    return VectorCapacityBytes( sample.bodies );
}

uint64_t PredictionFrameMemoryBytes( const RunReplayPredictionFrame& frame )
{
    return VectorCapacityBytes( frame.bodies ) + VectorCapacityBytes( frame.debugContacts );
}

bool ReplayRuntimeModelIsRagdollPart( const GameObjects::GameModel& model )
{
    // SimpleRagdoll children share replay visuals with their collection root.
    // This helper keeps that policy local to replay loading/restoration paths.
    return model.GetRuntimeCollectionKind() == SkullbonezCore::GameObjects::GameModelCollectionKind::SimpleRagdoll;
}


const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   ReplayBodyId id )
{
    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

float ReplayRuntimeModelRadius( const GameModel& model )
{
    return (std::max)( GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}

ReplayBodyId ReplayBodyIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    ReplayBodyId id;
    if ( modelIndex < 0 )
    {
        return id;
    }

    if ( modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const ReplaySolverBodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return body.id;
        }
    }
    return id;
}

Vector3 ReplayNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    value /= sqrtf( magSq );
    return value;
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    if ( modelIndex >= 0 && modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const ReplaySolverBodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

bool ReplayContactHasModelIndex( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    if ( contact.bodyA == modelIndex )
    {
        return contact.bodyB;
    }
    if ( contact.bodyB == modelIndex )
    {
        return contact.bodyA;
    }
    return -1;
}

Vector3 ReplayContactPoint( const ReplaySolverFrameSample& sample, const ReplaySolverPersistentContactSample& contact )
{
    if ( const ReplaySolverBodySample* bodyA = FindReplayBodyByModelIndex( sample, contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }
    if ( const ReplaySolverBodySample* bodyB = FindReplayBodyByModelIndex( sample, contact.bodyB ) )
    {
        return bodyB->position + contact.rB;
    }
    return SkullbonezCore::Math::Vector::ZERO_VECTOR;
}

Vector3 ReplayContactNormalForModel( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    Vector3 normal = contact.normal;
    if ( contact.isTerrain && VectorMagSquared( contact.terrainNormal ) > TOLERANCE * TOLERANCE )
    {
        normal = contact.terrainNormal;
    }
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        normal = normal * -1.0f;
    }
    return ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) );
}

Vector3 ReplayContactImpulseForModel( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    const Vector3 rowImpulse =
        contact.normal * contact.accN + contact.tangent1 * contact.accT1 + contact.tangent2 * contact.accT2;
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        return rowImpulse;
    }
    return rowImpulse * -1.0f;
}

int ReplayFindPipelineIndexForContact( const ReplaySolverWorldSnapshot& snapshot,
                                       const ReplaySolverPersistentContactSample& contact )
{
    for ( int i = 0; i < static_cast<int>( snapshot.pipelineTrace.size() ); ++i )
    {
        const PhysicsPipelineRecord& record = snapshot.pipelineTrace[static_cast<std::size_t>( i )];
        if ( record.featureId == contact.featureId &&
             ( ( record.bodyA == contact.bodyA && record.bodyB == contact.bodyB ) ||
               ( record.bodyA == contact.bodyB && record.bodyB == contact.bodyA ) ) )
        {
            return i;
        }
    }
    return -1;
}

std::string SolverReplayHashLogPath( const std::string& presentationPath )
{
    // Keep solver hash logs beside presentation logs so capture artifacts can
    // be copied or deleted as a pair.
    if ( presentationPath.empty() )
    {
        return {};
    }

    const std::size_t slash = presentationPath.find_last_of( "/\\" );
    const std::size_t dot = presentationPath.find_last_of( '.' );
    if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) )
    {
        return presentationPath.substr( 0, dot ) + ".solver" + presentationPath.substr( dot );
    }
    return presentationPath + ".solver";
}
} // namespace

ReplayRecorder& ReplayRuntime::Presentation()
{
    return m_presentation;
}

const ReplayRecorder& ReplayRuntime::Presentation() const
{
    return m_presentation;
}

ReplaySolverRecorder& ReplayRuntime::Solver()
{
    return m_solver;
}

const ReplaySolverRecorder& ReplayRuntime::Solver() const
{
    return m_solver;
}

ReplayEventRecorder& ReplayRuntime::Events()
{
    return m_events;
}

const ReplayEventRecorder& ReplayRuntime::Events() const
{
    return m_events;
}

ReplayBranchInfo& ReplayRuntime::Branch()
{
    return m_branch;
}

const ReplayBranchInfo& ReplayRuntime::Branch() const
{
    return m_branch;
}

RunLoadedReplayPresentationState& ReplayRuntime::LoadedPresentation()
{
    return m_loadedPresentation;
}

const RunLoadedReplayPresentationState& ReplayRuntime::LoadedPresentation() const
{
    return m_loadedPresentation;
}

RunReplayScrubberState& ReplayRuntime::Scrubber()
{
    return m_scrubber;
}

const RunReplayScrubberState& ReplayRuntime::Scrubber() const
{
    return m_scrubber;
}

RunReplayCameraState& ReplayRuntime::Camera()
{
    return m_camera;
}

const RunReplayCameraState& ReplayRuntime::Camera() const
{
    return m_camera;
}

RunReplayPathVisualizerState& ReplayRuntime::PathVisualizer()
{
    return m_pathVisualizer;
}

const RunReplayPathVisualizerState& ReplayRuntime::PathVisualizer() const
{
    return m_pathVisualizer;
}

RunReplayPredictionState& ReplayRuntime::Prediction()
{
    return m_prediction;
}

const RunReplayPredictionState& ReplayRuntime::Prediction() const
{
    return m_prediction;
}

const std::vector<RunReplayPredictionFrame>& ReplayRuntime::ActivePredictionFrames() const
{
    return ReplayRuntimeActivePredictionFrames( m_prediction );
}

void ReplayRuntime::ClearPredictionFutureNodeCache()
{
    m_prediction.futureNodes.clear();
    m_prediction.futureNodesBuiltFrameCount = 0;
    m_prediction.futureNodesBuiltContactIndex = 0;
    m_prediction.futureNodesBuiltTargetId = ReplayBodyId{};
    m_prediction.futureNodesBuiltRagdollVisuals = m_prediction.ragdollVisualsEnabled;
    m_prediction.futureNodesBuiltFromBuildFrames = false;
    m_prediction.futureNodesCacheValid = false;
}

void ReplayRuntime::CancelPredictionJob( bool clearSamples )
{
    m_prediction.building = false;
    m_prediction.complete = false;
    m_prediction.targetModelIndex = -1;
    m_prediction.nextTick = 1;
    m_prediction.targetTickCount = 0;
    m_prediction.predictionBodies.clear();
    m_prediction.liveRestoreBodies.clear();
    m_prediction.predictionWorld = ReplaySolverWorldSnapshot();
    m_prediction.liveRestoreWorld = ReplaySolverWorldSnapshot();
    m_prediction.buildFrames.clear();
    if ( clearSamples )
    {
        m_prediction.frames.clear();
        ClearPredictionFutureNodeCache();
    }
}

void ReplayRuntime::ClearPredictionCache()
{
    CancelPredictionJob( true );
    m_prediction.targetId = ReplayBodyId{};
    m_prediction.sourceFrameIndex = 0;
    m_prediction.sourceSolverHash = 0;
    m_prediction.sourceSimulationSeconds = 0.0;
    m_prediction.lastBuildTime = 0.0;
}

void ReplayRuntime::MarkPredictionDirty()
{
    CancelPredictionJob( false );
    m_prediction.dirty = true;
}

void ReplayRuntime::ClearPathVisualizerState()
{
    m_pathVisualizer.hasTarget = false;
    m_pathVisualizer.targetId = ReplayBodyId{};
    m_pathVisualizer.targetModelIndex = -1;
    m_pathVisualizer.targetName[0] = '\0';
    m_pathVisualizer.futureNodes.clear();
    m_pathVisualizer.targets.clear();
    m_causeTree.rows.clear();
    m_causeTree.hoveredRow = -1;
    m_causeTree.selectedRow = -1;
    m_causeTree.scrollY = 0.0f;
    ClearPredictionCache();
    MarkPredictionDirty();
}

RunReplayCauseTreeState& ReplayRuntime::CauseTree()
{
    return m_causeTree;
}

const RunReplayCauseTreeState& ReplayRuntime::CauseTree() const
{
    return m_causeTree;
}

RunReplayVelocityEditState& ReplayRuntime::VelocityEdit()
{
    return m_velocityEdit;
}

const RunReplayVelocityEditState& ReplayRuntime::VelocityEdit() const
{
    return m_velocityEdit;
}

void ReplayRuntime::SetVelocityEditAltKeyDown( bool isDown )
{
    m_velocityEdit.keyboardAltWasDown = isDown;
}

float ReplayRuntime::TrackPosition( RunReplayTrack track ) const
{
    return track == RunReplayTrack::Solver ? m_scrubber.solverPosition : m_scrubber.presentationPosition;
}

void ReplayRuntime::SetTrackPosition( RunReplayTrack track, float position )
{
    const float clamped = std::clamp( position, 0.0f, 1.0f );
    if ( track == RunReplayTrack::Solver )
    {
        m_scrubber.solverPosition = clamped;
    }
    else
    {
        m_scrubber.presentationPosition = clamped;
    }

    if ( m_scrubber.activeTrack == track )
    {
        m_scrubber.position = clamped;
    }
}

void ReplayRuntime::SyncActiveTrackPosition()
{
    m_scrubber.position = TrackPosition( m_scrubber.activeTrack );
}

void ReplayRuntime::SetAllTrackPositions( float position )
{
    const float clamped = std::clamp( position, 0.0f, 1.0f );
    m_scrubber.presentationPosition = clamped;
    m_scrubber.solverPosition = clamped;
    m_scrubber.position = clamped;
}

float ReplayRuntime::SolverPresentTrackPosition() const
{
    return ReplayRuntimeScrubberPresentTrackPosition( m_solver.GetStats(), m_prediction );
}

bool ReplayRuntime::TimelineHasFuture( float presentT )
{
    return presentT < REPLAY_RUNTIME_SCRUBBER_LIVE_THRESHOLD;
}

bool ReplayRuntime::AtPresentTrackPosition( float position, float presentT )
{
    if ( !TimelineHasFuture( presentT ) )
    {
        return position >= REPLAY_RUNTIME_SCRUBBER_LIVE_THRESHOLD;
    }
    return std::fabs( position - presentT ) <= REPLAY_RUNTIME_SCRUBBER_PRESENT_EPSILON;
}

bool ReplayRuntime::TrackPositionIsFuture( float position, float presentT )
{
    return TimelineHasFuture( presentT ) && position > presentT + REPLAY_RUNTIME_SCRUBBER_PRESENT_EPSILON;
}

float ReplayRuntime::SolverNormalizedFromTrack( float position, float presentT )
{
    if ( !TimelineHasFuture( presentT ) )
    {
        return std::clamp( position, 0.0f, 1.0f );
    }
    return std::clamp( position / (std::max)( presentT, 0.0001f ), 0.0f, 1.0f );
}

float ReplayRuntime::PredictionNormalizedFromTrack( float position, float presentT )
{
    if ( !TimelineHasFuture( presentT ) )
    {
        return 0.0f;
    }
    return std::clamp( ( position - presentT ) / ( 1.0f - presentT ), 0.0f, 1.0f );
}

bool ReplayRuntime::ShouldRenderScrubber( bool editorModeEnabled, bool uiVisible, bool uiMinimized ) const
{
    if ( editorModeEnabled || !uiVisible || !uiMinimized )
    {
        return false;
    }

    const bool loadedPresentation = HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = m_solver.GetStats();
    const bool solverReplayAvailable = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
    return ( loadedPresentation || solverReplayAvailable ) &&
           ( m_scrubber.visible || m_scrubber.dragging || m_scrubber.historicalSamplePaused ||
             m_scrubber.liveAdvanceHeld );
}

ReplayRuntime::RecordingConfigResult
ReplayRuntime::ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath )
{
    ReplayRecorderConfig replayConfig;
    replayConfig.enabled = enabled || ( hashLogPath && hashLogPath[0] != '\0' );
    replayConfig.retentionSeconds = (std::max)( 1, retentionSeconds );
    replayConfig.checkpointIntervalFrames = 30;
    if ( hashLogPath && hashLogPath[0] != '\0' )
    {
        replayConfig.hashLogPath = hashLogPath;
    }

    ReplayRecorderConfig solverReplayConfig = replayConfig;
    solverReplayConfig.checkpointIntervalFrames = 60;
    solverReplayConfig.hashLogPath = SolverReplayHashLogPath( replayConfig.hashLogPath );

    m_presentation.Configure( replayConfig );
    m_solver.Configure( solverReplayConfig );
    m_events.Configure( replayConfig );

    RecordingConfigResult result;
    result.presentationConfig = replayConfig;
    result.solverConfig = solverReplayConfig;
    result.presentationStats = m_presentation.GetStats();
    result.solverStats = m_solver.GetStats();
    result.eventStats = m_events.GetStats();
    return result;
}

void ReplayRuntime::FlushHashLogs()
{
    m_presentation.FlushHashLog();
    m_solver.FlushHashLog();
}

void ReplayRuntime::ResetBranch()
{
    m_branch = ReplayBranchInfo();
}

void ReplayRuntime::ResetTimeline( const char* sceneLabel )
{
    m_presentation.ResetTimeline( sceneLabel );
    m_solver.ResetTimeline( sceneLabel );
    m_events.ResetTimeline( sceneLabel );
}

bool ReplayRuntime::IsPresentationEnabled() const
{
    return m_presentation.IsEnabled();
}

bool ReplayRuntime::IsCaptureEnabled() const
{
    return m_presentation.IsEnabled() || m_solver.IsEnabled();
}

ReplayRecorderStats ReplayRuntime::PresentationStats() const
{
    return m_presentation.GetStats();
}

ReplayRecorderStats ReplayRuntime::SolverStats() const
{
    return m_solver.GetStats();
}

ReplayEventRecorderStats ReplayRuntime::EventStats() const
{
    return m_events.GetStats();
}

ReplayFrameIndex ReplayRuntime::NextEventFrameIndex() const
{
    const ReplayRecorderStats solverStats = m_solver.GetStats();
    if ( solverStats.enabled )
    {
        return solverStats.nextFrameIndex;
    }

    const ReplayRecorderStats presentationStats = m_presentation.GetStats();
    return presentationStats.nextFrameIndex;
}

void ReplayRuntime::CaptureFrame( ReplayCaptureInput input )
{
    input.branch = m_branch;
    input.eventCursor = m_events.GetStats().nextSequence;
    m_presentation.CaptureFrame( input );
    m_solver.CaptureFrame( input );
}

bool ReplayRuntime::ApplyPresentationSampleForRender( GameObjects::GameModelCollection& collection,
                                                      const ReplayPresentationSample& sample )
{
    std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();
    m_renderPoseBackups.clear();
    m_renderPoseBackups.reserve( models.size() );
    std::vector<uint8_t> bodyMatched( models.size(), 0 );

    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        if ( model.GetReplayBodyId() != body.id.value )
        {
            continue;
        }

        RenderPoseBackup backup;
        backup.modelIndex = body.modelIndex;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        m_renderPoseBackups.push_back( backup );
        bodyMatched[static_cast<std::size_t>( body.modelIndex )] = 1;

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        model.SetPosition( body.position );
        model.SetOrientation( orientation );
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        if ( bodyMatched[i] )
        {
            continue;
        }

        RenderPoseBackup backup;
        backup.modelIndex = static_cast<int>( i );
        backup.position = models[i].GetPosition();
        backup.orientation = models[i].GetOrientation();
        m_renderPoseBackups.push_back( backup );
        models[i].SetPosition( hiddenReplayPosition );
    }

    if ( !m_renderPoseBackups.empty() )
    {
        collection.InvalidatePhysicsStreams();
    }
    return !m_renderPoseBackups.empty();
}

bool ReplayRuntime::ApplySolverSampleForRender( GameObjects::GameModelCollection& collection,
                                                const ReplaySolverFrameSample& sample )
{
    std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();
    m_renderPoseBackups.clear();
    m_renderPoseBackups.reserve( models.size() );
    std::vector<uint8_t> bodyMatched( models.size(), 0 );

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        if ( model.GetReplayBodyId() != body.id.value )
        {
            continue;
        }

        RenderPoseBackup backup;
        backup.modelIndex = body.modelIndex;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        m_renderPoseBackups.push_back( backup );
        bodyMatched[static_cast<std::size_t>( body.modelIndex )] = 1;

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        model.SetPosition( body.position );
        model.SetOrientation( orientation );
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        if ( bodyMatched[i] )
        {
            continue;
        }

        RenderPoseBackup backup;
        backup.modelIndex = static_cast<int>( i );
        backup.position = models[i].GetPosition();
        backup.orientation = models[i].GetOrientation();
        m_renderPoseBackups.push_back( backup );
        models[i].SetPosition( hiddenReplayPosition );
    }

    if ( !m_renderPoseBackups.empty() )
    {
        collection.InvalidatePhysicsStreams();
    }
    return !m_renderPoseBackups.empty();
}

bool ReplayRuntime::ApplyPredictionFrameForRender( GameObjects::GameModelCollection& collection,
                                                   const RunReplayPredictionFrame& frame )
{
    std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();
    m_renderPoseBackups.clear();
    m_renderPoseBackups.reserve( models.size() );
    std::vector<uint8_t> bodyMatched( models.size(), 0 );

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        if ( model.GetReplayBodyId() != body.id.value )
        {
            continue;
        }

        RenderPoseBackup backup;
        backup.modelIndex = body.modelIndex;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        m_renderPoseBackups.push_back( backup );
        bodyMatched[static_cast<std::size_t>( body.modelIndex )] = 1;

        Math::Orientation::Quaternion orientation = body.orientation;
        orientation.Normalise();
        model.SetPosition( body.position );
        model.SetOrientation( orientation );
    }

    const Math::Vector::Vector3 hiddenReplayPosition( 0.0f, -100000.0f, 0.0f );
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        if ( bodyMatched[i] )
        {
            continue;
        }

        RenderPoseBackup backup;
        backup.modelIndex = static_cast<int>( i );
        backup.position = models[i].GetPosition();
        backup.orientation = models[i].GetOrientation();
        m_renderPoseBackups.push_back( backup );
        models[i].SetPosition( hiddenReplayPosition );
    }

    if ( !m_renderPoseBackups.empty() )
    {
        collection.InvalidatePhysicsStreams();
    }
    return !m_renderPoseBackups.empty();
}

void ReplayRuntime::RestoreRenderPose( GameObjects::GameModelCollection& collection )
{
    if ( m_renderPoseBackups.empty() )
    {
        return;
    }

    std::vector<GameObjects::GameModel>& models = collection.PhysicsModels();
    for ( const RenderPoseBackup& backup : m_renderPoseBackups )
    {
        if ( backup.modelIndex < 0 || backup.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( backup.modelIndex )];
        model.SetPosition( backup.position );
        model.SetOrientation( backup.orientation );
    }
    m_renderPoseBackups.clear();
    collection.InvalidatePhysicsStreams();
}


bool ReplayRuntime::HasLoadedPresentation() const
{
    return m_loadedPresentation.enabled && m_loadedPresentation.samples.size() >= 2;
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationSampleAtNormalized( float normalized ) const
{
    if ( !HasLoadedPresentation() )
    {
        return nullptr;
    }

    return ReplayRuntimeLoadedPresentationSampleAtNormalized( m_loadedPresentation.samples, normalized );
}


const ReplayPresentationSample* ReplayRuntime::LoadedPresentationLatestSample() const
{
    return HasLoadedPresentation() ? &m_loadedPresentation.samples.back() : nullptr;
}


bool ReplayRuntime::IsScrubPaused() const
{
    if ( !m_scrubber.historicalSamplePaused )
    {
        return false;
    }

    if ( m_scrubber.activeTrack == RunReplayTrack::Presentation && HasLoadedPresentation() )
    {
        return LoadedPresentationSampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) ) != nullptr;
    }

    const float position = TrackPosition( m_scrubber.activeTrack );
    const float presentT = m_scrubber.activeTrack == RunReplayTrack::Solver ? SolverPresentTrackPosition() : 1.0f;
    if ( AtPresentTrackPosition( position, presentT ) )
    {
        return false;
    }

    if ( m_scrubber.activeTrack == RunReplayTrack::Presentation )
    {
        return m_presentation.IsEnabled() && m_presentation.SampleAtNormalized( position ) != nullptr;
    }

    if ( TrackPositionIsFuture( position, presentT ) )
    {
        return CurrentPredictionScrubFrame() != nullptr;
    }

    return m_solver.IsEnabled() &&
           m_solver.SampleAtNormalized( SolverNormalizedFromTrack( position, presentT ) ) != nullptr;
}


const ReplayPresentationSample* ReplayRuntime::CurrentScrubSample() const
{
    if ( m_scrubber.activeTrack != RunReplayTrack::Presentation )
    {
        return nullptr;
    }

    if ( HasLoadedPresentation() )
    {
        return m_scrubber.historicalSamplePaused
                   ? LoadedPresentationSampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) )
                   : nullptr;
    }

    if ( !IsScrubPaused() )
    {
        return nullptr;
    }

    return m_presentation.SampleAtNormalized( TrackPosition( RunReplayTrack::Presentation ) );
}


const ReplaySolverFrameSample* ReplayRuntime::CurrentSolverScrubSample() const
{
    if ( m_scrubber.activeTrack != RunReplayTrack::Solver || !IsScrubPaused() )
    {
        return nullptr;
    }

    const float position = TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();
    if ( TrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    return m_solver.SampleAtNormalized( SolverNormalizedFromTrack( position, presentT ) );
}


const RunReplayPredictionFrame* ReplayRuntime::CurrentPredictionScrubFrame() const
{
    if ( m_scrubber.activeTrack != RunReplayTrack::Solver || !m_scrubber.historicalSamplePaused ||
         !m_prediction.enabled || ActivePredictionFrames().size() < 2 )
    {
        return nullptr;
    }

    const float position = TrackPosition( RunReplayTrack::Solver );
    const float presentT = SolverPresentTrackPosition();
    if ( !TrackPositionIsFuture( position, presentT ) )
    {
        return nullptr;
    }

    const std::vector<RunReplayPredictionFrame>& frames = ActivePredictionFrames();
    const float predictionT = PredictionNormalizedFromTrack( position, presentT );
    const std::size_t frameCount = frames.size();
    const std::size_t frameIndex =
        (std::min)( frameCount - 1,
                    static_cast<std::size_t>( std::round( predictionT * static_cast<float>( frameCount - 1 ) ) ) );
    return &frames[frameIndex];
}


bool ReplayRuntime::ResolveCauseTreeBodyPosition( ReplayBodyId id,
                                                  const std::vector<GameObjects::GameModel>& models,
                                                  Vector3& outPosition,
                                                  float* outRadius ) const
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( outRadius )
    {
        *outRadius = 1.0f;
    }

    const std::vector<RunReplayPredictionFrame>& activePredictionFrames = ActivePredictionFrames();
    if ( m_prediction.enabled && !activePredictionFrames.empty() &&
         m_prediction.targetId.value == m_pathVisualizer.targetId.value )
    {
        if ( const RunReplayPredictionBodySample* body =
                 FindReplayPredictionBodyById( activePredictionFrames.front(), id ) )
        {
            outPosition = body->position;
            if ( outRadius && body->modelIndex >= 0 && body->modelIndex < static_cast<int>( models.size() ) )
            {
                *outRadius = ReplayRuntimeModelRadius( models[static_cast<std::size_t>( body->modelIndex )] );
            }
            return true;
        }
    }

    if ( const ReplaySolverFrameSample* sample = CurrentSolverScrubSample() )
    {
        if ( const ReplaySolverBodySample* body = FindReplayBodyById( *sample, id ) )
        {
            outPosition = body->position;
            if ( outRadius && body->modelIndex >= 0 && body->modelIndex < static_cast<int>( models.size() ) )
            {
                *outRadius = ReplayRuntimeModelRadius( models[static_cast<std::size_t>( body->modelIndex )] );
            }
            return true;
        }
    }

    for ( const GameModel& model : models )
    {
        if ( model.GetReplayBodyId() == id.value )
        {
            outPosition = model.GetPosition();
            if ( outRadius )
            {
                *outRadius = ReplayRuntimeModelRadius( model );
            }
            return true;
        }
    }
    return false;
}


bool ReplayRuntime::BuildCauseTreeRows( const std::vector<GameObjects::GameModel>& models )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/BuildRows" );
    m_causeTree.rows.clear();

    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 )
    {
        return false;
    }

    const bool usePrediction = m_prediction.enabled && ActivePredictionFrames().size() >= 2 &&
                               m_prediction.targetId.value == m_pathVisualizer.targetId.value;
    const std::vector<RunReplayPathTraceNode>& nodes =
        usePrediction ? m_prediction.futureNodes : m_pathVisualizer.futureNodes;
    const ReplaySolverFrameSample* solverSample = CurrentSolverScrubSample();
    const std::size_t solverContactCount =
        solverSample ? solverSample->worldSnapshot.persistentContacts.size() : static_cast<std::size_t>( 0 );
    const std::size_t estimatedRows = 1 + nodes.size() + solverContactCount * 3;
    if ( m_causeTree.rows.capacity() < estimatedRows )
    {
        m_causeTree.rows.reserve( estimatedRows );
    }

    auto modelIndexForId = [&]( ReplayBodyId id ) -> int
    {
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == id.value )
            {
                return i;
            }
        }
        return -1;
    };

    auto idForModelIndex = [&]( int modelIndex ) -> ReplayBodyId
    {
        ReplayBodyId id;
        if ( modelIndex < 0 )
        {
            return id;
        }
        if ( solverSample )
        {
            id = ReplayBodyIdForModelIndex( *solverSample, modelIndex );
            if ( id.value != 0 )
            {
                return id;
            }
        }
        if ( modelIndex < static_cast<int>( models.size() ) )
        {
            id.value = models[static_cast<std::size_t>( modelIndex )].GetReplayBodyId();
        }
        return id;
    };

    auto writeName =
        [&]( ReplayBodyId id, int modelIndex, const char* fallback, char* out, std::size_t outSize ) -> void
    {
        out[0] = '\0';
        if ( fallback && fallback[0] != '\0' )
        {
            strncpy_s( out, outSize, fallback, _TRUNCATE );
            return;
        }
        if ( modelIndex >= 0 && modelIndex < static_cast<int>( models.size() ) )
        {
            const char* modelName = models[static_cast<std::size_t>( modelIndex )].GetName();
            if ( modelName && modelName[0] != '\0' )
            {
                strncpy_s( out, outSize, modelName, _TRUNCATE );
                return;
            }
        }
        if ( solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyById( *solverSample, id ) )
            {
                if ( body->name[0] != '\0' )
                {
                    strncpy_s( out, outSize, body->name, _TRUNCATE );
                    return;
                }
            }
        }
        sprintf_s( out, outSize, "body_%u", id.value );
    };

    auto appendSolverRowsForBody = [&]( RunReplayCauseTreeRow bodyRow ) -> void
    {
        if ( usePrediction )
        {
            for ( int i = 0; i < static_cast<int>( nodes.size() ); ++i )
            {
                const RunReplayPathTraceNode& node = nodes[static_cast<std::size_t>( i )];
                if ( node.id.value != bodyRow.id.value )
                {
                    continue;
                }
                RunReplayCauseTreeRow contactRow;
                contactRow.kind = RunReplayCauseTreeRowKind::PredictionContact;
                contactRow.id = bodyRow.id;
                contactRow.parentId = node.parentId;
                contactRow.firstFrame = node.firstFrame;
                contactRow.depth = bodyRow.depth + 1;
                contactRow.modelIndex = bodyRow.modelIndex;
                contactRow.contactIndex = i;
                contactRow.prediction = true;
                contactRow.point = node.contactPoint;
                contactRow.normal = ReplayNormalizeOr( node.contactNormal, Vector3( 0.0f, 1.0f, 0.0f ) );
                sprintf_s( contactRow.name, sizeof( contactRow.name ), "Predicted contact" );
                sprintf_s( contactRow.detail,
                           sizeof( contactRow.detail ),
                           "first frame %llu  normal %.2f %.2f %.2f",
                           static_cast<unsigned long long>( node.firstFrame ),
                           contactRow.normal.x,
                           contactRow.normal.y,
                           contactRow.normal.z );
                m_causeTree.rows.push_back( contactRow );
            }
            return;
        }

        if ( !solverSample || bodyRow.modelIndex < 0 )
        {
            return;
        }

        struct ManifoldGroup
        {
            int otherModelIndex = -1;
            bool terrain = false;
        };
        std::vector<ManifoldGroup> groups;
        groups.reserve( solverSample->worldSnapshot.persistentContacts.size() );
        for ( const ReplaySolverPersistentContactSample& contact : solverSample->worldSnapshot.persistentContacts )
        {
            if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
            {
                continue;
            }
            const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
            const bool terrain = contact.isTerrain || otherModelIndex < 0;
            bool exists = false;
            for ( const ManifoldGroup& group : groups )
            {
                if ( group.otherModelIndex == otherModelIndex && group.terrain == terrain )
                {
                    exists = true;
                    break;
                }
            }
            if ( !exists )
            {
                groups.push_back( { otherModelIndex, terrain } );
            }
        }

        for ( const ManifoldGroup& group : groups )
        {
            Vector3 centroid = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            Vector3 normalSum = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            float maxPenetration = 0.0f;
            int pointCount = 0;
            int firstContactIndex = -1;
            uint32_t firstFeatureId = 0;
            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.persistentContacts.size() ); ++i )
            {
                const ReplaySolverPersistentContactSample& contact =
                    solverSample->worldSnapshot.persistentContacts[static_cast<std::size_t>( i )];
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }
                const Vector3 point = ReplayContactPoint( *solverSample, contact );
                centroid += point;
                normalSum += ReplayContactNormalForModel( contact, bodyRow.modelIndex );
                maxPenetration = (std::max)( maxPenetration, contact.penetration );
                pointCount += 1;
                if ( firstContactIndex < 0 )
                {
                    firstContactIndex = i;
                    firstFeatureId = contact.featureId;
                }
            }
            if ( pointCount <= 0 )
            {
                continue;
            }
            centroid /= static_cast<float>( pointCount );
            const ReplayBodyId otherId = idForModelIndex( group.otherModelIndex );

            char otherName[64] = {};
            if ( group.terrain )
            {
                strncpy_s( otherName, sizeof( otherName ), "terrain", _TRUNCATE );
            }
            else
            {
                writeName( otherId, group.otherModelIndex, nullptr, otherName, sizeof( otherName ) );
            }

            RunReplayCauseTreeRow manifoldRow;
            manifoldRow.kind = RunReplayCauseTreeRowKind::Manifold;
            manifoldRow.id = bodyRow.id;
            manifoldRow.parentId = bodyRow.parentId;
            manifoldRow.counterpartId = otherId;
            manifoldRow.depth = bodyRow.depth + 1;
            manifoldRow.modelIndex = bodyRow.modelIndex;
            manifoldRow.counterpartModelIndex = group.otherModelIndex;
            manifoldRow.contactIndex = firstContactIndex;
            manifoldRow.featureId = static_cast<int>( firstFeatureId );
            manifoldRow.manifoldPointCount = pointCount;
            manifoldRow.penetration = maxPenetration;
            manifoldRow.point = centroid;
            manifoldRow.normal = ReplayNormalizeOr( normalSum, Vector3( 0.0f, 1.0f, 0.0f ) );
            manifoldRow.terrain = group.terrain;
            sprintf_s( manifoldRow.name, sizeof( manifoldRow.name ), "Manifold vs %s", otherName );
            sprintf_s( manifoldRow.detail,
                       sizeof( manifoldRow.detail ),
                       "%d point%s  max pen %.3f",
                       pointCount,
                       pointCount == 1 ? "" : "s",
                       maxPenetration );
            m_causeTree.rows.push_back( manifoldRow );

            for ( int i = 0; i < static_cast<int>( solverSample->worldSnapshot.persistentContacts.size() ); ++i )
            {
                const ReplaySolverPersistentContactSample& contact =
                    solverSample->worldSnapshot.persistentContacts[static_cast<std::size_t>( i )];
                if ( !ReplayContactHasModelIndex( contact, bodyRow.modelIndex ) )
                {
                    continue;
                }
                const int otherModelIndex = ReplayContactOtherModelIndex( contact, bodyRow.modelIndex );
                const bool terrain = contact.isTerrain || otherModelIndex < 0;
                if ( otherModelIndex != group.otherModelIndex || terrain != group.terrain )
                {
                    continue;
                }

                RunReplayCauseTreeRow solverRow;
                solverRow.kind = RunReplayCauseTreeRowKind::SolverRow;
                solverRow.id = bodyRow.id;
                solverRow.parentId = bodyRow.parentId;
                solverRow.counterpartId = otherId;
                solverRow.depth = bodyRow.depth + 2;
                solverRow.modelIndex = bodyRow.modelIndex;
                solverRow.counterpartModelIndex = group.otherModelIndex;
                solverRow.contactIndex = i;
                solverRow.solverRowIndex = i;
                solverRow.pipelineIndex = ReplayFindPipelineIndexForContact( solverSample->worldSnapshot, contact );
                solverRow.featureId = static_cast<int>( contact.featureId );
                solverRow.manifoldPointCount = contact.manifoldPointCount;
                solverRow.penetration = contact.penetration;
                solverRow.normalImpulse = contact.accN;
                solverRow.tangentImpulse = sqrtf( contact.accT1 * contact.accT1 + contact.accT2 * contact.accT2 );
                solverRow.warmStartImpulse = contact.terrainWarmStart;
                solverRow.bias = contact.bias;
                solverRow.effectiveMass = contact.normalMass;
                solverRow.frictionLimit = contact.frictionLimit;
                solverRow.point = ReplayContactPoint( *solverSample, contact );
                solverRow.normal = ReplayContactNormalForModel( contact, bodyRow.modelIndex );
                solverRow.impulse = ReplayContactImpulseForModel( contact, bodyRow.modelIndex );
                solverRow.terrain = terrain;
                solverRow.warmStarted = contact.warmStarted;
                sprintf_s( solverRow.name, sizeof( solverRow.name ), "Solver row %d", i );
                const char* traceStage = "";
                if ( solverRow.pipelineIndex >= 0 )
                {
                    const PhysicsPipelineRecord& record =
                        solverSample->worldSnapshot.pipelineTrace[static_cast<std::size_t>( solverRow.pipelineIndex )];
                    traceStage = PhysicsPipelineStageName( record.stage );
                }
                sprintf_s( solverRow.detail,
                           sizeof( solverRow.detail ),
                           "feature %u  n %.3f  t %.3f  bias %.3f  mass %.3f  limit %.3f  %s%s%s",
                           contact.featureId,
                           solverRow.normalImpulse,
                           solverRow.tangentImpulse,
                           solverRow.bias,
                           solverRow.effectiveMass,
                           solverRow.frictionLimit,
                           contact.warmStarted ? "warm" : "cold",
                           solverRow.pipelineIndex >= 0 ? "  " : "",
                           traceStage );
                m_causeTree.rows.push_back( solverRow );
            }
        }
    };

    auto addBodyRow = [&]( ReplayBodyId id,
                           ReplayBodyId parentId,
                           ReplayFrameIndex firstFrame,
                           int depth,
                           int modelIndex,
                           const char* fallbackName ) -> bool
    {
        if ( id.value == 0 )
        {
            return false;
        }

        RunReplayCauseTreeRow row;
        row.kind = RunReplayCauseTreeRowKind::Body;
        row.id = id;
        row.parentId = parentId;
        row.firstFrame = firstFrame;
        row.depth = depth;
        row.modelIndex = modelIndex >= 0 ? modelIndex : modelIndexForId( id );
        row.prediction = usePrediction;
        writeName( id, row.modelIndex, fallbackName, row.name, sizeof( row.name ) );
        if ( row.modelIndex >= 0 && solverSample )
        {
            if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( *solverSample, row.modelIndex ) )
            {
                sprintf_s( row.detail,
                           sizeof( row.detail ),
                           "contacts %u  max pen %.3f  impulse %.3f",
                           static_cast<unsigned int>( body->contactCount ),
                           body->maxPenetration,
                           body->normalImpulseSum );
            }
        }
        else if ( firstFrame > 0 )
        {
            sprintf_s( row.detail,
                       sizeof( row.detail ),
                       "first affected frame %llu",
                       static_cast<unsigned long long>( firstFrame ) );
        }
        m_causeTree.rows.push_back( row );
        appendSolverRowsForBody( m_causeTree.rows.back() );
        return true;
    };

    addBodyRow( m_pathVisualizer.targetId,
                ReplayBodyId{},
                0,
                0,
                m_pathVisualizer.targetModelIndex,
                m_pathVisualizer.targetName );

    auto addChildren = [&]( auto&& self, ReplayBodyId parentId, int fallbackDepth ) -> void
    {
        for ( const RunReplayPathTraceNode& node : nodes )
        {
            if ( node.parentId.value != parentId.value )
            {
                continue;
            }
            const int depth = node.depth > 0 ? node.depth : fallbackDepth;
            if ( addBodyRow( node.id, parentId, node.firstFrame, depth, modelIndexForId( node.id ), nullptr ) )
            {
                self( self, node.id, depth + 1 );
            }
        }
    };
    addChildren( addChildren, m_pathVisualizer.targetId, 1 );

    m_causeTree.selectedRow = -1;
    if ( m_camera.focusKind != RunReplayCameraFocusKind::None )
    {
        for ( int i = 0; i < static_cast<int>( m_causeTree.rows.size() ); ++i )
        {
            const RunReplayCauseTreeRow& row = m_causeTree.rows[static_cast<std::size_t>( i )];
            if ( row.kind != m_camera.focusRowKind || row.id.value != m_camera.focusedId.value ||
                 row.modelIndex != m_camera.focusModelIndex || row.terrain != m_camera.focusTerrain )
            {
                continue;
            }
            if ( row.kind == RunReplayCauseTreeRowKind::Body ||
                 ( row.counterpartId.value == m_camera.counterpartId.value &&
                   row.counterpartModelIndex == m_camera.focusCounterpartModelIndex &&
                   ( row.kind != RunReplayCauseTreeRowKind::SolverRow ||
                     ( row.featureId == m_camera.focusFeatureId &&
                       row.solverRowIndex == m_camera.focusSolverRowIndex ) ) ) )
            {
                m_causeTree.selectedRow = i;
                m_camera.focusedRow = i;
                break;
            }
        }
    }
    if ( m_causeTree.selectedRow >= static_cast<int>( m_causeTree.rows.size() ) )
    {
        m_causeTree.selectedRow = -1;
    }
    return !m_causeTree.rows.empty();
}


bool ReplayRuntime::BuildPredictionGhostDrawRequests( const std::vector<GameObjects::GameModel>& models )
{
    m_predictionGhostDrawRequests.clear();
    const std::vector<RunReplayPredictionFrame>& frames = ActivePredictionFrames();
    if ( !m_prediction.enabled || !m_prediction.ragdollVisualsEnabled || frames.size() < 2 )
    {
        return false;
    }

    bool hasRagdollPart = false;
    for ( const GameObjects::GameModel& model : models )
    {
        if ( ReplayRuntimeModelIsRagdollPart( model ) )
        {
            hasRagdollPart = true;
            break;
        }
    }
    if ( !hasRagdollPart )
    {
        return false;
    }

    const std::size_t lastIndex = frames.size() - 1;
    const std::size_t stride =
        (std::max)( static_cast<std::size_t>( 1 ),
                    ( lastIndex + REPLAY_PREDICTION_GHOST_MAX_FRAMES - 1 ) / REPLAY_PREDICTION_GHOST_MAX_FRAMES );
    const ReplayFrameIndex lastFrame = frames.back().frameIndex;
    m_predictionGhostDrawRequests.reserve( (std::min)( frames.size(), REPLAY_PREDICTION_GHOST_MAX_FRAMES + 1 ) *
                                           models.size() );

    auto appendGhostFrame = [&]( std::size_t index )
    {
        const RunReplayPredictionFrame& predictionFrame = frames[index];
        if ( predictionFrame.frameIndex == 0 )
        {
            return;
        }

        const float t =
            lastFrame > 0
                ? std::clamp( static_cast<float>( predictionFrame.frameIndex ) / static_cast<float>( lastFrame ),
                              0.0f,
                              1.0f )
                : 1.0f;
        const float alpha = std::clamp( 0.055f + ( 1.0f - t ) * 0.105f, 0.045f, 0.18f );

        for ( const RunReplayPredictionBodySample& body : predictionFrame.bodies )
        {
            if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
            {
                continue;
            }

            const GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
            if ( model.GetReplayBodyId() != body.id.value || !ReplayRuntimeModelIsRagdollPart( model ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelIndex = body.modelIndex;
            request.position = body.position;
            request.orientation = body.orientation;
            request.orientation.Normalise();
            request.alpha = alpha;
            m_predictionGhostDrawRequests.push_back( request );
        }
    };

    std::size_t farIndex = lastIndex;
    if ( farIndex % stride != 0 )
    {
        appendGhostFrame( farIndex );
        farIndex = ( farIndex / stride ) * stride;
    }
    for ( std::size_t index = farIndex; index >= stride; index -= stride )
    {
        appendGhostFrame( index );
        if ( index == stride )
        {
            break;
        }
    }
    return !m_predictionGhostDrawRequests.empty();
}

const std::vector<ReplayPredictionGhostDrawRequest>& ReplayRuntime::PredictionGhostDrawRequests() const
{
    return m_predictionGhostDrawRequests;
}


bool ReplayRuntime::BuildFocusModelMask( const GameObjects::GameModelCollection& collection )
{
    PROFILE_SCOPED( "Frame/Replay/FocusMask" );
    const int modelCount = collection.GetModelCount();
    if ( !m_pathVisualizer.hasTarget || m_pathVisualizer.targetId.value == 0 || modelCount <= 0 )
    {
        m_focusModelMask.clear();
        return false;
    }

    m_focusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
    const std::vector<GameObjects::GameModel>& models = collection.Models();
    int markedCount = 0;
    const auto markByReplayId = [&]( ReplayBodyId id, int preferredModelIndex )
    {
        if ( id.value == 0 )
        {
            return;
        }

        int resolvedIndex = -1;
        if ( preferredModelIndex >= 0 && preferredModelIndex < modelCount &&
             models[static_cast<std::size_t>( preferredModelIndex )].GetReplayBodyId() == id.value )
        {
            resolvedIndex = preferredModelIndex;
        }
        else
        {
            for ( int i = 0; i < modelCount; ++i )
            {
                if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == id.value )
                {
                    resolvedIndex = i;
                    break;
                }
            }
        }

        if ( resolvedIndex >= 0 )
        {
            uint8_t& mask = m_focusModelMask[static_cast<std::size_t>( resolvedIndex )];
            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( m_pathVisualizer.targets.empty() )
    {
        markByReplayId( m_pathVisualizer.targetId, m_pathVisualizer.targetModelIndex );
    }
    else
    {
        for ( const RunReplayPathTarget& target : m_pathVisualizer.targets )
        {
            markByReplayId( target.id, target.modelIndex );
        }
    }

    const std::vector<RunReplayPathTraceNode>& futureNodes =
        m_prediction.enabled ? m_prediction.futureNodes : m_pathVisualizer.futureNodes;
    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markByReplayId( node.id, node.modelIndex );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        m_focusModelMask.clear();
        return false;
    }
    return true;
}


std::vector<uint8_t>& ReplayRuntime::FocusModelMask()
{
    return m_focusModelMask;
}

const std::vector<uint8_t>& ReplayRuntime::FocusModelMask() const
{
    return m_focusModelMask;
}

bool ReplayRuntime::HasLauncherVisualBackup() const
{
    return m_launcherVisualBackupActive;
}

void ReplayRuntime::StoreLauncherVisualBackup( const ReplayLauncherVisualSample& sample )
{
    m_launcherVisualBackup = sample;
    m_launcherVisualBackupActive = true;
}

const ReplayLauncherVisualSample& ReplayRuntime::LauncherVisualBackup() const
{
    return m_launcherVisualBackup;
}

void ReplayRuntime::ClearLauncherVisualBackup()
{
    m_launcherVisualBackup = ReplayLauncherVisualSample();
    m_launcherVisualBackupActive = false;
}

MainMemoryReplayStats ReplayRuntime::CollectMemoryStats() const
{
    MainMemoryReplayStats stats;
    const ReplayRecorderStats presentationStats = m_presentation.GetStats();
    const ReplayRecorderStats solverStats = m_solver.GetStats();
    const ReplayEventRecorderStats eventStats = m_events.GetStats();

    stats.presentationBytes = m_presentation.CollectMemoryBytes();
    stats.solverBytes = m_solver.CollectMemoryBytes();
    stats.eventsBytes = m_events.CollectMemoryBytes();
    stats.presentationSamples = presentationStats.sampleCount;
    stats.solverSamples = solverStats.sampleCount;
    stats.eventSamples = eventStats.eventCount;

    stats.loadedReplayBytes =
        static_cast<uint64_t>( sizeof( m_loadedPresentation ) ) + VectorCapacityBytes( m_loadedPresentation.samples );
    for ( const ReplayPresentationSample& sample : m_loadedPresentation.samples )
    {
        stats.loadedReplayBytes += PresentationSampleMemoryBytes( sample );
    }
    stats.loadedReplaySamples = m_loadedPresentation.samples.size();

    stats.predictionBytes = static_cast<uint64_t>( sizeof( m_prediction ) );
    stats.predictionBytes += SolverWorldSnapshotMemoryBytes( m_prediction.predictionWorld );
    stats.predictionBytes += SolverWorldSnapshotMemoryBytes( m_prediction.liveRestoreWorld );
    stats.predictionBytes += VectorCapacityBytes( m_prediction.predictionBodies );
    stats.predictionBytes += VectorCapacityBytes( m_prediction.liveRestoreBodies );
    stats.predictionBytes += VectorCapacityBytes( m_prediction.frames );
    stats.predictionBytes += VectorCapacityBytes( m_prediction.buildFrames );
    stats.predictionBytes += VectorCapacityBytes( m_prediction.futureNodes );
    for ( const RunReplayPredictionFrame& frame : m_prediction.frames )
    {
        stats.predictionBytes += PredictionFrameMemoryBytes( frame );
    }
    for ( const RunReplayPredictionFrame& frame : m_prediction.buildFrames )
    {
        stats.predictionBytes += PredictionFrameMemoryBytes( frame );
    }
    stats.predictionFrames = m_prediction.frames.size() + m_prediction.buildFrames.size();

    stats.pathAndCauseBytes = static_cast<uint64_t>( sizeof( m_pathVisualizer ) + sizeof( m_causeTree ) );
    stats.pathAndCauseBytes += VectorCapacityBytes( m_pathVisualizer.futureNodes );
    stats.pathAndCauseBytes += VectorCapacityBytes( m_pathVisualizer.targets );
    stats.pathAndCauseBytes += VectorCapacityBytes( m_causeTree.rows );
    stats.pathNodes = m_pathVisualizer.futureNodes.size() + m_prediction.futureNodes.size();
    stats.causeRows = m_causeTree.rows.size();

    stats.renderScratchBytes = VectorCapacityBytes( m_renderPoseBackups );
    stats.renderScratchBytes += VectorCapacityBytes( m_predictionGhostDrawRequests );
    stats.renderScratchBytes += VectorCapacityBytes( m_focusModelMask );
    stats.renderScratchBytes += static_cast<uint64_t>( sizeof( m_launcherVisualBackup ) );
    stats.renderScratchBytes += LauncherVisualMemoryBytes( m_launcherVisualBackup );
    stats.ghostRequests = m_predictionGhostDrawRequests.size();

    stats.totalBytes = stats.presentationBytes + stats.solverBytes + stats.eventsBytes + stats.loadedReplayBytes +
                       stats.predictionBytes + stats.pathAndCauseBytes + stats.renderScratchBytes;
    return stats;
}

void ReplayRuntime::RecordEvent( ReplayEventKind kind,
                                 ReplayFrameIndex frameIndex,
                                 uint32_t flags,
                                 int32_t value0,
                                 int32_t value1,
                                 int32_t value2,
                                 int32_t value3,
                                 uint64_t data0,
                                 const char* text )
{
    if ( !m_events.IsEnabled() )
    {
        return;
    }

    ReplayEventInput input;
    input.frameIndex = frameIndex;
    input.branch = m_branch;
    input.kind = kind;
    input.flags = flags;
    input.value0 = value0;
    input.value1 = value1;
    input.value2 = value2;
    input.value3 = value3;
    input.data0 = data0;
    input.text = text;
    m_events.RecordEvent( input );
}

bool ReplayRuntime::SaveSolverReplay( const char* path ) const
{
    return ReplayExporter::Save( m_solver, path );
}

bool ReplayRuntime::SavePresentationWithSolverHashes( const char* path, ReplayV2SaveResult* result ) const
{
    return ReplayV2Artifact::SavePresentationWithSolverHashes( m_presentation, m_solver, m_events, path, result );
}
} // namespace SkullbonezCore::Basics
