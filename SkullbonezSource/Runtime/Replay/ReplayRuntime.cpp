/*
File: SkullbonezSource/Runtime/Replay/ReplayRuntime.cpp
Purpose:
  Provides the replay subsystem ownership boundary for legacy Run replay callers.
*/
#include "ReplayRuntime.h"
#include "ReplayExporter.h"
#include "ReplayV2Artifact.h"
#include "../../GameObjects/GameModel.h"
#include "../../GameObjects/GameModelCollection.h"

#include <algorithm>

namespace SkullbonezCore::Basics
{
namespace
{
std::string SolverReplayHashLogPath( const std::string& presentationPath )
{
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
