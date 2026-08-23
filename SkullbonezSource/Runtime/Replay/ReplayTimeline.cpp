/*
File: SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp
Purpose:
  Implements retained replay recording, loading, event, and memory-policy state.

Summary:
  ReplayTimeline is one bounded history book with presentation, solver, and
  event columns. A frame advances those columns together; retention changes
  resize their windows together, and loaded artifacts occupy a separate cold
  storage section.

Glossary:
  Paired capture: One solver sample and its presentation projection sharing a
  frame index and presentation hash.

Invariants:
  - Solver and presentation capture advance as one transaction when solver
    recording is enabled.
  - Retention changes reconfigure all three tracks together.
  - Hash-log paths stay paired beside the presentation log.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayTimeline.h
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp
  - SkullbonezSource/Runtime/App/ReplayRuntime.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayTimeline.h"

#include "ReplayV2Artifact.h"


#include <cstdio>
#include <cstring>
#include <string>

namespace SkullbonezCore::Runtime
{
using namespace ReplayTimelineOperations;
namespace
{
template <typename T> uint64_t ReplayTimelineVectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

} // namespace

ReplayRecordingConfigResult ReplayTimeline::ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath,
                                                                int runtimeBodyCapacity )
{
    m_recordingConfigured = true;
    m_recordingEnabled = enabled || ( hashLogPath && hashLogPath[0] != '\0' );
    m_recordingRuntimeBodyCapacity = runtimeBodyCapacity;
    m_recordingHashLogPath = hashLogPath ? hashLogPath : "";
    m_memoryPolicy.requestedRetentionSeconds = (std::max)( 1, retentionSeconds );
    m_memoryPolicy = ResolveReplayMemoryPolicy( m_memoryPolicy );

    ReplayRecorderConfig replayConfig;
    replayConfig.enabled = m_recordingEnabled;
    replayConfig.retentionSeconds = m_memoryPolicy.presentationRetentionSeconds;
    replayConfig.checkpointIntervalFrames = 30;
    replayConfig.runtimeBodyCapacity = runtimeBodyCapacity;
    ReplayRecorderConfig solverReplayConfig = replayConfig;
    solverReplayConfig.retentionSeconds = m_memoryPolicy.solverRetentionSeconds;
    solverReplayConfig.checkpointIntervalFrames = 60;

    m_presentation.Configure( replayConfig );
    m_solver.Configure( solverReplayConfig );
    m_events.Configure( replayConfig );

    const ReplayRecorderStats presentationStats = m_presentation.GetStats();
    const ReplayRecorderStats solverStats = m_solver.GetStats();
    m_artifactHashLog.Configure( hashLogPath, replayConfig, solverReplayConfig, presentationStats, solverStats );

    ReplayRecordingConfigResult result;
    result.presentationConfig = replayConfig;
    result.solverConfig = solverReplayConfig;
    result.presentationStats = presentationStats;
    result.solverStats = solverStats;
    result.eventStats = m_events.GetStats();
    return result;
}

bool ReplayTimeline::SetRecordingEnabled( bool enabled ) noexcept
{
    // Hazard: hash-log capture is a startup validation contract and cannot be
    // paused by an editor surface. Ordinary recording may stop without
    // reconfiguring or clearing the already reserved retained rings.
    if ( !m_recordingConfigured || !m_recordingHashLogPath.empty() ||
         ( enabled && !m_solver.IsEnabled() && !m_presentation.IsEnabled() ) )
    {
        return false;
    }

    m_recordingEnabled = enabled;
    return true;
}

ReplayMemoryPolicyApplyResult ReplayTimeline::ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request )
{
    ReplayMemoryPolicyApplyResult result;
    ReplayMemoryPolicy nextPolicy = m_memoryPolicy;

    if ( request.presetIndex >= 0 )
    {
        nextPolicy = ReplayMemoryPresetPolicy( ReplayMemoryPresetFromIndex( request.presetIndex ) );
    }

    if ( request.retentionSeconds > 0 )
    {
        nextPolicy.requestedRetentionSeconds = request.retentionSeconds;
    }

    if ( request.budgetMiB > 0 )
    {
        nextPolicy.requestedBudgetMiB = request.budgetMiB;
    }

    nextPolicy = ResolveReplayMemoryPolicy( nextPolicy );

    if ( nextPolicy.preset == m_memoryPolicy.preset &&
         nextPolicy.requestedRetentionSeconds == m_memoryPolicy.requestedRetentionSeconds &&
         nextPolicy.requestedBudgetMiB == m_memoryPolicy.requestedBudgetMiB &&
         nextPolicy.presentationRetentionSeconds == m_memoryPolicy.presentationRetentionSeconds &&
         nextPolicy.solverRetentionSeconds == m_memoryPolicy.solverRetentionSeconds )
    {
        return result;
    }

    m_memoryPolicy = nextPolicy;
    result.changed = true;

    if ( !m_recordingConfigured )
    {
        return result;
    }

    // Hazard: changing retention invalidates every normalized cursor. Keep the
    // three recorder windows atomic so no frame observes mixed history ranges.
    ConfigureRecording( m_recordingEnabled, m_memoryPolicy.requestedRetentionSeconds,
                        m_recordingHashLogPath.empty() ? nullptr : m_recordingHashLogPath.c_str(),
                        m_recordingRuntimeBodyCapacity );

    result.recordersReset = true;
    return result;
}

void ReplayTimeline::FlushHashLogs()
{
    m_artifactHashLog.Flush();
}

void ReplayTimeline::Reset( const char* sceneLabel )
{
    m_presentation.ResetTimeline( sceneLabel );
    m_solver.ResetTimeline( sceneLabel );
    m_events.ResetTimeline( sceneLabel );
    m_artifactHashLog.ResetTimeline( sceneLabel );
}

void ReplayTimeline::ClearLoadedPresentation()
{
    m_loadedPresentation = RunLoadedReplayPresentationState {};
}

bool ReplayTimeline::LoadPresentationArtifact( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        return false;
    }

    // Why: decode into cold temporary storage and publish it atomically only
    // after validation, so a failed picker load preserves the previous track.
    std::vector<ReplayPresentationSample> samples;
    ReplayV2LoadResult result;

    if ( !ReplayV2Artifact::LoadPresentation( path, samples, &result ) || samples.size() < 2 )
    {
        return false;
    }

    InstallLoadedPresentation( path, samples, result.bodyDictionaryCount, result.fileBytes, result.firstFrame,
                               result.lastFrame );

    printf( "[replay] Loaded v2 presentation artifact: path=%s samples=%llu bodies=%llu first_frame=%llu "
            "last_frame=%llu bytes=%llu\n",
            m_loadedPresentation.path, static_cast<unsigned long long>( m_loadedPresentation.samples.size() ),
            static_cast<unsigned long long>( m_loadedPresentation.bodyDictionaryCount ),
            static_cast<unsigned long long>( m_loadedPresentation.firstFrame ),
            static_cast<unsigned long long>( m_loadedPresentation.lastFrame ),
            static_cast<unsigned long long>( m_loadedPresentation.fileBytes ) );

    return true;
}

void ReplayTimeline::InstallLoadedPresentation( const char* path, std::vector<ReplayPresentationSample>& samples,
                                                std::size_t bodyDictionaryCount, std::size_t fileBytes,
                                                ReplayFrameIndex firstFrame, ReplayFrameIndex lastFrame )
{
    ClearLoadedPresentation();
    m_loadedPresentation.samples.swap( samples );
    m_loadedPresentation.enabled = true;
    m_loadedPresentation.bodyDictionaryCount = bodyDictionaryCount;
    m_loadedPresentation.fileBytes = fileBytes;
    m_loadedPresentation.firstFrame = firstFrame;
    m_loadedPresentation.lastFrame = lastFrame;
    strncpy_s( m_loadedPresentation.path, sizeof( m_loadedPresentation.path ), path, _TRUNCATE );
}

void ReplayTimeline::RecordEvent( const ReplayEventInput& input )
{
    if ( m_recordingEnabled && m_events.IsEnabled() )
    {
        m_events.RecordEvent( input );
    }
}

void ReplayTimeline::SubmitEvent( const ReplayEventCommand& command, const ReplayBranchInfo& branch )
{
    if ( command.kind == ReplayEventKind::Unknown || !m_recordingEnabled || !m_events.IsEnabled() )
    {
        return;
    }

    ReplayEventInput input;

    if ( command.useNextFrame )
    {
        const ReplayRecorderStats solverStats = m_solver.GetStats();
        input.frameIndex = solverStats.enabled ? solverStats.nextFrameIndex : m_presentation.GetStats().nextFrameIndex;
    }
    else
    {
        input.frameIndex = command.frameIndex;
    }

    input.branch = branch;
    input.kind = command.kind;
    input.flags = command.flags;
    input.value0 = command.value0;
    input.value1 = command.value1;
    input.value2 = command.value2;
    input.value3 = command.value3;
    input.data0 = command.data0;
    input.text = command.text;
    m_events.RecordEvent( input );
}

void ReplayTimeline::CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const
{
    m_presentation.CollectMemoryCategoryBytes( categories );
    m_solver.CollectMemoryCategoryBytes( categories );
    m_events.CollectMemoryCategoryBytes( categories );
}

ReplayTimelineMemoryStats ReplayTimeline::CollectMemoryStats() const
{
    ReplayTimelineMemoryStats stats;
    CollectMemoryCategoryBytes( stats.categoryBytes );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedOwner,
                                                            static_cast<uint64_t>( sizeof( m_loadedPresentation ) ) );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedSampleRecords,
                                          ReplayTimelineVectorCapacityBytes( m_loadedPresentation.samples ) );

    for ( const ReplayPresentationSample& sample : m_loadedPresentation.samples )
    {
        SkullbonezCore::Core::
            MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedBodies,
                                              ReplayTimelineVectorCapacityBytes( sample.bodies ) );
    }

    stats.policy = m_memoryPolicy;
    stats.presentationSamples = m_presentation.GetStats().sampleCount;
    stats.solverSamples = m_solver.GetStats().sampleCount;
    stats.eventSamples = m_events.GetStats().eventCount;
    stats.loadedSamples = m_loadedPresentation.samples.size();
    return stats;
}

void ReplayTimeline::ResetCaptureMismatchDiagnostics() noexcept
{
    m_captureMismatchReports = 0;
    m_captureMismatchSuppressed = false;
}

void ReplayTimeline::ReportLatestCaptureMismatch()
{
    const ReplayPresentationSample* presentation = m_presentation.LatestSample();
    const ReplaySolverFrameSample* solver = m_solver.LatestSample();

    if ( !presentation || !solver )
    {
        return;
    }

    const bool matches = presentation->frameIndex == solver->frameIndex &&
                         presentation->stateHash == solver->presentationHash &&
                         presentation->bodies.size() == solver->bodies.size();

    if ( matches )
    {
        return;
    }

    if ( m_captureMismatchReports < 8 )
    {
        ++m_captureMismatchReports;
        fprintf( stderr,
                 "[replay] Solver/presentation capture mismatch #%u: presentation_frame=%llu solver_frame=%llu "
                 "presentation_hash=0x%016llX solver_presentation_hash=0x%016llX solver_hash=0x%016llX "
                 "presentation_bodies=%llu solver_bodies=%llu\n",
                 m_captureMismatchReports, static_cast<unsigned long long>( presentation->frameIndex ),
                 static_cast<unsigned long long>( solver->frameIndex ),
                 static_cast<unsigned long long>( presentation->stateHash ),
                 static_cast<unsigned long long>( solver->presentationHash ),
                 static_cast<unsigned long long>( solver->solverHash ),
                 static_cast<unsigned long long>( presentation->bodies.size() ),
                 static_cast<unsigned long long>( solver->bodies.size() ) );
    }
    else if ( !m_captureMismatchSuppressed )
    {
        m_captureMismatchSuppressed = true;
        fprintf( stderr, "[replay] Further solver/presentation capture mismatch diagnostics suppressed for this replay "
                         "timeline.\n" );
    }
}

const ReplaySolverFrameSample*
ReplayTimeline::CaptureFrame( int sceneFrame, float physicsDt, const ReplayWorldPresentationSample& world,
                              const ReplayCameraSample& camera, const ReplayLauncherVisualSample& launcherVisual,
                              Physics::PhysicsEngine& physics, const Gameplay::TornadoGameplay& tornadoGameplay,
                              const SceneEntityStore& entities, const Physics::PhysicsBodyStore& bodyStore,
                              const Physics::ColliderStore& colliderStore, const ReplayBranchInfo& branch )
{
    if ( !m_recordingEnabled )
    {
        return nullptr;
    }

    const uint32_t eventCursor = m_events.GetStats().nextSequence;

    if ( m_solver.IsEnabled() )
    {
        const ReplayFrameIndex expectedSolverFrame = m_solver.GetStats().nextFrameIndex;
        m_solver.CaptureFrame( branch, eventCursor, sceneFrame, physicsDt, world, camera, launcherVisual, physics,
                               tornadoGameplay, entities, bodyStore, colliderStore );

        const ReplaySolverFrameSample* solverSample = m_solver.LatestSample();

        if ( solverSample && solverSample->frameIndex == expectedSolverFrame )
        {
            // Why: the solver sample already contains presentation-facing body
            // fields and its hash, so a paired capture needs only one store walk.
            m_presentation.CaptureFrameFromSolverSample( *solverSample );
            const ReplayPresentationSample* presentationSample = m_presentation.LatestSample();

            if ( presentationSample )
            {
                m_artifactHashLog.AppendPresentation( *presentationSample );
            }

            m_artifactHashLog.AppendSolver( *solverSample );
            ReportLatestCaptureMismatch();
            return solverSample;
        }
    }

    m_presentation.CaptureFrame( branch, eventCursor, sceneFrame, physicsDt, world, camera, physics, entities, bodyStore,
                                 colliderStore );

    const ReplayPresentationSample* presentationSample = m_presentation.LatestSample();

    if ( presentationSample )
    {
        m_artifactHashLog.AppendPresentation( *presentationSample );
    }

    ReportLatestCaptureMismatch();
    return nullptr;
}
} // namespace SkullbonezCore::Runtime
