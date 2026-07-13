/*
File: TestSceneParserRuntime.cpp
Purpose:
  Parses simulation, tornado, playback, capture, logging, and runtime settings.

Summary:
  This translation unit handles one schema domain while mutating the single
  TestSceneParser result. Shared validation and failure policy live in
  TestSceneParserSchema.h; top-level document order stays in TestSceneParser.cpp.

Glossary:
  Schema domain: Cohesive authored section translated without creating another
    scene owner or intermediate model.
  Lane R: Recoverable invalid-input result accumulated by the active parser.

Invariants:
  - Authored JSON field names remain command-line and scene-file compatibility.
  - Parser failure stops further mutation and is returned without an engine throw.
  - Stable scene identities and source ordering are preserved exactly.

Related:
  - TestSceneParserSchema.h declares shared parser state and helpers.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns this decomposition.
*/
#include "TestSceneParserSchema.h"

namespace SkullbonezCore
{
namespace Basics
{
using TestSceneParserDetail::CopyStringField;
using TestSceneParserDetail::Fail;
using TestSceneParserDetail::FindMember;
using TestSceneParserDetail::Lowercase;
using TestSceneParserDetail::MaxConfigurableWorkerThreadCount;
using TestSceneParserDetail::ReadBool;
using TestSceneParserDetail::ReadFloat;
using TestSceneParserDetail::ReadInt;
using TestSceneParserDetail::ReadString;
using TestSceneParserDetail::ReadUInt;
using TestSceneParserDetail::ReadVec3;
using TestSceneParserDetail::RequireArray;
using TestSceneParserDetail::RequireMember;
using TestSceneParserDetail::RequireObject;

void TestSceneParser::ApplyPlayback( const Json& playback, const std::string& path )
{
    // Concept: playback fields are deterministic-run policy, not presentation
    // hints. Fixed-step and completion settings therefore remain authored scene
    // state and are validated before the runtime sees them.
    RequireObject( playback, path, "playback" );
    if ( const Json* frames = FindMember( playback, "frames" ) )
    {
        if ( frames->is_string() )
        {
            const std::string value = Lowercase( frames->get<std::string>() );
            if ( value != "unlimited" )
            {
                Fail( path, "playback.frames string value must be 'unlimited'" );
            }
            m_scene.m_sceneOptions.frameCount = -1;
        }
        else

        {
            m_scene.m_sceneOptions.frameCount = ReadInt( *frames, path, "playback.frames" );
        }
    }
    if ( const Json* fixedStep = FindMember( playback, "fixedStep" ) )
    {
        m_scene.m_sceneOptions.isFixedStep = ReadBool( *fixedStep, path, "playback.fixedStep" );
    }
    if ( const Json* pauseSnapshotState = FindMember( playback, "pauseSnapshotState" ) )
    {
        m_scene.m_sceneOptions.pauseSnapshotState =
            ReadBool( *pauseSnapshotState, path, "playback.pauseSnapshotState" );
    }
    if ( const Json* exitOnComplete = FindMember( playback, "exitOnComplete" ) )
    {
        m_scene.m_sceneOptions.exitOnComplete = ReadBool( *exitOnComplete, path, "playback.exitOnComplete" );
    }
    if ( const Json* trackHeight = FindMember( playback, "trackHeight" ) )
    {
        const float value = ReadFloat( *trackHeight, path, "playback.trackHeight" );
        if ( value <= 0.0f )
        {
            Fail( path, "playback.trackHeight must be > 0" );
        }
        m_scene.m_sceneOptions.trackHeight = value;
    }
    if ( const Json* autoCycle = FindMember( playback, "autoCycleInterval" ) )
    {
        const float value = ReadFloat( *autoCycle, path, "playback.autoCycleInterval" );
        if ( value <= 0.0f )
        {
            Fail( path, "playback.autoCycleInterval must be > 0" );
        }
        m_scene.m_sceneOptions.autoCycleInterval = value;
    }
}

Physics::MutualGravitySettings TestSceneParser::ReadMutualGravitySettings( const Json& mutualGravity,
                                                                           const std::string& path )
{
    // Invariant: an enabled gravity model must arrive complete and physically
    // bounded; accepting partial values would make defaults machine-dependent.
    RequireObject( mutualGravity, path, "simulation.world.mutualGravity" );
    Physics::MutualGravitySettings settings;
    if ( const Json* enabled = FindMember( mutualGravity, "enabled" ) )
    {
        settings.enabled = ReadBool( *enabled, path, "simulation.world.mutualGravity.enabled" );
    }

    const Json* gravitationalConstant = FindMember( mutualGravity, "gravitationalConstant" );
    if ( gravitationalConstant )
    {
        settings.gravitationalConstant =
            ReadFloat( *gravitationalConstant, path, "simulation.world.mutualGravity.gravitationalConstant" );
    }
    else if ( settings.enabled )
    {
        Fail( path, "simulation.world.mutualGravity.gravitationalConstant is required when enabled" );
    }

    const Json* softeningLength = FindMember( mutualGravity, "softeningLength" );
    if ( softeningLength )
    {
        settings.softeningLength =
            ReadFloat( *softeningLength, path, "simulation.world.mutualGravity.softeningLength" );
    }
    else if ( settings.enabled )
    {
        Fail( path, "simulation.world.mutualGravity.softeningLength is required when enabled" );
    }

    if ( const Json* elasticCollisions = FindMember( mutualGravity, "elasticCollisions" ) )
    {
        settings.elasticCollisions =
            ReadBool( *elasticCollisions, path, "simulation.world.mutualGravity.elasticCollisions" );
    }

    if ( settings.enabled && settings.gravitationalConstant <= 0.0f )
    {
        Fail( path, "simulation.world.mutualGravity.gravitationalConstant must be > 0 when enabled" );
    }
    if ( settings.softeningLength <= 0.0f )
    {
        Fail( path, "simulation.world.mutualGravity.softeningLength must be > 0" );
    }

    return settings;
}

void TestSceneParser::ApplySimulation( const Json& simulation, const std::string& path )
{
    RequireObject( simulation, path, "simulation" );
    if ( const Json* physics = FindMember( simulation, "physics" ) )
    {
        m_scene.m_sceneOptions.isPhysicsEnabled = ReadBool( *physics, path, "simulation.physics" );
    }
    if ( const Json* text = FindMember( simulation, "text" ) )
    {
        m_scene.m_sceneOptions.isTextEnabled = ReadBool( *text, path, "simulation.text" );
    }
    if ( const Json* textOnly = FindMember( simulation, "textOnly" ) )
    {
        m_scene.m_sceneOptions.isTextOnly = ReadBool( *textOnly, path, "simulation.textOnly" );
    }
    if ( const Json* seed = FindMember( simulation, "seed" ) )
    {
        m_scene.m_sceneOptions.seed = ReadUInt( *seed, path, "simulation.seed" );
    }
    if ( const Json* timeScale = FindMember( simulation, "timeScale" ) )
    {
        const float value = ReadFloat( *timeScale, path, "simulation.timeScale" );
        if ( value <= 0.0f )
        {
            Fail( path, "simulation.timeScale must be > 0" );
        }
        m_scene.m_sceneOptions.timeScale = value;
    }
    if ( const Json* solverBalls = FindMember( simulation, "solverBalls" ) )
    {
        const int value = ReadInt( *solverBalls, path, "simulation.solverBalls" );
        if ( value < 0 )
        {
            Fail( path, "simulation.solverBalls must be >= 0" );
        }
        m_scene.m_sceneOptions.solverBallCount = value;
    }
    if ( const Json* solverBoxes = FindMember( simulation, "solverBoxes" ) )
    {
        const int value = ReadInt( *solverBoxes, path, "simulation.solverBoxes" );
        if ( value < 0 )
        {
            Fail( path, "simulation.solverBoxes must be >= 0" );
        }
        m_scene.m_sceneOptions.solverBoxCount = value;
    }
    if ( const Json* modelCapacity = FindMember( simulation, "modelCapacity" ) )
    {
        const int value = ReadInt( *modelCapacity, path, "simulation.modelCapacity" );
        if ( value <= 0 || value > MAX_GAME_MODELS )
        {
            Fail( path, "simulation.modelCapacity is out of range" );
        }
        m_scene.m_sceneOptions.modelCapacity = value;
    }
    if ( const Json* workerThreads = FindMember( simulation, "workerThreads" ) )
    {
        const int value = ReadInt( *workerThreads, path, "simulation.workerThreads" );
        if ( value < -1 || value > MaxConfigurableWorkerThreadCount() )
        {
            Fail( path, "simulation.workerThreads is out of range" );
        }
        m_scene.m_sceneOptions.workerThreads = value;
    }
    if ( const Json* world = FindMember( simulation, "world" ) )
    {
        RequireObject( *world, path, "simulation.world" );
        m_scene.m_worldOverride.hasWorldOverride = true;
        m_scene.m_worldOverride.worldGravity =
            ReadFloat( RequireMember( *world, path, "simulation.world", "gravity" ), path, "simulation.world.gravity" );
        m_scene.m_worldOverride.worldFluidHeight =
            ReadFloat( RequireMember( *world, path, "simulation.world", "fluidHeight" ),
                       path,
                       "simulation.world.fluidHeight" );
        m_scene.m_worldOverride.worldFluidDensity =
            ReadFloat( RequireMember( *world, path, "simulation.world", "fluidDensity" ),
                       path,
                       "simulation.world.fluidDensity" );
        if ( const Json* mutualGravity = FindMember( *world, "mutualGravity" ) )
        {
            m_scene.m_worldOverride.mutualGravity = ReadMutualGravitySettings( *mutualGravity, path );
        }
    }
}

void TestSceneParser::ApplyTornadoFloat( const Json& source,
                                         const std::string& path,
                                         const char* memberName,
                                         float& target,
                                         float minimum )
{
    if ( const Json* value = FindMember( source, memberName ) )
    {
        const std::string context = std::string( "tornadoSystem." ) + memberName;
        target = (std::max)( minimum, ReadFloat( *value, path, context.c_str() ) );
    }
}

void TestSceneParser::ApplyTornadoVortex( const Json& object,
                                          const std::string& path,
                                          Physics::TornadoSystemConfig& system )
{
    RequireObject( object, path, "tornadoSystem.vortices[]" );
    Physics::TornadoVortexConfig vortex;
    vortex.field.enabled = true;
    if ( const Json* center = FindMember( object, "center" ) )
    {
        ReadVec3( *center,
                  path,
                  "tornadoSystem.vortices[].center",
                  vortex.field.center.x,
                  vortex.field.center.y,
                  vortex.field.center.z );
    }
    else
    {
        ReadVec3( RequireMember( object, path, "tornadoSystem.vortices[]", "position" ),
                  path,
                  "tornadoSystem.vortices[].position",
                  vortex.field.center.x,
                  vortex.field.center.y,
                  vortex.field.center.z );
    }

    if ( const Json* enabled = FindMember( object, "enabled" ) )
    {
        vortex.field.enabled = ReadBool( *enabled, path, "tornadoSystem.vortices[].enabled" );
    }
    if ( const Json* spawnTime = FindMember( object, "spawnTime" ) )
    {
        vortex.spawnSeconds = (std::max)( 0.0f, ReadFloat( *spawnTime, path, "tornadoSystem.vortices[].spawnTime" ) );
    }
    if ( const Json* spawnSeconds = FindMember( object, "spawnSeconds" ) )
    {
        vortex.spawnSeconds =
            (std::max)( 0.0f, ReadFloat( *spawnSeconds, path, "tornadoSystem.vortices[].spawnSeconds" ) );
    }
    if ( const Json* ttl = FindMember( object, "ttl" ) )
    {
        vortex.timeToLiveSeconds = (std::max)( 0.0f, ReadFloat( *ttl, path, "tornadoSystem.vortices[].ttl" ) );
    }
    if ( const Json* timeToLive = FindMember( object, "timeToLive" ) )
    {
        vortex.timeToLiveSeconds =
            (std::max)( 0.0f, ReadFloat( *timeToLive, path, "tornadoSystem.vortices[].timeToLive" ) );
    }
    if ( const Json* timeToLiveSeconds = FindMember( object, "timeToLiveSeconds" ) )
    {
        vortex.timeToLiveSeconds =
            (std::max)( 0.0f, ReadFloat( *timeToLiveSeconds, path, "tornadoSystem.vortices[].timeToLiveSeconds" ) );
    }

    ApplyTornadoFloat( object, path, "growSeconds", vortex.growSeconds, 0.0f );
    ApplyTornadoFloat( object, path, "shrinkSeconds", vortex.shrinkSeconds, 0.0f );
    ApplyTornadoFloat( object, path, "driftRadius", vortex.driftRadius, 0.0f );
    ApplyTornadoFloat( object, path, "driftSpeed", vortex.driftSpeed, 0.0f );
    ApplyTornadoFloat( object, path, "driftPhase", vortex.driftPhase, -100000.0f );
    ApplyTornadoFloat( object, path, "repulsionRadius", vortex.repulsionRadius, 0.0f );
    ApplyTornadoFloat( object, path, "repulsionStrength", vortex.repulsionStrength, 0.0f );
    ApplyTornadoFloat( object, path, "radius", vortex.field.radius, 1.0f );
    ApplyTornadoFloat( object, path, "height", vortex.field.height, 1.0f );
    ApplyTornadoFloat( object, path, "inwardAcceleration", vortex.field.inwardAcceleration, 0.0f );
    ApplyTornadoFloat( object, path, "swirlAcceleration", vortex.field.swirlAcceleration, 0.0f );
    ApplyTornadoFloat( object, path, "liftAcceleration", vortex.field.liftAcceleration, 0.0f );
    ApplyTornadoFloat( object, path, "ejectAcceleration", vortex.field.ejectAcceleration, 0.0f );
    ApplyTornadoFloat( object, path, "ejectUpAcceleration", vortex.field.ejectUpAcceleration, 0.0f );
    ApplyTornadoFloat( object, path, "ejectBand", vortex.field.ejectBand, 0.0f );
    ApplyTornadoFloat( object, path, "minCaptureSeconds", vortex.field.minCaptureSeconds, 0.0f );
    ApplyTornadoFloat( object, path, "ejectCooldownSeconds", vortex.field.ejectCooldownSeconds, 0.0f );
    ApplyTornadoFloat( object, path, "maxDeltaVelocity", vortex.field.maxDeltaVelocity, 1.0f );
    if ( const Json* vectors = FindMember( object, "visualizeVelocityField" ) )
    {
        vortex.field.visualizeVelocityField =
            ReadBool( *vectors, path, "tornadoSystem.vortices[].visualizeVelocityField" );
    }

    system.vortices.push_back( vortex );
}

void TestSceneParser::ApplyTornadoSystem( const Json& tornadoSystem, const std::string& path )
{
    RequireObject( tornadoSystem, path, "tornadoSystem" );
    Physics::TornadoSystemConfig system;
    system.enabled = true;
    if ( const Json* enabled = FindMember( tornadoSystem, "enabled" ) )
    {
        system.enabled = ReadBool( *enabled, path, "tornadoSystem.enabled" );
    }
    if ( const Json* vectors = FindMember( tornadoSystem, "visualizeVelocityField" ) )
    {
        system.visualizeVelocityField = ReadBool( *vectors, path, "tornadoSystem.visualizeVelocityField" );
    }

    const Json& vortices = RequireMember( tornadoSystem, path, "tornadoSystem", "vortices" );
    RequireArray( vortices, path, "tornadoSystem.vortices" );
    for ( const Json& vortex : vortices )
    {
        ApplyTornadoVortex( vortex, path, system );
    }
    if ( system.vortices.empty() )
    {
        Fail( path, "tornadoSystem.vortices must not be empty" );
    }

    m_scene.m_tornadoSystem.hasTornadoSystem = true;
    m_scene.m_tornadoSystem.config = system;
}

void TestSceneParser::ApplyRuntime( const Json& runtime, const std::string& path )
{
    RequireObject( runtime, path, "runtime" );
    if ( const Json* vsync = FindMember( runtime, "vsync" ) )
    {
        m_scene.m_runtimeOverrides.hasVsyncOverride = true;
        m_scene.m_runtimeOverrides.isVsyncEnabled = ReadBool( *vsync, path, "runtime.vsync" );
    }
    if ( const Json* pipelineSync = FindMember( runtime, "pipelineSync" ) )
    {
        m_scene.m_runtimeOverrides.hasPipelineSyncOverride = true;
        m_scene.m_runtimeOverrides.isPipelineSyncEnabled = ReadBool( *pipelineSync, path, "runtime.pipelineSync" );
    }
}

void TestSceneParser::ApplyCapture( const Json& capture, const std::string& path )
{
    RequireObject( capture, path, "capture" );
    if ( const Json* screenshot = FindMember( capture, "screenshot" ) )
    {
        RequireObject( *screenshot, path, "capture.screenshot" );
        CopyStringField( m_scene.m_captureOptions.screenshotPath,
                         ReadString( RequireMember( *screenshot, path, "capture.screenshot", "path" ),
                                     path,
                                     "capture.screenshot.path" ) );
        if ( const Json* frame = FindMember( *screenshot, "frame" ) )
        {
            m_scene.m_captureOptions.screenshotFrame = ReadInt( *frame, path, "capture.screenshot.frame" );
            m_scene.m_captureOptions.screenshotMs = -1;
        }
        if ( const Json* ms = FindMember( *screenshot, "ms" ) )
        {
            m_scene.m_captureOptions.screenshotMs = ReadInt( *ms, path, "capture.screenshot.ms" );
            m_scene.m_captureOptions.screenshotFrame = -1;
        }
    }
    if ( const Json* screenshotAndExit = FindMember( capture, "screenshotAndExit" ) )
    {
        m_scene.m_sceneOptions.screenshotAndExit = ReadBool( *screenshotAndExit, path, "capture.screenshotAndExit" );
    }
    if ( const Json* interval = FindMember( capture, "interval" ) )
    {
        RequireObject( *interval, path, "capture.interval" );
        CopyStringField(
            m_scene.m_captureOptions.screenshotDir,
            ReadString( RequireMember( *interval, path, "capture.interval", "dir" ), path, "capture.interval.dir" ) );
        const int frames =
            ReadInt( RequireMember( *interval, path, "capture.interval", "frames" ), path, "capture.interval.frames" );
        if ( frames <= 0 )
        {
            Fail( path, "capture.interval.frames must be > 0" );
        }
        m_scene.m_captureOptions.screenshotInterval = frames;
    }
}

void TestSceneParser::ApplyLogging( const Json& logging, const std::string& path )
{
    RequireObject( logging, path, "logging" );
    if ( const Json* perfLog = FindMember( logging, "perfLog" ) )
    {
        CopyStringField( m_scene.m_loggingOptions.perfLogPath, ReadString( *perfLog, path, "logging.perfLog" ) );
    }
    if ( const Json* flush = FindMember( logging, "perfLogFlush" ) )
    {
        m_scene.m_loggingOptions.isPerfLogFlush = ReadBool( *flush, path, "logging.perfLogFlush" );
    }
    if ( const Json* interval = FindMember( logging, "perfLogFlushInterval" ) )
    {
        m_scene.m_loggingOptions.perfLogFlushInterval = ReadInt( *interval, path, "logging.perfLogFlushInterval" );
    }
}


} // namespace Basics
} // namespace SkullbonezCore
