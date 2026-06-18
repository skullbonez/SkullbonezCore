/*
File: SkullbonezSource/SkullbonezRuntimeDiagnostics.cpp
Purpose:
  Writes runtime diagnostic artifacts without making SkullbonezRun own the details.

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
  - SkullbonezSource/SkullbonezRuntimeDiagnostics.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRuntimeDiagnostics.h"

#include "SkullbonezCommon.h"
#include "SkullbonezConfig.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezSceneRuntime.h"

#include <cstring>
#include <psapi.h>
#include <string>

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

void RuntimeDiagnostics::LogPerfMemory( RunPerfLogState& perfLog, int pass, const char* checkpoint )
{
    if ( !perfLog.perfLogFile )
    {
        return;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof( pmc );
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    {
        double mb = static_cast<double>( pmc.WorkingSetSize ) / ( 1024.0 * 1024.0 );
        fprintf( perfLog.perfLogFile, "# MEM %s pass=%d working_set_mb=%.2f\n", checkpoint, pass, mb );
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

    Log().WriteEventf( "scene_finished index=%d load=%d path=\"%s\" reason=%s frame=%d target_frames=%d renderer=\"%s\" models=%d test_complete=%d",
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
                                                     const SkullbonezConfig& config,
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
                  "{\"kind\":\"run\",\"run\":\"%s\",\"scene\":\"%s\",\"scene_index\":%d,\"load_count\":%d,\"manual_reset_count\":%d,\"renderer\":\"%s\",\"solver\":\"%s\",\"seed\":%u,\"fixed_step\":%d,\"fixed_step_forced_by_diag\":%d,\"target_frames\":%d,\"model_count\":%d,\"config\":{\"gravity\":%.6f,\"contact_epsilon\":%.6f,\"contact_restitution_threshold\":%.6f,\"friction_coeff\":%.6f,\"rolling_friction_coeff\":%.6f,\"spin_friction_coeff\":%.6f,\"broadphase_cell\":%.6f,\"persistent_contact_slop\":%.6f,\"persistent_contact_baumgarte_beta\":%.6f,\"persistent_contact_position_correction_percent\":%.6f,\"persistent_contact_solver_iterations\":%d,\"terrain_contact_threshold\":%.6f,\"terrain_contact_slop\":%.6f,\"terrain_contact_baumgarte_beta\":%.6f,\"terrain_max_baumgarte_bias\":%.6f,\"physics_sleep_linear_speed\":%.6f,\"physics_sleep_angular_speed\":%.6f,\"physics_sleep_frames\":%d}}\n",
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
