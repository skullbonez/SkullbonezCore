/*
File: SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp
Purpose:
  Implements retained replay recording, loading, event, and memory-policy state.

Mental model:
  ReplayTimeline is one bounded history book with presentation, solver, and
  event columns. A frame advances those columns together; retention changes
  resize their windows together, and loaded artifacts occupy a separate cold
  storage section.

Glossary:
  Paired capture: One solver sample and its presentation projection sharing a
  frame index and presentation hash.
  Event cursor: Sequence boundary linking a solver checkpoint to recorded
  commands that occurred before it.

Invariants:
  - Solver and presentation capture advance as one transaction when solver
    recording is enabled.
  - Retention changes reconfigure all three tracks together.
  - Hash-log paths stay paired beside the presentation log.

Related:
  - ReplayTimeline.h
  - ReplayRecorder.cpp
  - ReplayRuntime.cpp
*/
#include "ReplayTimeline.h"

#include "../RuntimeFileWriter.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace SkullbonezCore::Runtime
{
namespace
{
std::string SolverReplayHashLogPath( const std::string& presentationPath )
{
    // Why: paired log names let diagnostics copy or remove one capture without
    // searching for a solver artifact in a second directory.
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

ReplayRecordingConfigResult ReplayTimeline::ConfigureRecording( bool enabled,
                                                                int retentionSeconds,
                                                                const char* hashLogPath,
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
    if ( hashLogPath && hashLogPath[0] != '\0' )
    {
        replayConfig.hashLogPath = hashLogPath;
    }

    ReplayRecorderConfig solverReplayConfig = replayConfig;
    solverReplayConfig.retentionSeconds = m_memoryPolicy.solverRetentionSeconds;
    solverReplayConfig.checkpointIntervalFrames = 60;
    solverReplayConfig.hashLogPath = SolverReplayHashLogPath( replayConfig.hashLogPath );

    m_presentation.Configure( replayConfig );
    m_solver.Configure( solverReplayConfig );
    m_events.Configure( replayConfig );

    ReplayRecordingConfigResult result;
    result.presentationConfig = replayConfig;
    result.solverConfig = solverReplayConfig;
    result.presentationStats = m_presentation.GetStats();
    result.solverStats = m_solver.GetStats();
    result.eventStats = m_events.GetStats();
    return result;
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
    ConfigureRecording( m_recordingEnabled,
                        m_memoryPolicy.requestedRetentionSeconds,
                        m_recordingHashLogPath.empty() ? nullptr : m_recordingHashLogPath.c_str(),
                        m_recordingRuntimeBodyCapacity );
    result.recordersReset = true;
    return result;
}

void ReplayTimeline::FlushHashLogs()
{
    m_presentation.FlushHashLog();
    m_solver.FlushHashLog();
}

void ReplayTimeline::Reset( const char* sceneLabel )
{
    m_presentation.ResetTimeline( sceneLabel );
    m_solver.ResetTimeline( sceneLabel );
    m_events.ResetTimeline( sceneLabel );
}

void ReplayTimeline::ClearLoadedPresentation()
{
    m_loadedPresentation = RunLoadedReplayPresentationState{};
}

void ReplayTimeline::InstallLoadedPresentation( const char* path,
                                                std::vector<ReplayPresentationSample>& samples,
                                                std::size_t bodyDictionaryCount,
                                                std::size_t fileBytes,
                                                ReplayFrameIndex firstFrame,
                                                ReplayFrameIndex lastFrame )
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

bool ReplayTimeline::NextPresentationSavePath( char* outPath, std::size_t outPathSize )
{
    return RuntimeFileWriter::NextNumberedPath( outPath,
                                                outPathSize,
                                                "replays",
                                                "replay_v2_",
                                                ".skreplay",
                                                m_presentationSaveSequence );
}

void ReplayTimeline::RecordEvent( const ReplayEventInput& input )
{
    if ( m_events.IsEnabled() )
    {
        m_events.RecordEvent( input );
    }
}

void ReplayTimeline::CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const
{
    m_presentation.CollectMemoryCategoryBytes( categories );
    m_solver.CollectMemoryCategoryBytes( categories );
    m_events.CollectMemoryCategoryBytes( categories );
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
                 m_captureMismatchReports,
                 static_cast<unsigned long long>( presentation->frameIndex ),
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
        fprintf( stderr,
                 "[replay] Further solver/presentation capture mismatch diagnostics suppressed for this replay "
                 "timeline.\n" );
    }
}

ReplayTimelineCaptureResult ReplayTimeline::CaptureFrame( ReplayCaptureInput input )
{
    ReplayTimelineCaptureResult result;
    input.eventCursor = m_events.GetStats().nextSequence;
    if ( m_solver.IsEnabled() )
    {
        const ReplayFrameIndex expectedSolverFrame = m_solver.GetStats().nextFrameIndex;
        m_solver.CaptureFrame( input );
        const ReplaySolverFrameSample* solverSample = m_solver.LatestSample();
        if ( solverSample && solverSample->frameIndex == expectedSolverFrame )
        {
            // Why: the solver sample already contains presentation-facing body
            // fields and its hash, so a paired capture needs only one store walk.
            m_presentation.CaptureFrameFromSolverSample( *solverSample );
            ReportLatestCaptureMismatch();
            result.solverSample = solverSample;
            return result;
        }
    }

    m_presentation.CaptureFrame( input );
    ReportLatestCaptureMismatch();
    return result;
}
} // namespace SkullbonezCore::Runtime
