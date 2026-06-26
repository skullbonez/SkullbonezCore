/*
File: SkullbonezSource/Runtime/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"

#include "CaptureSystem.h"
#include "Replay/ReplayV2Artifact.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SUPPORTED =
    REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

float ReplayEventFloatFromBits( int32_t signedBits )
{
    uint32_t bits = 0;
    float value = 0.0f;
    static_assert( sizeof( bits ) == sizeof( signedBits ), "Replay event float bits must be 32-bit." );
    static_assert( sizeof( bits ) == sizeof( value ), "Replay event float payloads assume 32-bit floats." );
    std::memcpy( &bits, &signedBits, sizeof( bits ) );
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}

int ReplayHexNibble( char value )
{
    if ( value >= '0' && value <= '9' )
    {
        return value - '0';
    }
    if ( value >= 'a' && value <= 'f' )
    {
        return value - 'a' + 10;
    }
    if ( value >= 'A' && value <= 'F' )
    {
        return value - 'A' + 10;
    }
    return -1;
}

bool ReadReplayHexFloat( const char*& cursor, float& outValue )
{
    uint32_t bits = 0;
    for ( int i = 0; i < 8; ++i )
    {
        const int nibble = ReplayHexNibble( cursor[i] );
        if ( nibble < 0 )
        {
            return false;
        }
        bits = ( bits << 4 ) | static_cast<uint32_t>( nibble );
    }
    cursor += 8;
    std::memcpy( &outValue, &bits, sizeof( outValue ) );
    return true;
}

bool DecodeReplayRay9Payload( const ReplayEventSample& event,
                              Vector3& outOrigin,
                              Vector3& outDirection,
                              Vector3& outCameraUp )
{
    constexpr char prefix[] = "ray9:";
    if ( std::strncmp( event.text, prefix, sizeof( prefix ) - 1 ) != 0 )
    {
        return false;
    }

    const char* cursor = event.text + sizeof( prefix ) - 1;
    float values[9] = {};
    for ( float& value : values )
    {
        if ( !ReadReplayHexFloat( cursor, value ) )
        {
            return false;
        }
    }

    outOrigin = Vector3( values[0], values[1], values[2] );
    outDirection = Vector3( values[3], values[4], values[5] );
    outCameraUp = Vector3( values[6], values[7], values[8] );
    return true;
}

bool DecodeReplayPlacePayload( const ReplayEventSample& event,
                               Vector3& outTerrainPoint,
                               Vector3& outPlacementScale,
                               float& outPlacementYawRadians )
{
    constexpr char prefix6[] = "place6:";
    constexpr char prefix7[] = "place7:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
    }
    else if ( std::strncmp( event.text, prefix6, sizeof( prefix6 ) - 1 ) == 0 )
    {
        valueCount = 6;
        cursor = event.text + sizeof( prefix6 ) - 1;
    }
    else
    {
        return false;
    }

    float values[7] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outTerrainPoint = Vector3( values[0], values[1], values[2] );
    outPlacementScale = Vector3( values[3], values[4], values[5] );
    outPlacementYawRadians = valueCount >= 7 ? values[6] : 0.0f;
    return true;
}


bool DecodeReplayTransformPayload( const ReplayEventSample& event,
                                   Vector3& outPosition,
                                   Quaternion& outOrientation,
                                   float& outScaleFactor,
                                   bool& outHasScaleFactor )
{
    constexpr char prefix7[] = "xform7:";
    constexpr char prefix8[] = "xform8:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
        outHasScaleFactor = false;
    }
    else if ( std::strncmp( event.text, prefix8, sizeof( prefix8 ) - 1 ) == 0 )
    {
        valueCount = 8;
        cursor = event.text + sizeof( prefix8 ) - 1;
        outHasScaleFactor = true;
    }
    else
    {
        return false;
    }

    float values[8] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outPosition = Vector3( values[0], values[1], values[2] );
    outOrientation = Quaternion( values[3], values[4], values[5], values[6] );
    outOrientation.Normalise();
    outScaleFactor = outHasScaleFactor ? values[7] : 1.0f;
    return true;
}


const ReplayV2SolverHashSample* FindReplaySolverHashForFrame( const std::vector<ReplayV2SolverHashSample>& hashes,
                                                              ReplayFrameIndex frameIndex )
{
    for ( const ReplayV2SolverHashSample& hash : hashes )
    {
        if ( hash.frameIndex == frameIndex )
        {
            return &hash;
        }
    }
    return nullptr;
}

const ReplayPresentationSample* FindReplayPresentationForFrame( const std::vector<ReplayPresentationSample>& samples,
                                                                ReplayFrameIndex frameIndex )
{
    for ( const ReplayPresentationSample& sample : samples )
    {
        if ( sample.frameIndex == frameIndex )
        {
            return &sample;
        }
    }
    return nullptr;
}

void WriteReplayProbeReason( char* outReason, std::size_t reasonSize, const char* reason )
{
    if ( outReason && reasonSize > 0 )
    {
        sprintf_s( outReason, reasonSize, "%s", reason ? reason : "event replay failed" );
    }
}

class ScopedReplayProbeProfilerFrame
{
  public:
    ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_BEGIN();
    }
    ~ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_END();
    }
};
} // namespace

void Run::Execute()
{
    MSG msg;

    for ( ;; )
    {
        if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();
            m_timers.workTimer.StartTimer();
            Gfx().ResetFrameDrawCalls();

            PROFILE_BEGIN( "Frame/Input" );
            TickInteractionAutomationBeforeInput();
            TakeInput();
            TickLiveStyleControl();
            PROFILE_END( "Frame/Input" );

            m_cGameModelCollection.BeginCollisionVisualFrame();
            TickPhysics( secondsPerFrame );

            PROFILE_BEGIN( "Frame/PostPhysics" );

            PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
            // Update broadphase visualizer state (runs even when overlay is hidden so fades are correct)
            {
                m_broadphaseVisualizer.SetEnabled( m_debug.isBroadphaseOverlay );
                m_broadphaseVisualizer.SetCellSize( m_cGameModelCollection.GetSpatialGrid().GetCellSize() );
                const SpatialGrid& grid = m_cGameModelCollection.GetSpatialGrid();
                SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
                int activeCellCount = grid.GetActiveCellCount();
                grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
                const std::vector<int64_t>& collisionKeys = m_cGameModelCollection.GetCollisionCellKeys();
                m_broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ),
                                               activeCellBuf,
                                               activeCellCount,
                                               collisionKeys.data(),
                                               static_cast<int>( collisionKeys.size() ) );
                UpdateRequiredSceneBroadphaseXCells( activeCellBuf,
                                                     (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
            }
            PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
            m_collisionVisualizer.SetEnabled( m_debug.isCollisionVisualizer );
            m_collisionVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
            m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
            m_physicsDebugVisualizer.SetContactLingerSeconds( m_debug.physicsDebugContactLinger );
            m_physicsDebugVisualizer.SetPipelineStageCursor( m_debug.physicsDebugPipelineStageCursor );
            m_physicsDebugVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            UpdateRequiredSceneContacts();
            PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
            m_cGameModelCollection.EndCollisionVisualFrame();
            PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );

            PROFILE_END( "Frame/PostPhysics" );

            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                PROFILE_BEGIN( "Frame/PipelineSync" );
                Gfx().Finish();
                PROFILE_END( "Frame/PipelineSync" );
            }

            PROFILE_BEGIN( "Frame/Render" );
            {
                DRAW_CALL_TRACE_SCOPE( "Frame/Render" );
                Render();
            }
            PROFILE_END( "Frame/Render" );

            if ( m_renderer.ShouldRenderUiText() )
            {
                const int uiDrawCallStart = Gfx().GetFrameDrawCallCount();
                PROFILE_BEGIN( "Frame/UI" );
                {
                    DRAW_CALL_TRACE_SCOPE( "Frame/UI" );
                    m_renderer.RenderUiText( secondsPerFrame );
                }
                PROFILE_END( "Frame/UI" );
                const int uiDrawCallEnd = Gfx().GetFrameDrawCallCount();
                m_timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
            }
            else
            {
                m_timers.lastUIDrawCalls = 0;
            }

            PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
            TickLiveStyleControlCapture();
            PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

            PROFILE_BEGIN( "Frame/PostDraw/InteractionAutomation" );
            TickInteractionAutomationAfterRender();
            PROFILE_END( "Frame/PostDraw/InteractionAutomation" );

            if ( TickScreenshots() )
            {
                continue;
            }

            PROFILE_BEGIN( "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( "Frame/VsyncWait" );
            Gfx().Present();
            PROFILE_END( "Frame/VsyncWait" );

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END();

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                using SkullbonezCore::Basics::Profiler;
                static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
                static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
                m_timers.physicsTime = Profiler::Instance().LastFrameMsByHash( kPhysicsHash ) * 0.001f;
                m_timers.renderTime = Profiler::Instance().LastFrameMsByHash( kRenderHash ) * 0.001f;
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
                float gpuMs = 0.0f;
                for ( uint32_t h : kRenderGpuHashes )
                {
                    gpuMs += Profiler::Instance().LastGpuFrameMsByHash( h );
                }
                m_timers.gpuFrameWorkMs = gpuMs;
            }
#endif

            TickPerfLog();

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
}


void Run::TickPhysics( double secondsPerFrame )
{
    if ( IsReplayScrubPaused() )
    {
        PROFILE_SCOPED( "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ) );
        return;
    }

    const bool replayLiveAdvanceHeld = m_replayRuntime.Scrubber().liveAdvanceHeld;
    const bool stepRequested = Input::IsKeyDown( VK_SPACE );
    const bool replayCapture = m_replayRuntime.IsCaptureEnabled();
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    const RuntimeInteractionFramePolicy policy =
        m_interaction.BuildFramePolicy( RuntimeInteractionFrameInput{ SceneState().isScenePhysics,
                                                                      stepRequested,
                                                                      false,
                                                                      replayLiveAdvanceHeld,
                                                                      Input::IsRightMouseDown(),
                                                                      m_runtimeTools.Editor().viewportLookActive,
                                                                      ReplayInspectionMouseLookActive(),
                                                                      physicsCapture,
                                                                      SceneState().timeScale } );
    const bool manipulatorPhysics = policy.manipulatorActive;
    const SimulationTickResult tick = m_simulation.Tick(
        SimulationTickInput{ secondsPerFrame,
                             policy.physicsTimeScale,
                             SceneState().isSceneMode,
                             SceneState().isScenePhysics,
                             SceneState().isFixedStep,
                             policy.physicsAdvance,
                             stepRequested,
                             &m_cGameModelCollection,
                             manipulatorPhysics ? &Run::ApplyMousePickupPhysicsStepThunk : nullptr,
                             this,
                             ( manipulatorPhysics || replayCapture ) ? &Run::AfterPhysicsStepThunk : nullptr,
                             this } );
    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );
    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt );
    }
}


void Run::CaptureReplayPhysicsStepThunk( void* userData )
{
    Run* run = static_cast<Run*>( userData );
    if ( run )
    {
        run->CaptureReplayPhysicsStep();
    }
}


void Run::AfterPhysicsStepThunk( void* userData )
{
    Run* run = static_cast<Run*>( userData );
    if ( run )
    {
        run->AfterPhysicsStep();
    }
}


void Run::ApplyMousePickupPhysicsStepThunk( void* userData )
{
    Run* run = static_cast<Run*>( userData );
    if ( run )
    {
        run->ApplyMousePickupPhysicsStep();
    }
}


void Run::AfterPhysicsStep()
{
    RestoreMousePickupAngularVelocity();
    if ( m_replayRuntime.IsCaptureEnabled() )
    {
        CaptureReplayPhysicsStep();
    }
}


void Run::CaptureReplayPhysicsStep()
{
    {
        PROFILE_SCOPED( "Frame/Physics/Step/ReplayCapture" );
        ReplayLauncherVisualSample launcherVisual;
        BuildReplayLauncherVisualSample( launcherVisual );

        ReplayCaptureInput input;
        input.sceneFrame = SceneState().currentFrame;
        input.simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
        input.physicsDt = PHYSICS_FIXED_DT;
        input.fixedStep = SceneState().isFixedStep;
        input.scenePhysicsEnabled = SceneState().isScenePhysics;
        input.sceneTextEnabled = SceneState().isSceneText;
        input.waterHidden = m_debug.isWaterHidden;
        input.terrainHidden = m_debug.isTerrainHidden;
        input.cameras = m_systems.cameras;
        input.world = &m_cWorldEnvironment;
        input.models = &m_cGameModelCollection;
        input.launcherVisual = &launcherVisual;
        m_replayRuntime.CaptureFrame( input );
        CompareLatestReplaySamples();
    }
#ifdef _DEBUG
    TickReplayScrubProbe();
    TickReplayRestoreProbe();
    TickReplaySaveProbe();
#endif
}


void Run::BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const
{
    outSample = ReplayLauncherVisualSample();
    outSample.nextRayLine = m_runtimeTools.RayCastTest().nextLine;
    outSample.fireMode = m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile
                             ? ReplayLauncherFireMode::Projectile
                             : ReplayLauncherFireMode::Laser;
    outSample.visualizeRays = m_runtimeTools.RayCastTest().visualizeRays;
    outSample.impulseStrength = m_runtimeTools.RayCastTest().impulseStrength;
    outSample.projectileSpeed = m_runtimeTools.RayCastTest().projectileSpeed;
    outSample.rayLines.reserve( RunRayCastTestState::MAX_LINES );
    for ( const RunRayCastTestLine& line : m_runtimeTools.RayCastTest().lines )
    {
        ReplayRayCastLineSample sample;
        sample.start = line.start;
        sample.end = line.end;
        sample.ageSeconds = line.ageSeconds;
        sample.active = line.active;
        sample.hit = line.hit;
        outSample.rayLines.push_back( sample );
    }
    m_runtimeTools.Laser().CaptureShots( outSample.laserShots, outSample.nextLaserShot );
}

bool Run::ApplyReplayEventForRestoreTarget( const ReplayEventSample& event, char* outReason, std::size_t reasonSize )
{
    if ( event.payloadVersion != 1 )
    {
        WriteReplayProbeReason( outReason, reasonSize, "unsupported replay event payload version" );
        return false;
    }

    switch ( event.kind )
    {
    case ReplayEventKind::TimelineStart:
        WriteReplayProbeReason( outReason, reasonSize, "ignored" );
        return true;
    case ReplayEventKind::RuntimeCommand:
    {
        const RuntimeCommandType commandType = static_cast<RuntimeCommandType>( event.value0 );
        switch ( commandType )
        {
        case RuntimeCommandType::SaveScreenshot:
        case RuntimeCommandType::SaveSceneDefaults:
        case RuntimeCommandType::SaveRenderDefaults:
        case RuntimeCommandType::SaveSkyDefaults:
        case RuntimeCommandType::Quit:
        case RuntimeCommandType::None:
            WriteReplayProbeReason( outReason, reasonSize, "ignored non-solver runtime command" );
            return true;
        case RuntimeCommandType::ResetCurrentScene:
        case RuntimeCommandType::LoadSceneIndex:
        case RuntimeCommandType::LoadDemoScene:
        case RuntimeCommandType::CreateScene:
        case RuntimeCommandType::AdvanceScene:
        default:
            WriteReplayProbeReason( outReason, reasonSize, "unsupported runtime timeline mutation event" );
            return false;
        }
    }
    case ReplayEventKind::BranchRestore:
        WriteReplayProbeReason( outReason, reasonSize, "unsupported timeline mutation event" );
        return false;
    case ReplayEventKind::WorldOverride:
        if ( event.flags & REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED )
        {
            m_cWorldEnvironment.SetGravity( ReplayEventFloatFromBits( event.value0 ) );
        }
        if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED )
        {
            m_cWorldEnvironment.SetFluidSurfaceHeight( ReplayEventFloatFromBits( event.value1 ) );
        }
        if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED )
        {
            m_cWorldEnvironment.SetFluidDensity( ReplayEventFloatFromBits( event.value2 ) );
        }
        WriteReplayProbeReason( outReason, reasonSize, "applied world override" );
        return true;
    case ReplayEventKind::LauncherConfig:
        m_runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value0 );
        m_runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value1 );
        WriteReplayProbeReason( outReason, reasonSize, "applied launcher config" );
        return true;
    case ReplayEventKind::LauncherFire:
    {
        Vector3 rayOrigin;
        Vector3 rayDirection;
        Vector3 cameraUp;
        if ( !DecodeReplayRay9Payload( event, rayOrigin, rayDirection, cameraUp ) )
        {
            WriteReplayProbeReason( outReason, reasonSize, "invalid launcher fire payload" );
            return false;
        }
        m_runtimeTools.RayCastTest().fireMode = ( event.flags & REPLAY_LAUNCHER_FIRE_PROJECTILE ) != 0
                                                    ? RunLauncherFireMode::Projectile
                                                    : RunLauncherFireMode::Laser;
        m_runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value1 );
        m_runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value2 );
        if ( m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile )
        {
            FireLauncherProjectile( rayOrigin, rayDirection, cameraUp );
        }
        else
        {
            FireLauncherLaser( rayOrigin, rayDirection, cameraUp );
        }
        WriteReplayProbeReason( outReason, reasonSize, "applied launcher fire" );
        return true;
    }
    case ReplayEventKind::GeneratedSceneConfig:
        if ( SceneState().modelCount != event.value0 || SceneState().solverBallCount != event.value1 ||
             SceneState().solverBoxCount != event.value2 ||
             static_cast<int32_t>( SceneState().rngSeed ) != event.value3 )
        {
            WriteReplayProbeReason( outReason, reasonSize, "generated scene config event does not match live state" );
            return false;
        }
        WriteReplayProbeReason( outReason, reasonSize, "verified generated scene config" );
        return true;
    case ReplayEventKind::EditorPlace:
    {
        Vector3 terrainPoint;
        Vector3 placementScale;
        float placementYawRadians = 0.0f;
        if ( !DecodeReplayPlacePayload( event, terrainPoint, placementScale, placementYawRadians ) )
        {
            WriteReplayProbeReason( outReason, reasonSize, "invalid editor placement payload" );
            return false;
        }

        const int modelCountBefore = m_cGameModelCollection.GetModelCount();
        if ( event.value3 != modelCountBefore )
        {
            WriteReplayProbeReason( outReason, reasonSize, "editor placement model count precondition mismatch" );
            return false;
        }

        const Vector3 previousPlacementScale = m_runtimeTools.Editor().placementScale;
        const bool previousTerrainAlign = m_runtimeTools.Editor().autoTerrainAlign;
        const float previousPlacementYawRadians = m_runtimeTools.Editor().placementYawRadians;
        m_runtimeTools.Editor().placementScale = placementScale;
        m_runtimeTools.Editor().autoTerrainAlign = ( event.flags & REPLAY_EDITOR_PLACE_TERRAIN_ALIGN ) != 0;
        m_runtimeTools.Editor().placementYawRadians = placementYawRadians;
        const bool placed = PlaceEditorObjectAtTerrainPoint( event.value0,
                                                             ( event.flags & REPLAY_EDITOR_PLACE_FIXED ) != 0,
                                                             terrainPoint,
                                                             false );
        m_runtimeTools.Editor().placementScale = previousPlacementScale;
        m_runtimeTools.Editor().autoTerrainAlign = previousTerrainAlign;
        m_runtimeTools.Editor().placementYawRadians = previousPlacementYawRadians;
        if ( !placed )
        {
            WriteReplayProbeReason( outReason, reasonSize, "failed to replay editor placement" );
            return false;
        }
        WriteReplayProbeReason( outReason, reasonSize, "applied editor placement" );
        return true;
    }
    case ReplayEventKind::EditorTransform:
    {
        if ( event.flags == 0 || ( event.flags & ~REPLAY_EDITOR_TRANSFORM_SUPPORTED ) != 0 )
        {
            WriteReplayProbeReason( outReason, reasonSize, "unsupported editor transform flags" );
            return false;
        }

        Vector3 position;
        Quaternion orientation;
        float scaleFactor = 1.0f;
        bool hasScaleFactor = false;
        if ( !DecodeReplayTransformPayload( event, position, orientation, scaleFactor, hasScaleFactor ) )
        {
            WriteReplayProbeReason( outReason, reasonSize, "invalid editor transform payload" );
            return false;
        }
        if ( ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE ) != 0 &&
             ( !hasScaleFactor || event.value3 < 0 || event.value3 > 2 || !std::isfinite( scaleFactor ) ||
               scaleFactor <= 0.0f ) )
        {
            WriteReplayProbeReason( outReason, reasonSize, "invalid editor transform scale payload" );
            return false;
        }

        if ( event.value2 != m_cGameModelCollection.GetModelCount() )
        {
            WriteReplayProbeReason( outReason, reasonSize, "editor transform model count precondition mismatch" );
            return false;
        }
        if ( event.value0 < 0 || event.value0 >= m_cGameModelCollection.GetModelCount() )
        {
            WriteReplayProbeReason( outReason, reasonSize, "editor transform model index is out of range" );
            return false;
        }

        GameModel& model = m_cGameModelCollection.GetModelAtIndex( event.value0 );
        if ( model.GetReplayBodyId() != static_cast<uint32_t>( event.value1 ) )
        {
            WriteReplayProbeReason( outReason, reasonSize, "editor transform replay body id mismatch" );
            return false;
        }

        if ( event.flags & REPLAY_EDITOR_TRANSFORM_TRANSLATE )
        {
            model.SetPosition( position );
        }
        if ( event.flags & REPLAY_EDITOR_TRANSFORM_ROTATE )
        {
            model.SetOrientation( orientation );
        }
        if ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE )
        {
            const CollisionShape baseShape = model.GetCollisionShape();
            if ( !model.ScaleCollisionShapeAxisFromBase( baseShape, event.value3, scaleFactor ) )
            {
                WriteReplayProbeReason( outReason, reasonSize, "failed to replay editor transform scale" );
                return false;
            }
        }
        model.SetLinearVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
        model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
        if ( !model.IsFixed() )
        {
            m_cGameModelCollection.GetPhysicsEngine().WakeBody( m_cGameModelCollection, event.value0 );
        }
        m_cGameModelCollection.InvalidatePhysicsStreams();
        WriteReplayProbeReason( outReason, reasonSize, "applied editor transform" );
        return true;
    }
    default:
        WriteReplayProbeReason( outReason, reasonSize, "unsupported replay event kind" );
        return false;
    }
}


void Run::CompareLatestReplaySamples()
{
    const ReplayPresentationSample* presentation = m_replayRuntime.Presentation().LatestSample();
    const ReplaySolverFrameSample* solver = m_replayRuntime.Solver().LatestSample();
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

    if ( m_solverReplayMismatch.reports < 8 )
    {
        ++m_solverReplayMismatch.reports;
        fprintf( stderr,
                 "[replay] Solver/presentation capture mismatch #%u: presentation_frame=%llu solver_frame=%llu "
                 "presentation_hash=0x%016llX solver_presentation_hash=0x%016llX solver_hash=0x%016llX "
                 "presentation_bodies=%llu solver_bodies=%llu\n",
                 m_solverReplayMismatch.reports,
                 static_cast<unsigned long long>( presentation->frameIndex ),
                 static_cast<unsigned long long>( solver->frameIndex ),
                 static_cast<unsigned long long>( presentation->stateHash ),
                 static_cast<unsigned long long>( solver->presentationHash ),
                 static_cast<unsigned long long>( solver->solverHash ),
                 static_cast<unsigned long long>( presentation->bodies.size() ),
                 static_cast<unsigned long long>( solver->bodies.size() ) );
    }
    else if ( !m_solverReplayMismatch.suppressed )
    {
        m_solverReplayMismatch.suppressed = true;
        fprintf( stderr,
                 "[replay] Further solver/presentation capture mismatch diagnostics suppressed for this replay "
                 "timeline.\n" );
    }
}


#ifdef _DEBUG
void Run::TickReplayScrubProbe()
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayScrubProbe.enabled || m_replayScrubProbe.completed )
    {
        return;
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayScrubProbe.minSampleCount ) )
    {
        return;
    }

    const ReplayPresentationSample* selected =
        m_replayRuntime.Presentation().SampleAtNormalized( m_replayScrubProbe.normalized );
    const ReplayPresentationSample* live = m_replayRuntime.Presentation().LatestSample();
    if ( !selected || !live || selected->frameIndex >= live->frameIndex )
    {
        throw std::runtime_error( "replay scrub probe could not select an older replay sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        if ( candidate.fixed )
        {
            continue;
        }

        for ( const ReplayBodyPresentationSample& liveCandidate : live->bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }

    if ( !selectedBody || !liveBody || bestDistanceSquared < m_replayScrubProbe.minDistanceSquared )
    {
        throw std::runtime_error( "replay scrub probe did not find a moved body in the selected replay window" );
    }

    std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = m_cGameModelCollection.PhysicsModels();
    if ( liveBody->modelIndex < 0 || liveBody->modelIndex >= static_cast<int>( physicsModels.size() ) )
    {
        throw std::runtime_error( "replay scrub probe selected an invalid live model index" );
    }

    SkullbonezCore::GameObjects::GameModel& probedModel =
        physicsModels[static_cast<std::size_t>( liveBody->modelIndex )];
    const Math::Vector::Vector3 preApplyPosition = probedModel.GetPosition();
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > m_replayScrubProbe.minDistanceSquared )
    {
        throw std::runtime_error(
            "replay scrub probe live model did not match the current replay sample before applying scrub state" );
    }

    const bool applied = m_replayRuntime.ApplyPresentationSampleForRender( m_cGameModelCollection, *selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay scrub probe failed to apply the selected presentation sample" );
    }
    const Math::Vector::Vector3 appliedPosition = probedModel.GetPosition();
    const float appliedDeltaSquared = distanceSquared( appliedPosition, selectedBody->position );
    if ( appliedDeltaSquared > m_replayScrubProbe.minDistanceSquared )
    {
        m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
        throw std::runtime_error( "replay scrub probe did not move the live model to the selected replay sample" );
    }

    m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
    const Math::Vector::Vector3 restoredPosition = probedModel.GetPosition();
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    const bool restored = restoredDeltaSquared <= m_replayScrubProbe.minDistanceSquared;
    if ( !restored )
    {
        throw std::runtime_error(
            "replay scrub probe did not restore the live model after applying the selected sample" );
    }

    m_diagnosticsRuntime.LogReplayScrubProbe( SceneState(),
                                              *selected,
                                              *live,
                                              *selectedBody,
                                              *liveBody,
                                              m_replayScrubProbe.normalized,
                                              bestDistanceSquared,
                                              applied,
                                              restored,
                                              preLiveDeltaSquared,
                                              appliedDeltaSquared,
                                              restoredDeltaSquared );

    m_replayScrubProbe.completed = true;
    printf(
        "[replay] Scrub probe passed: selected_replay_frame=%llu live_replay_frame=%llu body_id=%u distance_sq=%.6f\n",
        static_cast<unsigned long long>( selected->frameIndex ),
        static_cast<unsigned long long>( live->frameIndex ),
        selectedBody->id.value,
        bestDistanceSquared );
    PostQuitMessage( 0 );
}

void Run::TickReplayRestoreProbe()
{
    if ( !m_replayRestoreProbe.enabled || m_replayRestoreProbe.completed )
    {
        return;
    }

    const ReplayRecorderStats stats = m_replayRuntime.Solver().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayRestoreProbe.minSampleCount ) )
    {
        return;
    }

    const ReplaySolverFrameSample* selectedSample =
        m_replayRuntime.Solver().SampleAtNormalized( m_replayRestoreProbe.normalized );
    const ReplaySolverFrameSample* latestSample = m_replayRuntime.Solver().LatestSample();
    if ( !selectedSample || !latestSample )
    {
        throw std::runtime_error( "replay restore probe could not select retained solver samples" );
    }
    if ( selectedSample->frameIndex >= latestSample->frameIndex )
    {
        throw std::runtime_error( "replay restore probe did not select an older solver sample" );
    }

    const ReplaySolverFrameSample selected = *selectedSample;
    const ReplayFrameIndex latestFrame = latestSample->frameIndex;
    const uint64_t selectedHash = selected.solverHash;
    char reason[160] = {};
    const bool restored = RestoreReplaySolverSampleAsLive( selected, reason, sizeof( reason ) );
    if ( !restored )
    {
        char message[224] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    m_replayRestoreProbe.completed = true;
    printf( "[replay] Restore probe passed: target_replay_frame=%llu previous_live_replay_frame=%llu "
            "solver_hash=0x%016llX\n",
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( latestFrame ),
            static_cast<unsigned long long>( selectedHash ) );
    PostQuitMessage( 0 );
}

void Run::TickReplaySaveProbe()
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replaySaveProbe.enabled || m_replaySaveProbe.completed )
    {
        return;
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( !m_replaySaveProbe.runtimeResetCoverageInjected && stats.sampleCount >= 4 )
    {
        m_replaySaveProbe.runtimeResetCoverageInjected = true;
        m_replaySaveProbe.eventCoverageInjected = false;
        m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        return;
    }

    if ( !m_replaySaveProbe.eventCoverageInjected && stats.sampleCount >= 4 )
    {
        m_replaySaveProbe.eventCoverageInjected = true;
        const float currentGravity = m_cWorldEnvironment.GetGravity();
        const float probeGravity = currentGravity != 0.0f ? currentGravity * 0.95f : -0.25f;
        ApplyUIWorldOverride( probeGravity,
                              m_cWorldEnvironment.GetFluidSurfaceHeight(),
                              m_cWorldEnvironment.GetFluidDensity() );
        m_runtimeTools.Editor().placementScale = Vector3( 2.0f, 2.0f, 2.0f );
        m_runtimeTools.Editor().autoTerrainAlign = false;
        const int modelCountBeforePlace = m_cGameModelCollection.GetModelCount();
        if ( PlaceEditorObjectAtTerrainPoint( UI::EditorTab::OBJECT_BOX, true, Vector3( 18.0f, 0.0f, 18.0f ) ) )
        {
            GameModel& placedModel = m_cGameModelCollection.GetModelAtIndex( modelCountBeforePlace );
            placedModel.SetPosition( placedModel.GetPosition() + Vector3( 4.0f, 0.0f, 0.0f ) );
            Quaternion placedOrientation = placedModel.GetOrientation();
            placedOrientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f );
            placedModel.SetOrientation( placedOrientation );
            const CollisionShape placedShapeBeforeScale = placedModel.GetCollisionShape();
            constexpr int PROBE_SCALE_AXIS = 0;
            constexpr float PROBE_SCALE_FACTOR = 1.5f;
            if ( !placedModel.ScaleCollisionShapeAxisFromBase( placedShapeBeforeScale,
                                                               PROBE_SCALE_AXIS,
                                                               PROBE_SCALE_FACTOR ) )
            {
                throw std::runtime_error( "replay save probe failed to apply editor transform scale" );
            }
            placedModel.SetLinearVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
            placedModel.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
            m_cGameModelCollection.InvalidatePhysicsStreams();
            RecordReplayEditorTransformEvent(
                modelCountBeforePlace,
                REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE,
                placedModel,
                PROBE_SCALE_AXIS,
                PROBE_SCALE_FACTOR );
        }
        m_runtimeTools.RayCastTest().projectileSpeed += 1.0f;
        RecordReplayLauncherConfigEvent( 2u );
        FireRayCastTest();
    }
    if ( stats.sampleCount < static_cast<std::size_t>( m_replaySaveProbe.minSampleCount ) )
    {
        return;
    }

    ReplayV2SaveResult result;
    if ( !m_replayRuntime.SavePresentationWithSolverHashes( m_replaySaveProbe.path, &result ) )
    {
        throw std::runtime_error( "replay save probe failed to write v2 presentation artifact" );
    }
    if ( result.solverHashCount < result.sampleCount )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without a full solver hash track" );
    }
    if ( result.solverCheckpointCount == 0 )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without solver checkpoint chunks" );
    }
    if ( result.eventCount == 0 )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without event chunks" );
    }
    if ( result.eventCursorCount == 0 )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without checkpoint event cursors" );
    }

    std::vector<ReplayPresentationSample> loadedSamples;
    ReplayV2LoadResult loadResult;
    if ( !ReplayV2Artifact::LoadPresentation( m_replaySaveProbe.path, loadedSamples, &loadResult ) )
    {
        throw std::runtime_error( "replay save probe failed to reload v2 presentation artifact" );
    }
    if ( loadedSamples.size() < 2 )
    {
        throw std::runtime_error( "replay save probe loaded too few v2 presentation samples" );
    }

    const std::size_t selectedIndex = (std::min)( loadedSamples.size() / 4, loadedSamples.size() - 2 );
    const ReplayPresentationSample& selected = loadedSamples[selectedIndex];
    const ReplayPresentationSample& live = loadedSamples.back();
    if ( selected.frameIndex >= live.frameIndex )
    {
        throw std::runtime_error( "replay save probe could not seek to an older loaded v2 sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected.bodies )
    {
        for ( const ReplayBodyPresentationSample& liveCandidate : live.bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !liveBody || bestDistanceSquared < 0.0001f )
    {
        throw std::runtime_error( "replay save probe did not find a moved body in the loaded v2 artifact" );
    }

    std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = m_cGameModelCollection.PhysicsModels();
    if ( liveBody->modelIndex < 0 || liveBody->modelIndex >= static_cast<int>( physicsModels.size() ) )
    {
        throw std::runtime_error( "replay save probe loaded an invalid live model index" );
    }

    SkullbonezCore::GameObjects::GameModel& probedModel =
        physicsModels[static_cast<std::size_t>( liveBody->modelIndex )];
    const Math::Vector::Vector3 preApplyPosition = probedModel.GetPosition();
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay save probe live model did not match the loaded v2 live sample" );
    }

    const bool applied = m_replayRuntime.ApplyPresentationSampleForRender( m_cGameModelCollection, selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay save probe failed to apply the loaded v2 presentation sample" );
    }
    const Math::Vector::Vector3 appliedPosition = probedModel.GetPosition();
    const float appliedDeltaSquared = distanceSquared( appliedPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
        throw std::runtime_error( "replay save probe did not move the live model to the loaded v2 sample" );
    }

    m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
    const Math::Vector::Vector3 restoredPosition = probedModel.GetPosition();
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay save probe did not restore after applying the loaded v2 sample" );
    }

    m_replaySaveProbe.completed = true;
    printf( "[replay] Save probe wrote: path=%s samples=%llu bodies=%llu solver_hashes=%llu "
            "solver_checkpoints=%llu events=%llu event_cursors=%llu bytes=%llu\n",
            m_replaySaveProbe.path,
            static_cast<unsigned long long>( result.sampleCount ),
            static_cast<unsigned long long>( result.bodyDictionaryCount ),
            static_cast<unsigned long long>( result.solverHashCount ),
            static_cast<unsigned long long>( result.solverCheckpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.eventCursorCount ),
            static_cast<unsigned long long>( result.fileBytes ) );
    printf( "[replay] Save probe loaded: samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            static_cast<unsigned long long>( loadResult.sampleCount ),
            static_cast<unsigned long long>( loadResult.bodyDictionaryCount ),
            static_cast<unsigned long long>( loadResult.firstFrame ),
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( live.frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
    PostQuitMessage( 0 );
}

void Run::VerifyLoadedReplayPresentationProbe( float normalized )
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !HasLoadedReplayPresentation() )
    {
        throw std::runtime_error( "replay load probe requires a loaded v2 presentation artifact" );
    }

    ArmLoadedReplayPresentationScrubber( std::clamp( normalized, 0.0f, 1.0f ) );
    const ReplayPresentationSample* selected = CurrentReplayScrubSample();
    const ReplayPresentationSample* latest = LoadedReplayPresentationLatestSample();
    if ( !selected || !latest )
    {
        throw std::runtime_error( "replay load probe could not select a loaded presentation sample" );
    }
    if ( selected->frameIndex >= latest->frameIndex )
    {
        throw std::runtime_error( "replay load probe did not select an older v2 presentation sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* latestBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        for ( const ReplayBodyPresentationSample& latestCandidate : latest->bodies )
        {
            if ( latestCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( latestCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                latestBody = &latestCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !latestBody || bestDistanceSquared < 0.0001f )
    {
        throw std::runtime_error( "replay load probe did not find a moved body in the loaded v2 artifact" );
    }

    std::vector<SkullbonezCore::GameObjects::GameModel>& physicsModels = m_cGameModelCollection.PhysicsModels();
    if ( selectedBody->modelIndex < 0 || selectedBody->modelIndex >= static_cast<int>( physicsModels.size() ) )
    {
        throw std::runtime_error( "replay load probe loaded an invalid model index" );
    }

    SkullbonezCore::GameObjects::GameModel& probedModel =
        physicsModels[static_cast<std::size_t>( selectedBody->modelIndex )];
    const Math::Vector::Vector3 preApplyPosition = probedModel.GetPosition();
    const bool applied = m_replayRuntime.ApplyPresentationSampleForRender( m_cGameModelCollection, *selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay load probe failed to apply the selected loaded v2 sample" );
    }

    const Math::Vector::Vector3 appliedPosition = probedModel.GetPosition();
    const float appliedDeltaSquared = distanceSquared( appliedPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
        throw std::runtime_error( "replay load probe did not move the model to the selected loaded v2 sample" );
    }

    m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
    const Math::Vector::Vector3 restoredPosition = probedModel.GetPosition();
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay load probe did not restore after applying the selected loaded v2 sample" );
    }

    printf( "[replay] Load probe passed: path=%s samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            m_replayRuntime.LoadedPresentation().path,
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( selected->frameIndex ),
            static_cast<unsigned long long>( latest->frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
}

void Run::VerifyReplaySolverCheckpointFileProbe( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        throw std::runtime_error( "replay restore file probe requires a v2 artifact path" );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult result;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &result ) )
    {
        throw std::runtime_error( "replay restore file probe failed to load v2 solver checkpoints" );
    }
    if ( checkpoints.empty() )
    {
        throw std::runtime_error( "replay restore file probe found no v2 solver checkpoints" );
    }

    const ReplaySolverFrameSample& checkpoint = checkpoints.front();
    if ( checkpoint.eventCursor == 0 )
    {
        throw std::runtime_error( "replay restore file probe loaded a checkpoint without an event cursor" );
    }
    char reason[160] = {};
    if ( !RestoreReplaySolverSampleAsLive( checkpoint, reason, sizeof( reason ) ) )
    {
        char message[256] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore file probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    printf( "[replay] Restore file probe passed: path=%s checkpoints=%llu first_frame=%llu target_frame=%llu "
            "event_cursor=%u bodies=%llu solver_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.firstFrame ),
            static_cast<unsigned long long>( checkpoint.frameIndex ),
            checkpoint.eventCursor,
            static_cast<unsigned long long>( checkpoint.bodies.size() ),
            static_cast<unsigned long long>( checkpoint.solverHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
}
#endif

bool Run::RestoreReplayV2ArtifactTargetState( const char* path,
                                              ReplayFrameIndex requestedFrame,
                                              bool makeLiveBranch,
                                              RunReplayV2TargetRestoreResult& outResult,
                                              char* outReason,
                                              std::size_t reasonSize )
{
    outResult = RunReplayV2TargetRestoreResult();
    auto writeReason = [outReason, reasonSize]( const char* reason )
    { WriteReplayProbeReason( outReason, reasonSize, reason ); };
    constexpr ReplayFrameIndex LATEST_NON_CHECKPOINT_TARGET = ( std::numeric_limits<ReplayFrameIndex>::max )();
    const char* restoreSource = makeLiveBranch ? "v2_file_branch" : "v2_file_target";
#ifndef _DEBUG
    (void)restoreSource;
#endif
    const ReplayV2SolverHashSample* target = nullptr;
    const ReplaySolverFrameSample* checkpoint = nullptr;

    auto logRestoreDiagnostic = [&]( const char* failureReason,
                                     const ReplayV2SolverHashSample* diagnosticTarget,
                                     const ReplaySolverFrameSample* diagnosticCheckpoint,
                                     uint64_t restoredSolverHash,
                                     uint64_t restoredPresentationHash,
                                     std::size_t restoredBodyCount,
                                     bool hashCaptured,
                                     bool hashMatched,
                                     bool fallbackAttempted,
                                     bool fallbackRestored )
    {
#ifdef _DEBUG
        const ReplayFrameIndex targetFrame =
            diagnosticTarget ? diagnosticTarget->frameIndex
                             : ( requestedFrame == LATEST_NON_CHECKPOINT_TARGET ? 0 : requestedFrame );
        m_diagnosticsRuntime.LogReplayRestoreResult(
            SceneState(),
            restoreSource,
            targetFrame,
            diagnosticTarget ? diagnosticTarget->sceneFrame : SceneState().currentFrame,
            diagnosticCheckpoint ? diagnosticCheckpoint->frameIndex : 0,
            diagnosticTarget ? diagnosticTarget->solverHash : 0,
            diagnosticTarget ? diagnosticTarget->presentationHash : 0,
            diagnosticTarget ? diagnosticTarget->bodyCount : 0,
            restoredSolverHash,
            restoredPresentationHash,
            restoredBodyCount,
            diagnosticCheckpoint ? diagnosticCheckpoint->contactCount : 0,
            diagnosticCheckpoint ? diagnosticCheckpoint->pipelineRecordCount : 0,
            diagnosticCheckpoint ? diagnosticCheckpoint->checkpointBoundary : false,
            hashCaptured,
            hashMatched,
            fallbackAttempted,
            fallbackRestored,
            failureReason );
#else
        (void)failureReason;
        (void)diagnosticTarget;
        (void)diagnosticCheckpoint;
        (void)restoredSolverHash;
        (void)restoredPresentationHash;
        (void)restoredBodyCount;
        (void)hashCaptured;
        (void)hashMatched;
        (void)fallbackAttempted;
        (void)fallbackRestored;
#endif
    };

    auto failWithDiagnostic = [&]( const char* message,
                                   const ReplayV2SolverHashSample* diagnosticTarget,
                                   const ReplaySolverFrameSample* diagnosticCheckpoint,
                                   uint64_t restoredSolverHash = 0,
                                   uint64_t restoredPresentationHash = 0,
                                   std::size_t restoredBodyCount = 0,
                                   bool hashCaptured = false,
                                   bool hashMatched = false,
                                   bool fallbackAttempted = false,
                                   bool fallbackRestored = false ) -> bool
    {
        logRestoreDiagnostic( message,
                              diagnosticTarget,
                              diagnosticCheckpoint,
                              restoredSolverHash,
                              restoredPresentationHash,
                              restoredBodyCount,
                              hashCaptured,
                              hashMatched,
                              fallbackAttempted,
                              fallbackRestored );
        writeReason( message );
        return false;
    };

    if ( !path || path[0] == '\0' )
    {
        return failWithDiagnostic( "replay v2 target restore requires a v2 artifact path", target, checkpoint );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult checkpointResult;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &checkpointResult ) )
    {
        return failWithDiagnostic( "failed to load v2 solver checkpoints", target, checkpoint );
    }

    std::vector<ReplayV2SolverHashSample> hashes;
    ReplayV2SolverHashLoadResult hashResult;
    if ( !ReplayV2Artifact::LoadSolverHashes( path, hashes, &hashResult ) )
    {
        return failWithDiagnostic( "failed to load v2 solver hashes", target, checkpoint );
    }

    std::vector<ReplayEventSample> events;
    ReplayV2EventLoadResult eventResult;
    if ( !ReplayV2Artifact::LoadEvents( path, events, &eventResult ) )
    {
        return failWithDiagnostic( "failed to load v2 events", target, checkpoint );
    }

    std::vector<ReplayPresentationSample> presentationSamples;
    ReplayV2LoadResult presentationResult;
    if ( !ReplayV2Artifact::LoadPresentation( path, presentationSamples, &presentationResult ) )
    {
        return failWithDiagnostic( "failed to load v2 presentation frames", target, checkpoint );
    }

    if ( requestedFrame == LATEST_NON_CHECKPOINT_TARGET )
    {
        for ( auto it = hashes.rbegin(); it != hashes.rend(); ++it )
        {
            if ( !it->checkpointBoundary )
            {
                target = &*it;
                break;
            }
        }
        if ( !target )
        {
            return failWithDiagnostic( "found no saved non-checkpoint target hash", target, checkpoint );
        }
    }
    else
    {
        for ( const ReplayV2SolverHashSample& hash : hashes )
        {
            if ( hash.frameIndex == requestedFrame )
            {
                target = &hash;
                break;
            }
        }
        if ( !target )
        {
            char message[192] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "found no saved hash for requested target frame %llu",
                       static_cast<unsigned long long>( requestedFrame ) );
            return failWithDiagnostic( message, target, checkpoint );
        }
    }

    for ( const ReplaySolverFrameSample& candidate : checkpoints )
    {
        if ( candidate.frameIndex <= target->frameIndex &&
             ( !checkpoint || candidate.frameIndex > checkpoint->frameIndex ) )
        {
            checkpoint = &candidate;
        }
    }
    if ( !checkpoint )
    {
        return failWithDiagnostic( "found no checkpoint before target hash", target, checkpoint );
    }
    if ( checkpoint->frameIndex > target->frameIndex )
    {
        return failWithDiagnostic( "selected checkpoint after target frame", target, checkpoint );
    }
    if ( checkpoint->eventCursor == 0 )
    {
        return failWithDiagnostic( "loaded a checkpoint without an event cursor", target, checkpoint );
    }
    if ( target->frameIndex - checkpoint->frameIndex >
         static_cast<ReplayFrameIndex>( hashes.size() + events.size() + 1u ) )
    {
        return failWithDiagnostic( "selected an implausibly distant target frame", target, checkpoint );
    }

    ReplaySolverFrameSample liveBackup;
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        liveBackup = *latest;
        hasLiveBackup = true;
    }
    bool stateMutated = false;

    auto checkpointTopologyMatchesLive = [&]() -> bool
    {
        const std::vector<GameModel>& models = m_cGameModelCollection.PhysicsModels();
        if ( checkpoint->bodies.size() > models.size() )
        {
            return false;
        }
        for ( const ReplaySolverBodySample& body : checkpoint->bodies )
        {
            if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
            {
                return false;
            }
            if ( models[static_cast<std::size_t>( body.modelIndex )].GetReplayBodyId() != body.id.value )
            {
                return false;
            }
        }
        return true;
    };

    auto latestGeneratedSceneConfigBeforeCheckpoint = [&]() -> const ReplayEventSample*
    {
        const ReplayEventSample* generatedConfig = nullptr;
        for ( const ReplayEventSample& event : events )
        {
            if ( event.kind != ReplayEventKind::GeneratedSceneConfig || event.frameIndex > checkpoint->frameIndex ||
                 event.sequence >= checkpoint->eventCursor )
            {
                continue;
            }
            if ( event.branch.branchId != checkpoint->branch.branchId )
            {
                continue;
            }
            generatedConfig = &event;
        }
        return generatedConfig;
    };

    auto rebuildGeneratedSceneTopology =
        [&]( const ReplayEventSample& event, char* rebuildReason, std::size_t rebuildReasonSize ) -> bool
    {
        if ( event.value0 < 0 || event.value1 < 0 || event.value2 < 0 || event.value3 <= 0 )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene config contains invalid counts" );
            return false;
        }

        const uint32_t overrideBits =
            ( event.flags & REPLAY_GENERATED_SCENE_OVERRIDE_MASK ) >> REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
        if ( overrideBits > static_cast<uint32_t>( GeneratedObjectTypeOverride::AllBoxes ) )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene config has invalid override bits" );
            return false;
        }

        const bool exactSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS ) != 0;
        const bool uiSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS ) != 0;
        const bool uiModelCount = ( event.flags & REPLAY_GENERATED_SCENE_UI_MODEL_COUNT ) != 0;
        if ( exactSolverCounts && event.value1 + event.value2 != event.value0 )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated solver counts do not match model count" );
            return false;
        }
        if ( event.value0 > ActiveGameModelCapacity() )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene model count exceeds active capacity" );
            return false;
        }

        m_cGameModelCollection.Clear();
        m_runtimeTools.ClearRayCastTestLines();
        m_simulation.Reset();
        SceneState().rngSeed = static_cast<unsigned int>( event.value3 );
        SceneState().rngState = static_cast<unsigned int>( event.value3 );
        m_launchOptions.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( overrideBits );
        m_sceneUIOverrides.modelCountOverride = uiModelCount ? event.value0 : -1;
        m_sceneUIOverrides.solverBallCountOverride = uiSolverCounts || exactSolverCounts ? event.value1 : -1;
        m_sceneUIOverrides.solverBoxCountOverride = uiSolverCounts || exactSolverCounts ? event.value2 : -1;

        if ( exactSolverCounts || uiSolverCounts )
        {
            SceneGeneratedSetup::SetUpSolverObjects( BuildSceneGeneratedModelContext(), event.value1, event.value2 );
        }
        else
        {
            SceneGeneratedSetup::SetUpGameModels( BuildSceneGeneratedModelContext(), event.value0 );
        }
        m_cGameModelCollection.InvalidatePhysicsStreams();

        if ( !checkpointTopologyMatchesLive() )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "rebuilt generated topology still mismatches checkpoint" );
            return false;
        }
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "rebuilt generated topology" );
        return true;
    };

    auto failAfterMutation = [&]( const char* message,
                                  const ReplayV2SolverHashSample* diagnosticTarget,
                                  uint64_t restoredSolverHash = 0,
                                  uint64_t restoredPresentationHash = 0,
                                  std::size_t restoredBodyCount = 0,
                                  bool hashCaptured = false,
                                  bool hashMatched = false ) -> bool
    {
        bool fallbackRestored = false;
        if ( stateMutated && hasLiveBackup )
        {
            char fallbackReason[128] = {};
            fallbackRestored = ApplyReplaySolverSampleState( liveBackup, fallbackReason, sizeof( fallbackReason ) );
            m_cGameModelCollection.InvalidatePhysicsStreams();
        }
        return failWithDiagnostic( message,
                                   diagnosticTarget,
                                   checkpoint,
                                   restoredSolverHash,
                                   restoredPresentationHash,
                                   restoredBodyCount,
                                   hashCaptured,
                                   hashMatched,
                                   stateMutated && hasLiveBackup,
                                   fallbackRestored );
    };

    bool generatedTopologyRebuilt = false;
    if ( !checkpointTopologyMatchesLive() )
    {
        const ReplayEventSample* generatedConfig = latestGeneratedSceneConfigBeforeCheckpoint();
        if ( !generatedConfig )
        {
            return failAfterMutation( "checkpoint topology does not match live scene and no generated config was saved",
                                      target );
        }

        char rebuildReason[160] = {};
        stateMutated = true;
        if ( !rebuildGeneratedSceneTopology( *generatedConfig, rebuildReason, sizeof( rebuildReason ) ) )
        {
            char message[320] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "failed to rebuild generated scene topology: %s",
                       rebuildReason[0] != '\0' ? rebuildReason : "unknown rebuild failure" );
            return failAfterMutation( message, target );
        }
        generatedTopologyRebuilt = true;
    }

    char reason[192] = {};
    if ( !ApplyReplaySolverSampleState( *checkpoint, reason, sizeof( reason ) ) )
    {
        char message[288] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "failed to apply checkpoint: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        return failWithDiagnostic( message, target, checkpoint );
    }
    stateMutated = true;
    m_cGameModelCollection.InvalidatePhysicsStreams();

    ReplayFrameIndex currentFrame = checkpoint->frameIndex;
    int currentSceneFrame = checkpoint->sceneFrame;
    uint32_t eventCursor = checkpoint->eventCursor;
    std::size_t eventsApplied = 0;
    std::size_t unsupportedEvents = 0;
    SceneState().currentFrame = currentSceneFrame;

    {
        ScopedReplayProbeProfilerFrame profilerFrame;
        while ( currentFrame < target->frameIndex )
        {
            const ReplayFrameIndex nextFrame = currentFrame + 1u;

            for ( const ReplayEventSample& event : events )
            {
                if ( event.frameIndex != nextFrame || event.sequence < eventCursor )
                {
                    continue;
                }
                if ( event.branch.branchId != checkpoint->branch.branchId )
                {
                    ++unsupportedEvents;
                    continue;
                }

                char eventReason[160] = {};
                if ( !ApplyReplayEventForRestoreTarget( event, eventReason, sizeof( eventReason ) ) )
                {
                    char message[320] = {};
                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe failed to apply event sequence %u at frame %llu: %s",
                               event.sequence,
                               static_cast<unsigned long long>( event.frameIndex ),
                               eventReason[0] != '\0' ? eventReason : "unknown event replay failure" );
                    return failAfterMutation( message, target );
                }
                eventCursor = (std::max)( eventCursor, event.sequence + 1u );
                ++eventsApplied;
            }

            m_runtimeTools.TickRayCastTestLines( PHYSICS_FIXED_DT );
            m_runtimeTools.Laser().Update( PHYSICS_FIXED_DT );
            m_cGameModelCollection.EndCollisionVisualFrame();
            ++currentSceneFrame;
            SceneState().currentFrame = currentSceneFrame;
            m_cGameModelCollection.BeginCollisionVisualFrame();

            m_cGameModelCollection.GetPhysicsEngine().Step( m_cGameModelCollection, PHYSICS_FIXED_DT );
            currentFrame = nextFrame;

            const ReplayV2SolverHashSample* expectedHash = FindReplaySolverHashForFrame( hashes, currentFrame );
            if ( !expectedHash )
            {
                return failAfterMutation( "could not find stepped hash metadata", target );
            }

            ReplaySolverFrameSample stepReference;
            stepReference.frameIndex = expectedHash->frameIndex;
            stepReference.branch = checkpoint->branch;
            stepReference.eventCursor = eventCursor;
            stepReference.sceneFrame = expectedHash->sceneFrame;
            stepReference.simulationSeconds = expectedHash->simulationSeconds;
            stepReference.physicsDt = PHYSICS_FIXED_DT;

            uint64_t stepSolverHash = 0;
            uint64_t stepPresentationHash = 0;
            std::size_t stepBodyCount = 0;
            if ( !CaptureCurrentReplaySolverHash( stepReference, stepSolverHash, stepPresentationHash, stepBodyCount ) )
            {
                return failAfterMutation( "failed to capture stepped hash", expectedHash );
            }
            if ( stepBodyCount != expectedHash->bodyCount || stepSolverHash != expectedHash->solverHash )
            {
                char message[1024] = {};
                const ReplayPresentationSample* expectedPresentation =
                    FindReplayPresentationForFrame( presentationSamples, currentFrame );
                const std::vector<GameModel>& restoredModels = m_cGameModelCollection.PhysicsModels();
                if ( expectedPresentation && !expectedPresentation->bodies.empty() && !restoredModels.empty() )
                {
                    const ReplayBodyPresentationSample& expectedBody = expectedPresentation->bodies[0];
                    const GameModel& restoredModel = restoredModels[0];
                    const Vector3& restoredPosition = restoredModel.GetPosition();
                    const Vector3& restoredVelocity = restoredModel.GetVelocity();
                    float restoredQx = 0.0f;
                    float restoredQy = 0.0f;
                    float restoredQz = 0.0f;
                    float restoredQw = 1.0f;
                    restoredModel.GetOrientation().GetComponents( restoredQx, restoredQy, restoredQz, restoredQw );

                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                               "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                               "restored_pos=(%.6f,%.6f,%.6f) expected_pos=(%.6f,%.6f,%.6f) "
                               "restored_vel=(%.6f,%.6f,%.6f) restored_q=(%.6f,%.6f,%.6f,%.6f) "
                               "expected_q=(%.6f,%.6f,%.6f,%.6f) restored_body_id=%u expected_body_id=%u "
                               "events_applied=%llu",
                               static_cast<unsigned long long>( currentFrame ),
                               static_cast<unsigned long long>( stepSolverHash ),
                               static_cast<unsigned long long>( expectedHash->solverHash ),
                               static_cast<unsigned long long>( stepPresentationHash ),
                               static_cast<unsigned long long>( expectedHash->presentationHash ),
                               restoredPosition.x,
                               restoredPosition.y,
                               restoredPosition.z,
                               expectedBody.position.x,
                               expectedBody.position.y,
                               expectedBody.position.z,
                               restoredVelocity.x,
                               restoredVelocity.y,
                               restoredVelocity.z,
                               restoredQx,
                               restoredQy,
                               restoredQz,
                               restoredQw,
                               expectedBody.orientation[0],
                               expectedBody.orientation[1],
                               expectedBody.orientation[2],
                               expectedBody.orientation[3],
                               restoredModel.GetReplayBodyId(),
                               expectedBody.id.value,
                               static_cast<unsigned long long>( eventsApplied ) );
                }
                else
                {
                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                               "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                               "restored_bodies=%llu expected_bodies=%u events_applied=%llu",
                               static_cast<unsigned long long>( currentFrame ),
                               static_cast<unsigned long long>( stepSolverHash ),
                               static_cast<unsigned long long>( expectedHash->solverHash ),
                               static_cast<unsigned long long>( stepPresentationHash ),
                               static_cast<unsigned long long>( expectedHash->presentationHash ),
                               static_cast<unsigned long long>( stepBodyCount ),
                               expectedHash->bodyCount,
                               static_cast<unsigned long long>( eventsApplied ) );
                }
                return failAfterMutation( message,
                                          expectedHash,
                                          stepSolverHash,
                                          stepPresentationHash,
                                          stepBodyCount,
                                          true );
            }
        }
    }

    if ( unsupportedEvents != 0 )
    {
        return failAfterMutation( "encountered unsupported branch events before target", target );
    }

    ReplaySolverFrameSample reference;
    reference.frameIndex = target->frameIndex;
    reference.branch = checkpoint->branch;
    reference.eventCursor = eventCursor;
    reference.sceneFrame = target->sceneFrame;
    reference.simulationSeconds = target->simulationSeconds;
    reference.physicsDt = PHYSICS_FIXED_DT;

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    if ( !CaptureCurrentReplaySolverHash( reference, restoredSolverHash, restoredPresentationHash, restoredBodyCount ) )
    {
        return failAfterMutation( "failed to capture target hash", target );
    }
    if ( restoredBodyCount != target->bodyCount )
    {
        char message[256] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe body count mismatch: restored=%llu expected=%u",
                   static_cast<unsigned long long>( restoredBodyCount ),
                   target->bodyCount );
        return failAfterMutation( message,
                                  target,
                                  restoredSolverHash,
                                  restoredPresentationHash,
                                  restoredBodyCount,
                                  true );
    }
    if ( restoredSolverHash != target->solverHash )
    {
        char message[320] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe solver hash mismatch: restored=0x%016llX expected=0x%016llX",
                   static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( target->solverHash ) );
        return failAfterMutation( message,
                                  target,
                                  restoredSolverHash,
                                  restoredPresentationHash,
                                  restoredBodyCount,
                                  true );
    }

    outResult.checkpointCount = checkpointResult.checkpointCount;
    outResult.eventCount = eventResult.eventCount;
    outResult.hashCount = hashResult.hashCount;
    outResult.eventsApplied = eventsApplied;
    outResult.bodyCount = restoredBodyCount;
    outResult.fileBytes = hashResult.fileBytes;
    outResult.checkpointFrame = checkpoint->frameIndex;
    outResult.targetFrame = target->frameIndex;
    outResult.eventCursor = eventCursor;
    outResult.solverHash = restoredSolverHash;
    outResult.presentationHash = restoredPresentationHash;
    outResult.generatedTopologyRebuilt = generatedTopologyRebuilt;

    logRestoreDiagnostic( "",
                          target,
                          checkpoint,
                          restoredSolverHash,
                          restoredPresentationHash,
                          restoredBodyCount,
                          true,
                          true,
                          false,
                          false );

    if ( makeLiveBranch )
    {
        const uint32_t parentBranchId =
            checkpoint->branch.branchId != 0
                ? checkpoint->branch.branchId
                : ( m_replayRuntime.Branch().branchId != 0 ? m_replayRuntime.Branch().branchId : 1u );
        ReplayBranchInfo restoredBranch;
        restoredBranch.branchId = (std::max)( m_replayRuntime.Branch().branchId, parentBranchId ) + 1u;
        restoredBranch.parentBranchId = parentBranchId;
        restoredBranch.startFrame = 0;
        restoredBranch.sourceFrame = target->frameIndex;
        restoredBranch.sourceSolverHash = target->solverHash;
        m_replayRuntime.Branch() = restoredBranch;
        ResetReplayTimelineForActiveScene( true );
        RecordReplayEvent( ReplayEventKind::BranchRestore,
                           0,
                           0,
                           static_cast<int32_t>( parentBranchId ),
                           target->sceneFrame,
                           0,
                           0,
                           target->solverHash,
                           "hash-verified v2 file restore" );
        outResult.branchId = restoredBranch.branchId;
        outResult.parentBranchId = parentBranchId;
        outResult.madeLiveBranch = true;
    }

    writeReason( "restored hash match" );
    return true;
}

#ifdef _DEBUG
void Run::VerifyReplaySolverTargetFileProbe( const char* path )
{
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayV2ArtifactTargetState( path,
                                              ( std::numeric_limits<ReplayFrameIndex>::max )(),
                                              false,
                                              result,
                                              reason,
                                              sizeof( reason ) ) )
    {
        char message[384] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    printf( "[replay] Restore target probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
}

void Run::VerifyReplaySolverFailureFileProbe( const char* path )
{
    constexpr ReplayFrameIndex MISSING_TARGET_FRAME = 999999999u;
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( RestoreReplayV2ArtifactTargetState( path, MISSING_TARGET_FRAME, false, result, reason, sizeof( reason ) ) )
    {
        throw std::runtime_error( "replay restore failure probe unexpectedly restored a missing target frame" );
    }
    if ( strstr( reason, "found no saved hash for requested target frame" ) == nullptr )
    {
        char message[384] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore failure probe produced an unexpected reason: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }

    printf( "[replay] Restore failure probe passed: path=%s missing_frame=%llu reason=\"%s\"\n",
            path,
            static_cast<unsigned long long>( MISSING_TARGET_FRAME ),
            reason );
}

void Run::VerifyReplaySolverBranchFileProbe( const char* path )
{
    if ( !LoadReplayPresentationArtifact( path, true ) )
    {
        throw std::runtime_error( "replay restore branch probe failed to load v2 presentation scrub source" );
    }
    m_replayRuntime.Scrubber().historicalSamplePaused = true;
    m_replayRuntime.Scrubber().activeTrack = RunReplayTrack::Presentation;
    ReplayScrubberSetTrackPosition( m_replayRuntime.Scrubber(), RunReplayTrack::Presentation, 1.0f );

    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayScrubberSelectionAsLive( m_timers.simulationTimer.GetTotalTime(),
                                                &result,
                                                reason,
                                                sizeof( reason ) ) )
    {
        char message[384] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore branch probe failed: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        throw std::runtime_error( message );
    }
    if ( !result.madeLiveBranch || result.branchId == 0 )
    {
        throw std::runtime_error( "replay restore branch probe did not create a scrubber live branch" );
    }

    printf( "[replay] Restore branch probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "branch_id=%u parent_branch_id=%u solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            result.branchId,
            result.parentBranchId,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
}
#endif


void Run::EnterInteractiveSceneRun()
{
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
}


bool Run::CanSceneAutomationQuit() const
{
    return !SceneState().isInteractiveRun;
}


void Run::HoldCompletedInteractiveScene()
{
    SceneState().isTestComplete = true;
    SceneState().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
}


bool Run::TickScreenshots()
{
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );

    struct ScreenshotSink final : RuntimeCaptureSink
    {
        explicit ScreenshotSink( Run& owner ) : run( owner )
        {
        }

        void SaveScreenshot( const char* path ) override
        {
            run.SaveScreenshot( path );
        }

        Run& run;
    };

    ScreenshotSink sink( *this );
    const std::string* scenePath = CurrentSceneQueuePath();
    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture().TickScreenshots(
        RuntimeCaptureSceneContext{ SceneState().isSceneMode,
                                    SceneState().isInteractiveRun,
                                    SceneState().currentFrame,
                                    m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                    scenePath ? scenePath->c_str() : nullptr },
        sink );

    PROFILE_END( "Frame/PostDraw/Screenshots" );

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END();
    }

#ifdef _DEBUG
    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        LogSceneFinished( "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        LogSceneFinished( "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:
        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
        if ( !AdvanceScene() )
        {
            PostQuitMessage( 0 );
        }
        break;
    case RuntimeCaptureAutomation::HoldInteractive:
        HoldCompletedInteractiveScene();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle()
{
    struct ScreenshotSink final : RuntimeCaptureSink
    {
        explicit ScreenshotSink( Run& owner ) : run( owner )
        {
        }

        void SaveScreenshot( const char* path ) override
        {
            run.SaveScreenshot( path );
        }

        Run& run;
    };

    ScreenshotSink sink( *this );
    const RuntimeCaptureResult result =
        m_diagnosticsRuntime.Capture().TickAutoCycle( SceneState().isSceneMode,
                                                      SceneState().isInteractiveRun,
                                                      m_cGameModelCollection.GetModelCount(),
                                                      m_camera.autoCycleInterval,
                                                      m_camera.autoCycleAccum,
                                                      m_camera.autoCycleShotsTaken,
                                                      m_camera.trackBallIndex,
                                                      sink );

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    LogSceneFinished( "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        HoldCompletedInteractiveScene();
    }
}


void Run::TickPerfLog()
{
    m_diagnosticsRuntime.TickPerfLog( RuntimePerfTickContext{ sPerfPass + 1,
                                                              SceneState().currentFrame + 1,
                                                              m_timers.physicsTime,
                                                              m_timers.renderTime } );

    if ( ( SceneState().currentFrame + 1 ) % 60 == 0 )
    {
        LogPerfMemory( "periodic" );
    }
}


bool Run::TickSceneAdvance()
{
    ++SceneState().currentFrame;

    const bool hasRequiredContactGate = !m_requiredSceneContacts.empty();
    const bool hasRequiredBroadphaseGate = !m_requiredBroadphaseXCells.empty();
    const bool hasRequiredSceneGate = hasRequiredContactGate || hasRequiredBroadphaseGate;
    const bool requiredContactsComplete = RequiredSceneContactsComplete();
    const bool requiredBroadphaseComplete = RequiredSceneBroadphaseXCellsComplete();
    const bool requiredSceneComplete = requiredContactsComplete && requiredBroadphaseComplete;
    if ( hasRequiredSceneGate && requiredSceneComplete && !SceneState().isTestComplete )
    {
#ifdef _DEBUG
        LogSceneFinished( "required_scene_gates" );
#endif
        if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
        {
            if ( !AdvanceScene() )
            {
                PostQuitMessage( 0 );
            }
            return true;
        }

        if ( CanSceneAutomationQuit() )
        {
            SceneState().isTestComplete = true;
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
    }

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( SceneState().targetFrameCount > 0 && !m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved )
    {
        if ( SceneState().currentFrame >= SceneState().targetFrameCount )
        {
            const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;
#ifdef _DEBUG
            if ( !SceneState().isTestComplete &&
                 ( frameCountCompletesScene || SceneState().currentFrame == SceneState().targetFrameCount ) )
            {
                LogSceneFinished( frameCountCompletesScene ? "frame_count" : "required_scene_gates_missing" );
                if ( !frameCountCompletesScene )
                {
                    for ( const RunRequiredContactState& contact : m_requiredSceneContacts )
                    {
                        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
                        {
                            fprintf( stderr,
                                     "[scene] required_contact missing: %s <-> %s\n",
                                     contact.nameA,
                                     contact.nameB );
                        }
                    }
                    for ( const RunRequiredBroadphaseXCellsState& cells : m_requiredBroadphaseXCells )
                    {
                        if ( !cells.activated )
                        {
                            fprintf( stderr,
                                     "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                                     "active_cells=%d observed_x=%s%d..%d\n",
                                     cells.minCellX,
                                     cells.maxCellX,
                                     cells.cellY,
                                     cells.cellZ,
                                     cells.lastMissingCellX,
                                     cells.lastActiveCellCount,
                                     cells.hasObservedXRange ? "" : "none ",
                                     cells.lastObservedMinX,
                                     cells.lastObservedMaxX );
                        }
                    }
                }
            }
#endif
            if ( !frameCountCompletesScene )
            {
                return false;
            }

            if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
            {
                if ( !AdvanceScene() )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                if ( frameCountCompletesScene && CanSceneAutomationQuit() )
                {
                    SceneState().isTestComplete = true;
                }
                else if ( frameCountCompletesScene )
                {
                    HoldCompletedInteractiveScene();
                }
            }
        }
    }

    // Generated demo mode: restart every 20s to keep the sandbox moving indefinitely.
    if ( !SceneState().isSceneMode && !IsManualCameraMode() && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        LoadScene( SceneState().currentSceneIndex,
                   SceneState().isInteractiveRun,
                   SceneState().isInteractiveRun,
                   SceneState().isInteractiveRun );
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_diagnosticsRuntime.PerfLog().isPerfTest && SceneState().targetFrameCount <= 0 &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > PERF_TEST_PASS_SECONDS )
    {
#ifdef _DEBUG
        LogSceneFinished( "perf_duration" );
#endif
        if ( !AdvanceScene() )
        {
            if ( CanSceneAutomationQuit() )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                HoldCompletedInteractiveScene();
            }
        }
        return true;
    }

    return false;
}


void Run::UpdateLogic( float simulationDt, float cameraDt )
{
    // Auto-cycle
    if ( SceneState().isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += simulationDt;
    }

    // Camera controls are presentation-time behavior, not simulation-time
    // behavior. Keyboard travel is velocity-based, so it consumes unscaled real
    // frame time. Mouse look consumes a per-frame cursor delta, so using live dt
    // would make sensitivity vary with FPS; the fixed reference preserves the
    // existing 60 Hz tuning while making the result frame-rate independent.
    MoveCamera( cameraDt * Cfg().keySpeed, CAMERA_MOUSE_REFERENCE_DT * Cfg().mouseSensitivity );
    TickAttachedCamera();

    UpdateWaterHeightControls( simulationDt );

    // Tween speed is also presentation-time behavior. The selected destination
    // camera can still track moving scene objects, but the interpolation rate
    // itself should be stable in real seconds instead of following time_scale.
    m_systems.cameras->SetTweenSpeed( Cfg().cameraTweenRate * cameraDt );
}


void Run::UpdateWaterHeightControls( float dt )
{
    const bool downNow = Input::IsKeyDown( VK_NEXT );
    const bool upNow = Input::IsKeyDown( VK_PRIOR );
    if ( downNow == upNow )
    {
        return;
    }

    const float direction = upNow ? 1.0f : -1.0f;
    const float height = m_cWorldEnvironment.GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt;
    m_cWorldEnvironment.SetFluidSurfaceHeight( height );
}
