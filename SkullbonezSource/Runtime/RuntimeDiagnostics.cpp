/*
File: SkullbonezSource/Runtime/RuntimeDiagnostics.cpp
Purpose:
  Writes runtime diagnostic artifacts without making Run own the details.

Mental model:
  Diagnostics borrow current runtime context, emit bounded side-channel logs,
  and avoid changing simulation or rendering behavior.

Glossary:
  CSV (Comma-Separated Values): Text table format used for perf and physics
  regression output.
  SkullScope: Queryable physics diagnostics trace workflow used instead of
  loading raw traces into model context.
  Side-channel log: Artifact written for diagnostics without changing runtime
  behavior.

Related:
  - SkullbonezSource/Runtime/RuntimeDiagnostics.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RuntimeDiagnostics.h"

#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../GameObjects/GameModelCollection.h"
#include "../Core/Profiler.h"
#include "Replay/ReplayRecorder.h"
#include "Scene/SceneRuntime.h"

#include <cstring>
#include <psapi.h>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
void FlushPerfLogIfNeeded( RunPerfLogState& perfLog )
{
    if ( perfLog.isPerfLogFlushEnabled ||
         ( perfLog.perfLogFlushInterval > 0 && perfLog.perfLogWritesSinceFlush >= perfLog.perfLogFlushInterval ) )
    {
        fflush( perfLog.perfLogFile );
        perfLog.perfLogWritesSinceFlush = 0;
    }
}

bool FlushWorkingSetQueryBatch( HANDLE process,
                                std::vector<PSAPI_WORKING_SET_EX_INFORMATION>& pages,
                                uint64_t& privateWorkingSetBytes,
                                uint64_t pageSize )
{
    if ( pages.empty() )
    {
        return true;
    }

    const SIZE_T byteCount = pages.size() * sizeof( PSAPI_WORKING_SET_EX_INFORMATION );
    const bool queried = QueryWorkingSetEx( process, pages.data(), static_cast<DWORD>( byteCount ) ) != FALSE;
    if ( queried )
    {
        for ( const PSAPI_WORKING_SET_EX_INFORMATION& page : pages )
        {
            if ( page.VirtualAttributes.Valid && !page.VirtualAttributes.Shared )
            {
                privateWorkingSetBytes += pageSize;
            }
        }
    }
    pages.clear();
    return queried;
}

bool TrySamplePrivateWorkingSetBytes( HANDLE process, uint64_t& outBytes )
{
    SYSTEM_INFO systemInfo;
    GetNativeSystemInfo( &systemInfo );
    const uint64_t pageSize = static_cast<uint64_t>( systemInfo.dwPageSize );
    if ( pageSize == 0 )
    {
        return false;
    }

    constexpr std::size_t QUERY_BATCH_PAGES = 4096;
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> pages;
    pages.reserve( QUERY_BATCH_PAGES );

    uintptr_t address = reinterpret_cast<uintptr_t>( systemInfo.lpMinimumApplicationAddress );
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>( systemInfo.lpMaximumApplicationAddress );
    uint64_t privateWorkingSetBytes = 0;
    bool allQueriesSucceeded = true;

    while ( address < maxAddress )
    {
        MEMORY_BASIC_INFORMATION memoryInfo;
        std::memset( &memoryInfo, 0, sizeof( memoryInfo ) );
        const SIZE_T queryBytes =
            VirtualQuery( reinterpret_cast<const void*>( address ), &memoryInfo, sizeof( memoryInfo ) );
        if ( queryBytes == 0 )
        {
            address += static_cast<uintptr_t>( pageSize );
            continue;
        }

        const uintptr_t regionBase = reinterpret_cast<uintptr_t>( memoryInfo.BaseAddress );
        const uintptr_t regionSize = static_cast<uintptr_t>( memoryInfo.RegionSize );
        const uintptr_t regionEnd = regionBase + regionSize;
        if ( regionEnd <= address || regionEnd < regionBase )
        {
            break;
        }

        const bool queryable = memoryInfo.State == MEM_COMMIT && ( memoryInfo.Protect & PAGE_GUARD ) == 0 &&
                               ( memoryInfo.Protect & PAGE_NOACCESS ) == 0;
        if ( queryable )
        {
            uintptr_t pageAddress = regionBase;
            const uintptr_t pageMask = static_cast<uintptr_t>( pageSize - 1 );
            if ( ( pageAddress & pageMask ) != 0 )
            {
                pageAddress = ( pageAddress + pageMask ) & ~pageMask;
            }
            for ( ; pageAddress < regionEnd; pageAddress += static_cast<uintptr_t>( pageSize ) )
            {
                PSAPI_WORKING_SET_EX_INFORMATION page = {};
                page.VirtualAddress = reinterpret_cast<void*>( pageAddress );
                pages.push_back( page );
                if ( pages.size() >= QUERY_BATCH_PAGES )
                {
                    allQueriesSucceeded =
                        FlushWorkingSetQueryBatch( process, pages, privateWorkingSetBytes, pageSize ) &&
                        allQueriesSucceeded;
                }
            }
        }

        address = regionEnd;
    }

    allQueriesSucceeded =
        FlushWorkingSetQueryBatch( process, pages, privateWorkingSetBytes, pageSize ) && allQueriesSucceeded;
    outBytes = privateWorkingSetBytes;
    return allQueriesSucceeded;
}

#ifdef _DEBUG
std::string JsonEscape( const char* value )
{
    std::string escaped;
    if ( !value )
    {
        return escaped;
    }

    for ( const char* p = value; *p != '\0'; ++p )
    {
        switch ( *p )
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += *p;
            break;
        }
    }
    return escaped;
}
#endif
} // namespace

void RuntimeDiagnostics::ClosePerfLog( RunPerfLogState& perfLog )
{
    if ( perfLog.perfLogFile )
    {
        fclose( perfLog.perfLogFile );
        perfLog.perfLogFile = nullptr;
    }
}

MainMemoryProcessStats RuntimeDiagnostics::SampleProcessMemory()
{
    MainMemoryProcessStats stats;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    std::memset( &pmc, 0, sizeof( pmc ) );
    pmc.cb = sizeof( pmc );
    HANDLE process = GetCurrentProcess();
    if ( GetProcessMemoryInfo( process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>( &pmc ), sizeof( pmc ) ) )
    {
        stats.available = true;
        stats.workingSetBytes = static_cast<uint64_t>( pmc.WorkingSetSize );
        stats.privateCommitBytes = static_cast<uint64_t>( pmc.PrivateUsage );
        stats.pagefileUsageBytes = static_cast<uint64_t>( pmc.PagefileUsage );
        if ( TrySamplePrivateWorkingSetBytes( process, stats.privateWorkingSetBytes ) )
        {
            strcpy_s( stats.taskManagerMetricName, sizeof( stats.taskManagerMetricName ), "private_working_set" );
            stats.taskManagerBytes = stats.privateWorkingSetBytes;
        }
        else
        {
            strcpy_s( stats.taskManagerMetricName, sizeof( stats.taskManagerMetricName ), "working_set_fallback" );
            stats.taskManagerBytes = stats.workingSetBytes;
        }
    }

    return stats;
}

void RuntimeDiagnostics::LogPerfMemory( RunPerfLogState& perfLog, int pass, const char* checkpoint )
{
    if ( !perfLog.perfLogFile )
    {
        return;
    }

    const MainMemoryProcessStats stats = SampleProcessMemory();
    if ( stats.available )
    {
        const double taskManagerMb = static_cast<double>( stats.taskManagerBytes ) / ( 1024.0 * 1024.0 );
        const double workingSetMb = static_cast<double>( stats.workingSetBytes ) / ( 1024.0 * 1024.0 );
        const double privateWorkingSetMb = static_cast<double>( stats.privateWorkingSetBytes ) / ( 1024.0 * 1024.0 );
        const double privateCommitMb = static_cast<double>( stats.privateCommitBytes ) / ( 1024.0 * 1024.0 );
        const double pagefileMb = static_cast<double>( stats.pagefileUsageBytes ) / ( 1024.0 * 1024.0 );
        fprintf( perfLog.perfLogFile,
                 "# MEM %s pass=%d task_manager_metric=%s task_manager_mb=%.2f working_set_mb=%.2f "
                 "private_working_set_mb=%.2f private_commit_mb=%.2f pagefile_mb=%.2f\n",
                 checkpoint,
                 pass,
                 stats.taskManagerMetricName,
                 taskManagerMb,
                 workingSetMb,
                 privateWorkingSetMb,
                 privateCommitMb,
                 pagefileMb );
        ++perfLog.perfLogWritesSinceFlush;
        FlushPerfLogIfNeeded( perfLog );
    }
}

void RuntimeDiagnostics::TickPerfLog( RunPerfLogState& perfLog, const RuntimePerfTickContext& context )
{
    if ( !perfLog.isPerfTest || !perfLog.perfLogFile )
    {
        return;
    }

#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( !perfLog.perfHeaderWritten )
    {
        Profiler::Instance().WritePerfCSVHeader( perfLog.perfLogFile );
        perfLog.perfHeaderWritten = true;
    }
    Profiler::Instance().WritePerfCSVRow( perfLog.perfLogFile, context.pass, context.frame );
#else
    fprintf( perfLog.perfLogFile,
             "%d,%d,%.4f,%.4f\n",
             context.pass,
             context.frame,
             context.physicsTimeSeconds * 1000.0f,
             context.renderTimeSeconds * 1000.0f );
#endif

    ++perfLog.perfLogWritesSinceFlush;
    FlushPerfLogIfNeeded( perfLog );
}

#ifdef _DEBUG
void RuntimeDiagnostics::SetPhysicsRegressionLogOverride( RunPerfLogState& perfLog, const char* path )
{
    strcpy_s( perfLog.physicsRegressionLogOverride, sizeof( perfLog.physicsRegressionLogOverride ), path );
}

void RuntimeDiagnostics::SetPhysicsCollisionTimeLogOverride( RunPerfLogState& perfLog, const char* path )
{
    strcpy_s( perfLog.physicsCollisionTimeLogOverride, sizeof( perfLog.physicsCollisionTimeLogOverride ), path );
}

void RuntimeDiagnostics::SetPhysicsDiagnosticsPath( RunPhysicsDiagnosticsState& diagnostics,
                                                    GameObjects::GameModelCollection& models,
                                                    const char* path,
                                                    bool fixedStepForcedByDiagnostics )
{
    strcpy_s( diagnostics.path, sizeof( diagnostics.path ), path );
    diagnostics.isEnabled = diagnostics.path[0] != '\0';
    diagnostics.fixedStepForcedByDiagnostics = fixedStepForcedByDiagnostics;
    models.SetPhysicsDiagnosticsPath( diagnostics.path );
}

void RuntimeDiagnostics::LogSceneFinished( RunSceneState& scene,
                                           const char* scenePath,
                                           const char* rendererName,
                                           const char* reason )
{
    if ( scene.isFinishLogged )
    {
        return;
    }

    Log().WriteEventf( "scene_finished index=%d load=%d path=\"%s\" reason=%s frame=%d target_frames=%d "
                       "renderer=\"%s\" models=%d test_complete=%d",
                       scene.currentSceneIndex,
                       scene.loadCount,
                       scenePath && scenePath[0] != '\0' ? scenePath : "generated",
                       reason && reason[0] != '\0' ? reason : "unknown",
                       scene.currentFrame,
                       scene.targetFrameCount,
                       rendererName && rendererName[0] != '\0' ? rendererName : "unknown",
                       scene.modelCount,
                       scene.isTestComplete ? 1 : 0 );

    scene.isFinishLogged = true;
}

void RuntimeDiagnostics::BeginPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics,
                                                     GameObjects::GameModelCollection& models,
                                                     const RunSceneState& scene,
                                                     const EngineConfig& config,
                                                     const char* scenePath,
                                                     const char* rendererName )
{
    if ( !diagnostics.isEnabled )
    {
        return;
    }

    ++diagnostics.runSequence;
    sprintf_s( diagnostics.currentRunId, sizeof( diagnostics.currentRunId ), "run_%04d", diagnostics.runSequence );
    diagnostics.isRunActive = true;
    models.SetPhysicsDiagnosticsRunId( diagnostics.currentRunId );

    const char* solverName = "solver";
    std::string escapedScene = JsonEscape( scenePath && scenePath[0] != '\0' ? scenePath : "generated" );
    std::string escapedRenderer = JsonEscape( rendererName && rendererName[0] != '\0' ? rendererName : "unknown" );
    std::string escapedSolver = JsonEscape( solverName );

    Log().Writef( diagnostics.path,
                  "{\"kind\":\"run\",\"run\":\"%s\",\"scene\":\"%s\",\"scene_index\":%d,\"load_count\":%d,\"manual_"
                  "reset_count\":%d,\"renderer\":\"%s\",\"solver\":\"%s\",\"seed\":%u,\"fixed_step\":%d,\"fixed_step_"
                  "forced_by_diag\":%d,\"target_frames\":%d,\"model_count\":%d,\"config\":{\"gravity\":%.6f,\"contact_"
                  "epsilon\":%.6f,\"contact_restitution_threshold\":%.6f,\"friction_coeff\":%.6f,\"object_friction_"
                  "coeff\":%.6f,\"rolling_friction_coeff\":%.6f,\"spin_friction_coeff\":%.6f,\"broadphase_cell\":%.6f,"
                  "\"persistent_contact_slop\":%.6f,"
                  "\"persistent_contact_baumgarte_beta\":%.6f,\"persistent_contact_position_correction_percent\":%.6f,"
                  "\"persistent_contact_solver_iterations\":%d,\"terrain_contact_threshold\":%.6f,\"terrain_contact_"
                  "slop\":%.6f,\"terrain_contact_baumgarte_beta\":%.6f,\"terrain_max_baumgarte_bias\":%.6f,\"physics_"
                  "sleep_linear_speed\":%.6f,\"physics_sleep_angular_speed\":%.6f,\"physics_sleep_frames\":%d}}\n",
                  diagnostics.currentRunId,
                  escapedScene.c_str(),
                  scene.currentSceneIndex,
                  scene.loadCount,
                  scene.manualResetCount,
                  escapedRenderer.c_str(),
                  escapedSolver.c_str(),
                  scene.rngSeed,
                  scene.isFixedStep ? 1 : 0,
                  diagnostics.fixedStepForcedByDiagnostics ? 1 : 0,
                  scene.targetFrameCount,
                  scene.modelCount,
                  config.gravity,
                  config.contactEpsilon,
                  config.contactRestitutionThreshold,
                  config.frictionCoeff,
                  config.objectFrictionCoeff,
                  config.rollingFrictionCoeff,
                  config.spinFrictionCoeff,
                  config.broadphaseCell,
                  config.persistentContactSlop,
                  config.persistentContactBaumgarteBeta,
                  config.persistentContactPositionCorrectionPercent,
                  config.persistentContactSolverIterations,
                  config.terrainContactThreshold,
                  config.terrainContactSlop,
                  config.terrainContactBaumgarteBeta,
                  config.terrainMaxBaumgarteBias,
                  config.physicsSleepLinearSpeed,
                  config.physicsSleepAngularSpeed,
                  config.physicsSleepFrames );
}

void RuntimeDiagnostics::LogReplayScrubProbe( RunPhysicsDiagnosticsState& diagnostics,
                                              const RunSceneState& scene,
                                              const ReplayPresentationSample& selected,
                                              const ReplayPresentationSample& live,
                                              const ReplayBodyPresentationSample& selectedBody,
                                              const ReplayBodyPresentationSample& liveBody,
                                              float normalized,
                                              float distanceSquared,
                                              bool applied,
                                              bool restored,
                                              float preLiveDeltaSquared,
                                              float appliedDeltaSquared,
                                              float restoredDeltaSquared )
{
    if ( !diagnostics.isEnabled || !diagnostics.isRunActive )
    {
        return;
    }

    std::string escapedName = JsonEscape( selectedBody.name );
    Log().Writef( diagnostics.path,
                  "{\"kind\":\"replay_scrub\",\"run\":\"%s\",\"frame\":%d,\"normalized\":%.6f,\"selected_replay_"
                  "frame\":%llu,\"live_replay_frame\":%llu,\"selected_scene_frame\":%d,\"live_scene_frame\":%d,"
                  "\"selected_state_hash\":%llu,\"live_state_hash\":%llu,\"body_id\":%u,\"model_index\":%d,\"name\":\"%"
                  "s\",\"selected_pos\":[%.6f,%.6f,%.6f],\"live_pos\":[%.6f,%.6f,%.6f],\"distance_sq\":%.9f,\"selected_"
                  "body_count\":%zu,\"live_body_count\":%zu,\"applied\":%d,\"restored\":%d,\"pre_live_delta_sq\":%.9f,"
                  "\"applied_delta_sq\":%.9f,\"restored_delta_sq\":%.9f}\n",
                  diagnostics.currentRunId,
                  scene.currentFrame,
                  normalized,
                  static_cast<unsigned long long>( selected.frameIndex ),
                  static_cast<unsigned long long>( live.frameIndex ),
                  selected.sceneFrame,
                  live.sceneFrame,
                  static_cast<unsigned long long>( selected.stateHash ),
                  static_cast<unsigned long long>( live.stateHash ),
                  selectedBody.id.value,
                  liveBody.modelIndex,
                  escapedName.c_str(),
                  selectedBody.position.x,
                  selectedBody.position.y,
                  selectedBody.position.z,
                  liveBody.position.x,
                  liveBody.position.y,
                  liveBody.position.z,
                  distanceSquared,
                  selected.bodies.size(),
                  live.bodies.size(),
                  applied ? 1 : 0,
                  restored ? 1 : 0,
                  preLiveDeltaSquared,
                  appliedDeltaSquared,
                  restoredDeltaSquared );
    Log().FlushAll();
}

void RuntimeDiagnostics::LogReplayRestoreProbe( RunPhysicsDiagnosticsState& diagnostics,
                                                const RunSceneState& scene,
                                                const ReplaySolverFrameSample& selected,
                                                uint64_t restoredSolverHash,
                                                uint64_t restoredPresentationHash,
                                                std::size_t restoredBodyCount,
                                                bool hashCaptured,
                                                bool hashMatched,
                                                bool fallbackAttempted,
                                                bool fallbackRestored )
{
    LogReplayRestoreResult( diagnostics,
                            scene,
                            "retained_solver",
                            selected.frameIndex,
                            selected.sceneFrame,
                            selected.checkpointBoundary ? selected.frameIndex : 0,
                            selected.solverHash,
                            selected.presentationHash,
                            selected.bodies.size(),
                            restoredSolverHash,
                            restoredPresentationHash,
                            restoredBodyCount,
                            selected.contactCount,
                            selected.pipelineRecordCount,
                            selected.checkpointBoundary,
                            hashCaptured,
                            hashMatched,
                            fallbackAttempted,
                            fallbackRestored,
                            hashMatched ? "" : "retained solver restore hash mismatch" );
}

void RuntimeDiagnostics::LogReplayRestoreResult( RunPhysicsDiagnosticsState& diagnostics,
                                                 const RunSceneState& scene,
                                                 const char* restoreSource,
                                                 uint64_t targetReplayFrame,
                                                 int targetSceneFrame,
                                                 uint64_t checkpointReplayFrame,
                                                 uint64_t targetSolverHash,
                                                 uint64_t targetPresentationHash,
                                                 std::size_t targetBodyCount,
                                                 uint64_t restoredSolverHash,
                                                 uint64_t restoredPresentationHash,
                                                 std::size_t restoredBodyCount,
                                                 uint16_t contactCount,
                                                 uint16_t pipelineRecordCount,
                                                 bool checkpointBoundary,
                                                 bool hashCaptured,
                                                 bool hashMatched,
                                                 bool fallbackAttempted,
                                                 bool fallbackRestored,
                                                 const char* failureReason )
{
    if ( !diagnostics.isEnabled || !diagnostics.isRunActive )
    {
        return;
    }

    std::string escapedSource = JsonEscape( restoreSource && restoreSource[0] != '\0' ? restoreSource : "unknown" );
    std::string escapedReason = JsonEscape( failureReason ? failureReason : "" );

    Log().Writef( diagnostics.path,
                  "{\"kind\":\"replay_restore\",\"run\":\"%s\",\"frame\":%d,\"target_replay_frame\":%llu,"
                  "\"restore_source\":\"%s\",\"checkpoint_replay_frame\":%llu,"
                  "\"target_scene_frame\":%d,\"target_solver_hash\":%llu,\"target_presentation_hash\":%llu,"
                  "\"restored_solver_hash\":%llu,\"restored_presentation_hash\":%llu,\"target_body_count\":%zu,"
                  "\"restored_body_count\":%zu,\"contact_count\":%u,\"pipeline_record_count\":%u,"
                  "\"checkpoint_boundary\":%d,\"hash_captured\":%d,\"hash_matched\":%d,\"fallback_attempted\":%d,"
                  "\"fallback_restored\":%d,\"failure_reason\":\"%s\"}\n",
                  diagnostics.currentRunId,
                  scene.currentFrame,
                  static_cast<unsigned long long>( targetReplayFrame ),
                  escapedSource.c_str(),
                  static_cast<unsigned long long>( checkpointReplayFrame ),
                  targetSceneFrame,
                  static_cast<unsigned long long>( targetSolverHash ),
                  static_cast<unsigned long long>( targetPresentationHash ),
                  static_cast<unsigned long long>( restoredSolverHash ),
                  static_cast<unsigned long long>( restoredPresentationHash ),
                  targetBodyCount,
                  restoredBodyCount,
                  static_cast<unsigned>( contactCount ),
                  static_cast<unsigned>( pipelineRecordCount ),
                  checkpointBoundary ? 1 : 0,
                  hashCaptured ? 1 : 0,
                  hashMatched ? 1 : 0,
                  fallbackAttempted ? 1 : 0,
                  fallbackRestored ? 1 : 0,
                  escapedReason.c_str() );
    Log().FlushAll();
}

void RuntimeDiagnostics::EndPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics,
                                                   const RunSceneState& scene,
                                                   const char* status )
{
    if ( !diagnostics.isEnabled || !diagnostics.isRunActive )
    {
        return;
    }

    std::string escapedStatus = JsonEscape( status && status[0] != '\0' ? status : "ended" );
    Log().Writef( diagnostics.path,
                  "{\"kind\":\"end\",\"run\":\"%s\",\"frame\":%d,\"status\":\"%s\"}\n",
                  diagnostics.currentRunId,
                  scene.currentFrame,
                  escapedStatus.c_str() );
    Log().FlushAll();

    diagnostics.isRunActive = false;
}
#endif
} // namespace Basics
} // namespace SkullbonezCore
