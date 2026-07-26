/*
File: SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp
Purpose:
  Writes runtime diagnostic artifacts without making Run own the details.

Summary:
  Diagnostics borrow current runtime context, emit bounded side-channel logs,
  and avoid changing simulation or rendering behavior.

Glossary:
  CSV (Comma-Separated Values): Text table format used for perf and physics
  regression output.
  Perf pass: One validation scene run appended to a perf CSV before the next
    restart or exit.
  SkullScope: Queryable physics diagnostics trace workflow used instead of
  loading raw traces into model context.
  Side-channel log: Artifact written for diagnostics without changing runtime
    behavior.
  Private working set: Resident process pages not shared with other processes;

    matching it requires a page-level OS query.

Invariants:
  - Diagnostics may sample and flush artifacts, but must not mutate simulation
    or render ownership.
  - Private working-set sampling is a deep diagnostics path. UI/render callers
    must stay on cheap process counters.

Related:
  - SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RuntimeDiagnostics.h"
#include "../../Assets/AssetKeys.h"

#include "../../Core/Common.h"
#include "../../Core/Config.h"
#include "../../Core/Log.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Core/Profiler.h"
#include "../Replay/ReplayRecorder.h"
#include "../Scene/SceneRuntime.h"
#include "../../Core/PlatformWin32.h"

#include <array>
#include <cstring>
#include <psapi.h>
#include <string>

namespace SkullbonezCore
{
namespace Runtime
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

void FlushPendingPerfLogWrites( RunPerfLogState& perfLog )
{
    if ( perfLog.perfLogFile && perfLog.perfLogWritesSinceFlush > 0 )
    {
        fflush( perfLog.perfLogFile );
        perfLog.perfLogWritesSinceFlush = 0;
    }
}

bool FlushWorkingSetQueryBatch( HANDLE process,
                                PSAPI_WORKING_SET_EX_INFORMATION* pages,
                                std::size_t pageCount,
                                uint64_t& privateWorkingSetBytes,
                                uint64_t pageSize )
{
    // Hazard: QueryWorkingSetEx can fail for a region without invalidating the
    // whole sample. The caller tracks success separately from the byte count.
    if ( !pages || pageCount == 0 )
    {
        return true;
    }

    const SIZE_T byteCount = pageCount * sizeof( PSAPI_WORKING_SET_EX_INFORMATION );
    const bool queried = QueryWorkingSetEx( process, pages, static_cast<DWORD>( byteCount ) ) != FALSE;
    if ( queried )
    {
        for ( std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex )
        {
            const PSAPI_WORKING_SET_EX_INFORMATION& page = pages[pageIndex];
            if ( page.VirtualAttributes.Valid && !page.VirtualAttributes.Shared )
            {
                privateWorkingSetBytes += pageSize;
            }
        }
    }

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
    std::array<PSAPI_WORKING_SET_EX_INFORMATION, QUERY_BATCH_PAGES> pages = {};

    std::size_t pageCount = 0;

    // Why: VirtualQuery and PSAPI describe process memory with address-shaped
    // Win32 fields. The walk converts them to uintptr_t only for checked range
    // arithmetic, then reconstructs ABI pointers for each synchronous query.
    uintptr_t address = reinterpret_cast<uintptr_t>( systemInfo.lpMinimumApplicationAddress );
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>( systemInfo.lpMaximumApplicationAddress );
    uint64_t privateWorkingSetBytes = 0;
    bool allQueriesSucceeded = true;

    while ( address < maxAddress )
    {
        MEMORY_BASIC_INFORMATION memoryInfo;
        std::memset( &memoryInfo, 0, sizeof( memoryInfo ) );
        const SIZE_T queryBytes = VirtualQuery( reinterpret_cast<const void*>( address ),
                                                &memoryInfo,
                                                sizeof( memoryInfo ) );

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
                pages[pageCount++] = page;
                if ( pageCount >= QUERY_BATCH_PAGES )
                {
                    allQueriesSucceeded = FlushWorkingSetQueryBatch( process,
                                                                     pages.data(),
                                                                     pageCount,
                                                                     privateWorkingSetBytes,
                                                                     pageSize ) &&
                                          allQueriesSucceeded;

                    pageCount = 0;
                }
            }
        }

        address = regionEnd;
    }

    allQueriesSucceeded = FlushWorkingSetQueryBatch( process,
                                                     pages.data(),
                                                     pageCount,
                                                     privateWorkingSetBytes,
                                                     pageSize ) &&
                          allQueriesSucceeded;

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
        FlushPendingPerfLogWrites( perfLog );
        fclose( perfLog.perfLogFile );
        perfLog.perfLogFile = nullptr;
    }
}


void RuntimeDiagnostics::ClosePerfLogWithMemoryCheckpoint( RunPerfLogState& perfLog, int pass, const char* checkpoint )
{
    LogPerfMemory( perfLog, pass, checkpoint );
    ClosePerfLog( perfLog );
}

SkullbonezCore::Core::MainMemoryProcessStats RuntimeDiagnostics::SampleProcessMemory( bool includePrivateWorkingSet )
{
    SkullbonezCore::Core::MainMemoryProcessStats stats;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    std::memset( &pmc, 0, sizeof( pmc ) );
    pmc.cb = sizeof( pmc );
    HANDLE process = GetCurrentProcess();
    // Why: GetProcessMemoryInfo's base-structure ABI accepts the extended
    // structure when cb/size identify PROCESS_MEMORY_COUNTERS_EX.
    if ( GetProcessMemoryInfo( process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>( &pmc ), sizeof( pmc ) ) )
    {
        stats.available = true;
        stats.workingSetBytes = static_cast<uint64_t>( pmc.WorkingSetSize );
        stats.privateCommitBytes = static_cast<uint64_t>( pmc.PrivateUsage );
        stats.pagefileUsageBytes = static_cast<uint64_t>( pmc.PagefileUsage );
        if ( includePrivateWorkingSet && TrySamplePrivateWorkingSetBytes( process, stats.privateWorkingSetBytes ) )
        {
            strcpy_s( stats.taskManagerMetricName, sizeof( stats.taskManagerMetricName ), "private_working_set" );
            stats.taskManagerBytes = stats.privateWorkingSetBytes;
        }
        else if ( includePrivateWorkingSet )
        {
            strcpy_s( stats.taskManagerMetricName, sizeof( stats.taskManagerMetricName ), "working_set_fallback" );
            stats.taskManagerBytes = stats.workingSetBytes;
        }
        else
        {
            // Why: F6 memory UI runs on the render thread. GetProcessMemoryInfo
            // is a bounded counter query, while private working set requires an
            // address-space walk over committed pages and can stall a frame.
            strcpy_s( stats.taskManagerMetricName, sizeof( stats.taskManagerMetricName ), "working_set_fast" );
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

    const SkullbonezCore::Core::MainMemoryProcessStats stats = SampleProcessMemory( true );
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


void RuntimeDiagnostics::ResetPerfLogForSceneLoad( RunPerfLogState& perfLog )
{
    perfLog.isPerfTest = false;
    perfLog.perfHeaderWritten = false;
    perfLog.perfLogPath[0] = '\0';
    perfLog.isPerfLogFlushEnabled = false;
    perfLog.perfLogFlushInterval = 0;
    perfLog.perfLogWritesSinceFlush = 0;
}


void RuntimeDiagnostics::ConfigurePerfLogFlush( RunPerfLogState& perfLog, bool enabled, int interval )
{
    perfLog.isPerfLogFlushEnabled = enabled;
    perfLog.perfLogFlushInterval = interval;
}


void RuntimeDiagnostics::OpenScenePerfLog( RunPerfLogState& perfLog,
                                           const char* path,
                                           int pass,
                                           SkullbonezCore::Core::Profiler* profiler )
{
    if ( !path || path[0] == '\0' )
    {
        return;
    }

    perfLog.isPerfTest = true;
    strcpy_s( perfLog.perfLogPath, sizeof( perfLog.perfLogPath ), path );
    const char* mode = ( pass == 0 ) ? "w" : "a";
    fopen_s( &perfLog.perfLogFile, perfLog.perfLogPath, mode );
    if ( perfLog.perfLogFile )
    {
        perfLog.perfLogWritesSinceFlush = 0;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        // Why: validation appends multiple scene passes in one process. Reset
        // the profiler pass state so the existing warmup skips restart rows
        // before CSV output represents steady-state frame costs.
        if ( profiler )
        {
            profiler->ScheduleReset();
        }
#else
        (void)profiler;
#endif
        LogPerfMemory( perfLog, pass + 1, "start" );
    }
}


bool RuntimeDiagnostics::PerfTestActive( const RunPerfLogState& perfLog )
{
    return perfLog.isPerfTest;
}


RuntimeProfilerFrameTimes RuntimeDiagnostics::SampleProfilerFrameTimes( const SkullbonezCore::Core::Profiler* profiler )
{
    RuntimeProfilerFrameTimes times;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( !profiler )
    {
        return times;
    }

    static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
    static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
    times.physicsTimeSeconds = profiler->LastFrameMsByHash( kPhysicsHash ) * 0.001f;
    times.renderTimeSeconds = profiler->LastFrameMsByHash( kRenderHash ) * 0.001f;
    static constexpr uint32_t kRenderGpuHashes[] = {
        ::HashStr( "Frame/Shadows/ShadowMap" ),
        ::HashStr( "Frame/Render/Skybox" ),
        ::HashStr( "Frame/Render/Reflection" ),
        ::HashStr( "Frame/Render/CinematicSky" ),
        ::HashStr( "Frame/Render/Balls" ),
        ::HashStr( "Frame/Render/Terrain" ),
        ::HashStr( "Frame/Render/Water" ),
        ::HashStr( "Frame/Render/TornadoVisual" ),
        ::HashStr( "Frame/Render/TransparentBalls" ),
        ::HashStr( "Frame/Render/DebugOverlay" ),
        ::HashStr( "Frame/Render/VolumetricLight" ),
        ::HashStr( "Frame/Render/Tonemap" ),
        ::HashStr( "Frame/UI/Draw" ),
    };

    for ( uint32_t h : kRenderGpuHashes )
    {
        times.gpuFrameWorkMs += profiler->LastGpuFrameMsByHash( h );
    }
#else
    (void)profiler;
#endif
    return times;
}


void RuntimeDiagnostics::TickPerfLog( RunPerfLogState& perfLog,
                                      const RuntimePerfTickContext& context,
                                      SkullbonezCore::Core::Profiler* profiler )
{
    if ( !perfLog.isPerfTest || !perfLog.perfLogFile )
    {
        return;
    }

#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( !perfLog.perfHeaderWritten )
    {
        if ( profiler )
        {
            profiler->WritePerfCSVHeader( perfLog.perfLogFile );
        }

        perfLog.perfHeaderWritten = true;
    }

    if ( profiler )
    {
        profiler->WritePerfCSVRow( perfLog.perfLogFile, context.pass, context.frame );
    }
#else
    (void)profiler;
    fprintf( perfLog.perfLogFile,
             "%d,%d,%.4f,%.4f\n",
             context.pass,
             context.frame,
             context.physicsTimeSeconds * 1000.0f,
             context.renderTimeSeconds * 1000.0f );
#endif

    ++perfLog.perfLogWritesSinceFlush;
    FlushPerfLogIfNeeded( perfLog );

    if ( context.frame % 60 == 0 )
    {
        LogPerfMemory( perfLog, context.pass, "periodic" );
    }
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
                                                    Physics::PhysicsEngine& physics,
                                                    const char* path,
                                                    bool fixedStepForcedByDiagnostics )
{
    strcpy_s( diagnostics.path, sizeof( diagnostics.path ), path );
    diagnostics.isEnabled = diagnostics.path[0] != '\0';
    diagnostics.fixedStepForcedByDiagnostics = fixedStepForcedByDiagnostics;
    physics.SetPhysicsDiagnosticsPath( diagnostics.path );
}

void RuntimeDiagnostics::LogSceneFinished( SceneSessionState& scene,
                                           const char* scenePath,
                                           const char* rendererName,
                                           const char* reason )
{
    if ( scene.isFinishLogged )
    {
        return;
    }

    SkullbonezCore::Core::Log().WriteEventf(
        "scene_finished index=%d load=%d path=\"%s\" reason=%s frame=%d target_frames=%d "
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
                                                     Physics::PhysicsEngine& physics,
                                                     const SceneSessionState& scene,
                                                     const SkullbonezCore::Core::EngineConfig& config,
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
    physics.SetPhysicsDiagnosticsRunId( diagnostics.currentRunId );

    const char* solverName = "solver";
    std::string escapedScene = JsonEscape( scenePath && scenePath[0] != '\0' ? scenePath : "generated" );
    std::string escapedRenderer = JsonEscape( rendererName && rendererName[0] != '\0' ? rendererName : "unknown" );
    std::string escapedSolver = JsonEscape( solverName );

    SkullbonezCore::Core::Log().Writef(
        diagnostics.path,
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
        config.worldForces.gravity,
        config.bodySimulation.contactEpsilon,
        config.bodySimulation.contactRestitutionThreshold,
        config.physicsMaterial.frictionCoeff,
        config.physicsMaterial.objectFrictionCoeff,
        config.physicsMaterial.rollingFrictionCoeff,
        config.physicsMaterial.spinFrictionCoeff,
        config.broadphase.cellSize,
        config.persistentContactSolver.slop,
        config.persistentContactSolver.baumgarteBeta,
        config.persistentContactSolver.positionCorrectionPercent,
        config.persistentContactSolver.iterations,
        config.terrainContact.threshold,
        config.terrainContact.slop,
        config.terrainContact.baumgarteBeta,
        config.terrainContact.maxBaumgarteBias,
        config.physicsSleep.linearSpeed,
        config.physicsSleep.angularSpeed,
        config.physicsSleep.frames );
}

void RuntimeDiagnostics::LogReplayScrubProbe( RunPhysicsDiagnosticsState& diagnostics,
                                              const SceneSessionState& scene,
                                              const ReplayScrubProbeDiagnostic& probe )
{
    if ( !diagnostics.isEnabled || !diagnostics.isRunActive )
    {
        return;
    }

    std::string escapedName = JsonEscape( probe.bodyName );
    SkullbonezCore::Core::Log().Writef(
        diagnostics.path,
        "{\"kind\":\"replay_scrub\",\"run\":\"%s\",\"frame\":%d,\"normalized\":%.6f,\"selected_replay_"
        "frame\":%llu,\"live_replay_frame\":%llu,\"selected_scene_frame\":%d,\"live_scene_frame\":%d,"
        "\"selected_state_hash\":%llu,\"live_state_hash\":%llu,\"body_id\":%u,\"model_index\":%d,\"name\":\"%"
        "s\",\"selected_pos\":[%.6f,%.6f,%.6f],\"live_pos\":[%.6f,%.6f,%.6f],\"distance_sq\":%.9f,\"selected_"
        "body_count\":%zu,\"live_body_count\":%zu,\"applied\":%d,\"restored\":%d,\"pre_live_delta_sq\":%.9f,"
        "\"applied_delta_sq\":%.9f,\"restored_delta_sq\":%.9f}\n",
        diagnostics.currentRunId,
        scene.currentFrame,
        probe.normalized,
        static_cast<unsigned long long>( probe.selectedReplayFrame ),
        static_cast<unsigned long long>( probe.liveReplayFrame ),
        probe.selectedSceneFrame,
        probe.liveSceneFrame,
        static_cast<unsigned long long>( probe.selectedStateHash ),
        static_cast<unsigned long long>( probe.liveStateHash ),
        probe.bodyId,
        probe.modelIndex,
        escapedName.c_str(),
        probe.selectedPosition[0],
        probe.selectedPosition[1],
        probe.selectedPosition[2],
        probe.livePosition[0],
        probe.livePosition[1],
        probe.livePosition[2],
        probe.distanceSquared,
        probe.selectedBodyCount,
        probe.liveBodyCount,
        probe.applied ? 1 : 0,
        probe.restored ? 1 : 0,
        probe.preLiveDeltaSquared,
        probe.appliedDeltaSquared,
        probe.restoredDeltaSquared );

    SkullbonezCore::Core::Log().FlushAll();
}

void RuntimeDiagnostics::LogReplayRestoreProbe( RunPhysicsDiagnosticsState& diagnostics,
                                                const SceneSessionState& scene,
                                                const ReplayRestoreProbeDiagnostic& probe )
{
    ReplayRestoreResultDiagnostic result;
    result.restoreSource = "retained_solver";
    result.targetReplayFrame = probe.targetReplayFrame;
    result.targetSceneFrame = probe.targetSceneFrame;
    result.checkpointReplayFrame = probe.checkpointBoundary ? probe.targetReplayFrame : 0;
    result.targetSolverHash = probe.targetSolverHash;
    result.targetPresentationHash = probe.targetPresentationHash;
    result.targetBodyCount = probe.targetBodyCount;
    result.restoredSolverHash = probe.restoredSolverHash;
    result.restoredPresentationHash = probe.restoredPresentationHash;
    result.restoredBodyCount = probe.restoredBodyCount;
    result.contactCount = probe.contactCount;
    result.pipelineRecordCount = probe.pipelineRecordCount;
    result.checkpointBoundary = probe.checkpointBoundary;
    result.hashCaptured = probe.hashCaptured;
    result.hashMatched = probe.hashMatched;
    result.fallbackAttempted = probe.fallbackAttempted;
    result.fallbackRestored = probe.fallbackRestored;
    result.failureReason = probe.hashMatched ? "" : "retained solver restore hash mismatch";
    LogReplayRestoreResult( diagnostics, scene, result );
}

void RuntimeDiagnostics::LogReplayRestoreResult( RunPhysicsDiagnosticsState& diagnostics,
                                                 const SceneSessionState& scene,
                                                 const ReplayRestoreResultDiagnostic& result )
{
    if ( !diagnostics.isEnabled || !diagnostics.isRunActive )
    {
        return;
    }

    std::string escapedSource = JsonEscape( result.restoreSource && result.restoreSource[0] != '\0' ? result.restoreSource : "unknown" );

    std::string escapedReason = JsonEscape( result.failureReason ? result.failureReason : "" );

    SkullbonezCore::Core::Log().Writef(
        diagnostics.path,
        "{\"kind\":\"replay_restore\",\"run\":\"%s\",\"frame\":%d,\"target_replay_frame\":%llu,"
        "\"restore_source\":\"%s\",\"checkpoint_replay_frame\":%llu,"
        "\"target_scene_frame\":%d,\"target_solver_hash\":%llu,\"target_presentation_hash\":%llu,"
        "\"restored_solver_hash\":%llu,\"restored_presentation_hash\":%llu,\"target_body_count\":%zu,"
        "\"restored_body_count\":%zu,\"contact_count\":%u,\"pipeline_record_count\":%u,"
        "\"checkpoint_boundary\":%d,\"hash_captured\":%d,\"hash_matched\":%d,\"fallback_attempted\":%d,"
        "\"fallback_restored\":%d,\"failure_reason\":\"%s\"}\n",
        diagnostics.currentRunId,
        scene.currentFrame,
        static_cast<unsigned long long>( result.targetReplayFrame ),
        escapedSource.c_str(),
        static_cast<unsigned long long>( result.checkpointReplayFrame ),
        result.targetSceneFrame,
        static_cast<unsigned long long>( result.targetSolverHash ),
        static_cast<unsigned long long>( result.targetPresentationHash ),
        static_cast<unsigned long long>( result.restoredSolverHash ),
        static_cast<unsigned long long>( result.restoredPresentationHash ),
        result.targetBodyCount,
        result.restoredBodyCount,
        static_cast<unsigned>( result.contactCount ),
        static_cast<unsigned>( result.pipelineRecordCount ),
        result.checkpointBoundary ? 1 : 0,
        result.hashCaptured ? 1 : 0,
        result.hashMatched ? 1 : 0,
        result.fallbackAttempted ? 1 : 0,
        result.fallbackRestored ? 1 : 0,
        escapedReason.c_str() );

    SkullbonezCore::Core::Log().FlushAll();
}

void RuntimeDiagnostics::EndPhysicsDiagnosticsRun( RunPhysicsDiagnosticsState& diagnostics,
                                                   const SceneSessionState& scene,
                                                   const char* status )
{
    if ( !diagnostics.isEnabled || !diagnostics.isRunActive )
    {
        return;
    }

    std::string escapedStatus = JsonEscape( status && status[0] != '\0' ? status : "ended" );
    SkullbonezCore::Core::Log().Writef( diagnostics.path,
                                        "{\"kind\":\"end\",\"run\":\"%s\",\"frame\":%d,\"status\":\"%s\"}\n",
                                        diagnostics.currentRunId,
                                        scene.currentFrame,
                                        escapedStatus.c_str() );

    SkullbonezCore::Core::Log().FlushAll();

    diagnostics.isRunActive = false;
}
#endif
} // namespace Runtime
} // namespace SkullbonezCore
