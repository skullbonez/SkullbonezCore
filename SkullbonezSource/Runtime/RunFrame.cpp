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

#include <stdexcept>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

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

            if ( m_uiTextPass.ShouldRender() )
            {
                const int uiDrawCallStart = Gfx().GetFrameDrawCallCount();
                PROFILE_BEGIN( "Frame/UI" );
                {
                    DRAW_CALL_TRACE_SCOPE( "Frame/UI" );
                    m_uiTextPass.Render( secondsPerFrame );
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
                    ::HashStr( "Frame/Render/TransparentBalls" ),
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

    const bool replaySimulationPaused = m_replayScrubber.simulationPaused;
    const bool stepRequested = Input::IsKeyDown( VK_SPACE );
    const bool manipulatorPhysics = m_camera.mode == RunCameraMode::Manipulator;
    const bool replayCapture = m_replay.IsEnabled() || m_solverReplay.IsEnabled();
#ifdef _DEBUG
    const bool physicsCapture = m_diagnostics.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnostics.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnostics.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    const SimulationTickResult tick = m_simulation.Tick(
        SimulationTickInput{ secondsPerFrame,
                             replaySimulationPaused && !stepRequested ? 0.0f : SceneState().timeScale,
                             SceneState().isSceneMode,
                             SceneState().isScenePhysics,
                             SceneState().isFixedStep,
                             ( m_camera.isFlyMode && !physicsCapture ) || replaySimulationPaused,
                             m_camera.isLauncherMode,
                             manipulatorPhysics,
                             stepRequested,
                             &m_cGameModelCollection,
                             manipulatorPhysics ? &Run::ApplyMousePickupPhysicsStepThunk : nullptr,
                             this,
                             ( manipulatorPhysics || replayCapture ) ? &Run::AfterPhysicsStepThunk : nullptr,
                             this } );
    TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_launcherLaser.Update( static_cast<float>( secondsPerFrame ) );
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
    if ( m_replay.IsEnabled() || m_solverReplay.IsEnabled() )
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
        m_replay.CaptureFrame( input );
        m_solverReplay.CaptureFrame( input );
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
    outSample.nextRayLine = m_rayCastTest.nextLine;
    outSample.fireMode = m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? ReplayLauncherFireMode::Projectile
                                                                                   : ReplayLauncherFireMode::Laser;
    outSample.visualizeRays = m_rayCastTest.visualizeRays;
    outSample.impulseStrength = m_rayCastTest.impulseStrength;
    outSample.projectileSpeed = m_rayCastTest.projectileSpeed;
    outSample.rayLines.reserve( RunRayCastTestState::MAX_LINES );
    for ( const RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        ReplayRayCastLineSample sample;
        sample.start = line.start;
        sample.end = line.end;
        sample.ageSeconds = line.ageSeconds;
        sample.active = line.active;
        sample.hit = line.hit;
        outSample.rayLines.push_back( sample );
    }
    m_launcherLaser.CaptureShots( outSample.laserShots, outSample.nextLaserShot );
}


void Run::CompareLatestReplaySamples()
{
    const ReplayPresentationSample* presentation = m_replay.LatestSample();
    const ReplaySolverFrameSample* solver = m_solverReplay.LatestSample();
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

    if ( m_solverReplayMismatchReports < 8 )
    {
        ++m_solverReplayMismatchReports;
        fprintf( stderr,
                 "[replay] Solver/presentation capture mismatch #%u: presentation_frame=%llu solver_frame=%llu "
                 "presentation_hash=0x%016llX solver_presentation_hash=0x%016llX solver_hash=0x%016llX "
                 "presentation_bodies=%llu solver_bodies=%llu\n",
                 m_solverReplayMismatchReports,
                 static_cast<unsigned long long>( presentation->frameIndex ),
                 static_cast<unsigned long long>( solver->frameIndex ),
                 static_cast<unsigned long long>( presentation->stateHash ),
                 static_cast<unsigned long long>( solver->presentationHash ),
                 static_cast<unsigned long long>( solver->solverHash ),
                 static_cast<unsigned long long>( presentation->bodies.size() ),
                 static_cast<unsigned long long>( solver->bodies.size() ) );
    }
    else if ( !m_solverReplayMismatchSuppressed )
    {
        m_solverReplayMismatchSuppressed = true;
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

    const ReplayRecorderStats stats = m_replay.GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayScrubProbe.minSampleCount ) )
    {
        return;
    }

    const ReplayPresentationSample* selected = m_replay.SampleAtNormalized( m_replayScrubProbe.normalized );
    const ReplayPresentationSample* live = m_replay.LatestSample();
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

    const bool applied = ApplyReplayPresentationSampleForRender( *selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay scrub probe failed to apply the selected presentation sample" );
    }
    const Math::Vector::Vector3 appliedPosition = probedModel.GetPosition();
    const float appliedDeltaSquared = distanceSquared( appliedPosition, selectedBody->position );
    if ( appliedDeltaSquared > m_replayScrubProbe.minDistanceSquared )
    {
        RestoreReplayPresentationRenderPose();
        throw std::runtime_error( "replay scrub probe did not move the live model to the selected replay sample" );
    }

    RestoreReplayPresentationRenderPose();
    const Math::Vector::Vector3 restoredPosition = probedModel.GetPosition();
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    const bool restored = restoredDeltaSquared <= m_replayScrubProbe.minDistanceSquared;
    if ( !restored )
    {
        throw std::runtime_error(
            "replay scrub probe did not restore the live model after applying the selected sample" );
    }

    RuntimeDiagnostics::LogReplayScrubProbe( m_diagnostics.PhysicsDiagnostics(),
                                             SceneState(),
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

    const ReplayRecorderStats stats = m_solverReplay.GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayRestoreProbe.minSampleCount ) )
    {
        return;
    }

    const ReplaySolverFrameSample* selectedSample =
        m_solverReplay.SampleAtNormalized( m_replayRestoreProbe.normalized );
    const ReplaySolverFrameSample* latestSample = m_solverReplay.LatestSample();
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

    const ReplayRecorderStats stats = m_replay.GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replaySaveProbe.minSampleCount ) )
    {
        return;
    }

    ReplayV2SaveResult result;
    if ( !ReplayV2Artifact::SavePresentationWithSolverHashes( m_replay,
                                                              m_solverReplay,
                                                              m_replaySaveProbe.path,
                                                              &result ) )
    {
        throw std::runtime_error( "replay save probe failed to write v2 presentation artifact" );
    }
    if ( result.solverHashCount < result.sampleCount )
    {
        throw std::runtime_error( "replay save probe wrote v2 artifact without a full solver hash track" );
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

    const bool applied = ApplyReplayPresentationSampleForRender( selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay save probe failed to apply the loaded v2 presentation sample" );
    }
    const Math::Vector::Vector3 appliedPosition = probedModel.GetPosition();
    const float appliedDeltaSquared = distanceSquared( appliedPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayPresentationRenderPose();
        throw std::runtime_error( "replay save probe did not move the live model to the loaded v2 sample" );
    }

    RestoreReplayPresentationRenderPose();
    const Math::Vector::Vector3 restoredPosition = probedModel.GetPosition();
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay save probe did not restore after applying the loaded v2 sample" );
    }

    m_replaySaveProbe.completed = true;
    printf( "[replay] Save probe wrote: path=%s samples=%llu bodies=%llu solver_hashes=%llu bytes=%llu\n",
            m_replaySaveProbe.path,
            static_cast<unsigned long long>( result.sampleCount ),
            static_cast<unsigned long long>( result.bodyDictionaryCount ),
            static_cast<unsigned long long>( result.solverHashCount ),
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
    const bool applied = ApplyReplayPresentationSampleForRender( *selected );
    if ( !applied )
    {
        throw std::runtime_error( "replay load probe failed to apply the selected loaded v2 sample" );
    }

    const Math::Vector::Vector3 appliedPosition = probedModel.GetPosition();
    const float appliedDeltaSquared = distanceSquared( appliedPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayPresentationRenderPose();
        throw std::runtime_error( "replay load probe did not move the model to the selected loaded v2 sample" );
    }

    RestoreReplayPresentationRenderPose();
    const Math::Vector::Vector3 restoredPosition = probedModel.GetPosition();
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        throw std::runtime_error( "replay load probe did not restore after applying the selected loaded v2 sample" );
    }

    printf( "[replay] Load probe passed: path=%s samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            m_loadedPresentationReplay.path,
            static_cast<unsigned long long>( m_loadedPresentationReplay.samples.size() ),
            static_cast<unsigned long long>( m_loadedPresentationReplay.bodyDictionaryCount ),
            static_cast<unsigned long long>( m_loadedPresentationReplay.firstFrame ),
            static_cast<unsigned long long>( selected->frameIndex ),
            static_cast<unsigned long long>( latest->frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
}
#endif


void Run::EnterInteractiveSceneRun()
{
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;
    m_capture.Screenshot().isScreenshotAndExit = false;
}


bool Run::CanSceneAutomationQuit() const
{
    return !SceneState().isInteractiveRun;
}


void Run::HoldCompletedInteractiveScene()
{
    SceneState().isTestComplete = true;
    SceneState().isExitOnComplete = false;
    m_capture.Screenshot().isScreenshotAndExit = false;
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
    const RuntimeCaptureResult result = m_capture.TickScreenshots(
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
    const RuntimeCaptureResult result = m_capture.TickAutoCycle( SceneState().isSceneMode,
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
    m_diagnostics.TickPerfLog( RuntimePerfTickContext{ sPerfPass + 1,
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
    if ( SceneState().targetFrameCount > 0 && !m_capture.Screenshot().isScreenshotSaved )
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
    if ( !SceneState().isSceneMode && !m_camera.isFlyMode && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        LoadScene( SceneState().currentSceneIndex,
                   SceneState().isInteractiveRun,
                   SceneState().isInteractiveRun,
                   SceneState().isInteractiveRun );
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_diagnostics.PerfLog().isPerfTest && SceneState().targetFrameCount <= 0 &&
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
