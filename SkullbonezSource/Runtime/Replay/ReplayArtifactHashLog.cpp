/*
File: SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.cpp
Purpose:
  Implements Replay ArtifactIO hash-log ownership and stable CSV formatting.

Summary:
  One owner derives the paired paths, owns both streams, and serializes already
  committed presentation/solver sample values without reaching into capture.

Invariants:
  - An unavailable stream fails independently and does not disable capture.
  - Headers are emitted on timeline reset, matching retained-ring reset order.
  - Rows are appended only after the corresponding capture is committed.

Related:
  - ReplayArtifactHashLog.h
  - ReplayTimeline.cpp
  - tools/validate_replay_visual_fidelity.bat
*/
#include "ReplayArtifactHashLog.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

namespace SkullbonezCore::Runtime
{
namespace
{
std::string SolverHashLogPath( const std::string& presentationPath )
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

int SaturatingFrameCount( std::size_t count )
{
    return static_cast<int>( (std::min)( count, static_cast<std::size_t>( ( std::numeric_limits<int>::max )() ) ) );
}
} // namespace

void ReplayArtifactHashLog::Configure( const char* requestedPath, ReplayRecorderConfig& presentationConfig,
                                       ReplayRecorderConfig& solverConfig, const ReplayRecorderStats& presentationStats,
                                       const ReplayRecorderStats& solverStats )
{
    m_presentation.close();
    m_solver.close();

    presentationConfig.hashLogPath = requestedPath ? requestedPath : "";
    solverConfig.hashLogPath = SolverHashLogPath( presentationConfig.hashLogPath );
    m_presentationRetentionSeconds = presentationConfig.retentionSeconds;
    m_presentationRetentionFrames = SaturatingFrameCount( presentationStats.sampleCapacity );
    m_presentationCheckpointInterval = presentationConfig.checkpointIntervalFrames;
    m_solverRetentionSeconds = solverConfig.retentionSeconds;
    m_solverRetentionFrames = SaturatingFrameCount( solverStats.sampleCapacity );
    m_solverCheckpointInterval = solverConfig.checkpointIntervalFrames;

    if ( !presentationConfig.hashLogPath.empty() )
    {
        m_presentation.open( presentationConfig.hashLogPath, std::ios::out | std::ios::trunc );

        if ( !m_presentation.is_open() )
        {
            fprintf( stderr, "[replay] Failed to open hash log: %s\n", presentationConfig.hashLogPath.c_str() );
            presentationConfig.hashLogPath.clear();
        }
    }

    if ( !solverConfig.hashLogPath.empty() )
    {
        m_solver.open( solverConfig.hashLogPath, std::ios::out | std::ios::trunc );

        if ( !m_solver.is_open() )
        {
            fprintf( stderr, "[replay] Failed to open hash log: %s\n", solverConfig.hashLogPath.c_str() );
            solverConfig.hashLogPath.clear();
        }
    }
}

void ReplayArtifactHashLog::ResetTimeline( const char* sceneLabel )
{
    const char* label = sceneLabel && sceneLabel[0] != '\0' ? sceneLabel : "generated";

    if ( m_presentation.is_open() )
    {
        m_presentation << "# replay_scene scene=\"" << label << "\" retention_seconds=" << m_presentationRetentionSeconds
                       << " retention_frames=" << m_presentationRetentionFrames
                       << " checkpoint_interval_frames=" << m_presentationCheckpointInterval << "\n";
        m_presentation
            << "frame,scene_frame,simulation_seconds,body_count,contact_count,pipeline_record_count,checkpoint,state_"
               "hash\n";
    }

    if ( m_solver.is_open() )
    {
        m_solver << "# solver_replay_scene scene=\"" << label << "\" retention_seconds=" << m_solverRetentionSeconds
                 << " retention_frames=" << m_solverRetentionFrames
                 << " checkpoint_interval_frames=" << m_solverCheckpointInterval << "\n";
        m_solver << "frame,scene_frame,simulation_seconds,body_count,contact_count,pipeline_record_count,checkpoint,"
                    "presentation_hash,solver_hash\n";
    }
}

void ReplayArtifactHashLog::AppendPresentation( const ReplayPresentationSample& sample )
{

    if ( !m_presentation.is_open() )
    {
        return;
    }

    char line[256] = {};
    sprintf_s( line, sizeof( line ), "%llu,%d,%.6f,%llu,%u,%u,%u,0x%016llX\n",
               static_cast<unsigned long long>( sample.frameIndex ), sample.sceneFrame, sample.simulationSeconds,
               static_cast<unsigned long long>( sample.bodies.size() ), static_cast<unsigned>( sample.contactCount ),
               static_cast<unsigned>( sample.pipelineRecordCount ), sample.checkpointBoundary ? 1u : 0u,
               static_cast<unsigned long long>( sample.stateHash ) );

    m_presentation << line;
}

void ReplayArtifactHashLog::AppendSolver( const ReplaySolverFrameSample& sample )
{

    if ( !m_solver.is_open() )
    {
        return;
    }

    char line[288] = {};
    sprintf_s( line, sizeof( line ), "%llu,%d,%.6f,%llu,%u,%u,%u,0x%016llX,0x%016llX\n",
               static_cast<unsigned long long>( sample.frameIndex ), sample.sceneFrame, sample.simulationSeconds,
               static_cast<unsigned long long>( sample.bodies.size() ), static_cast<unsigned>( sample.contactCount ),
               static_cast<unsigned>( sample.pipelineRecordCount ), sample.checkpointBoundary ? 1u : 0u,
               static_cast<unsigned long long>( sample.presentationHash ),
               static_cast<unsigned long long>( sample.solverHash ) );

    m_solver << line;
}

void ReplayArtifactHashLog::Flush()
{

    if ( m_presentation.is_open() )
    {
        m_presentation.flush();
    }

    if ( m_solver.is_open() )
    {
        m_solver.flush();
    }
}
} // namespace SkullbonezCore::Runtime
