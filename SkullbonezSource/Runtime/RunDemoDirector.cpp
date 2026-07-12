/*
File: SkullbonezSource/Runtime/RunDemoDirector.cpp
Purpose:
  Applies Demo Director shot-list camera poses, phase styles, and reveal pacing.

Summary:
  The director never invents framing or looks. A loaded shot list owns authored
  camera/style phases, RunCameraState owns playback timers, and this file
  blends the selected phase into runtime camera/style/replay presentation owners
  before rendering.

Glossary:
  Phase pose: Authored eye, view target, and up vector stored in `.shot.json`.
  Blend start pose: Camera pose captured when a phase transition begins.
  Grab: Temporary operator ownership of the camera while Director mode remains
    selected.
  Director advance: Manual, timer, or reveal-synced rule that selects the next
    authored phase without changing simulation ownership.
  Phase style: Optional `.style.json` applied through SceneRuntimeStyle when a
    phase becomes active.
  Lane R result: Recoverable style-load failure that skips the phase style while
    Director playback continues.
  Reveal rate: Authored multiplier for prediction seconds revealed per real
    second while this phase is active.

Invariants:
  - Director playback is presentation-only; it must not mutate physics state.
  - Camera writes go through CameraCollection::SetPrimaryPose so render camera,
    listener, replay, and screenshot paths keep using the normal camera owner.
  - Phase style writes go through SceneRuntimeStyle so material/cinematic
    changes stay inside the existing render-facing scene owner.
  - Reveal-rate writes only affect replay overlay presentation timing; they
    must not mark prediction dirty or rebuild private physics state.
  - Empty or missing shot lists leave Director mode as a no-op.

Related:
  - SkullbonezSource/Runtime/RunDemoDirector.h
  - SkullbonezSource/Runtime/DemoDirector.h
  - SkullbonezSource/Runtime/Scene/SceneController.h
*/
#include "RunDemoDirector.h"
#include "Replay/ReplayRuntime.h"
#include "Scene/SceneRuntimeStyle.h"

#include "../Physics/PhysicsTimestep.h"
#include "../Scene/TestScene.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
using Math::Vector::Vector3;

Vector3 LerpVector( const Vector3& from, const Vector3& to, float t )
{
    return from + ( to - from ) * t;
}

DemoCameraPose LerpPose( const DemoCameraPose& from, const DemoCameraPose& to, float t )
{
    DemoCameraPose pose;
    pose.eye = LerpVector( from.eye, to.eye, t );
    pose.view = LerpVector( from.view, to.view, t );
    pose.up = LerpVector( from.up, to.up, t );
    return pose;
}

float PhaseBlendAlpha( const DemoPhase& phase, float blendElapsedSeconds )
{
    if ( phase.blendInSeconds <= 0.0f )
    {
        return 1.0f;
    }
    return std::clamp( blendElapsedSeconds / phase.blendInSeconds, 0.0f, 1.0f );
}

DemoCameraPose CaptureCurrentPose( Environment::CameraCollection& cameras )
{
    DemoCameraPose pose;
    pose.eye = cameras.GetCameraTranslation();
    pose.view = cameras.GetCameraView();
    pose.up = cameras.GetCameraUp();
    return pose;
}

void ResetBlendFromCurrentPose( DemoDirectorPlaybackState& director, Environment::CameraCollection& cameras )
{
    director.blendStartPose = CaptureCurrentPose( cameras );
    director.blendElapsedSeconds = 0.0f;
}

bool HasPlayableShotList( const DemoDirectorPlaybackState& director )
{
    return director.hasActiveShotList && director.activeShotList.phaseCount > 0;
}

bool IsCurrentPhaseValid( const DemoDirectorPlaybackState& director )
{
    return HasPlayableShotList( director ) && director.currentPhaseIndex >= 0 &&
           director.currentPhaseIndex < director.activeShotList.phaseCount;
}

const DemoPhase& CurrentPhase( const DemoDirectorPlaybackState& director )
{
    return director.activeShotList.phases[static_cast<std::size_t>( director.currentPhaseIndex )];
}

DemoPhase& CurrentPhase( DemoDirectorPlaybackState& director )
{
    return director.activeShotList.phases[static_cast<std::size_t>( director.currentPhaseIndex )];
}

void CopyShotListPath( DemoDirectorPlaybackState& director, const char* path )
{
    director.activeShotListPath[0] = '\0';
    if ( path && path[0] )
    {
        std::snprintf( director.activeShotListPath, sizeof( director.activeShotListPath ), "%s", path );
    }
}

void RememberPhaseStyleAttempt( DemoDirectorPlaybackState& director, const DemoPhase& phase )
{
    director.appliedStylePhaseIndex = director.currentPhaseIndex;
    std::snprintf( director.appliedStylePath, sizeof( director.appliedStylePath ), "%s", phase.stylePath );
}

bool PhaseStyleAttempted( const DemoDirectorPlaybackState& director, const DemoPhase& phase )
{
    return director.appliedStylePhaseIndex == director.currentPhaseIndex &&
           std::strncmp( director.appliedStylePath, phase.stylePath, sizeof( director.appliedStylePath ) ) == 0;
}

float NormalizedRevealRate( float revealRate )
{
    return revealRate > 0.0f ? revealRate : 1.0f;
}

bool PhaseRevealRateApplied( const DemoDirectorPlaybackState& director, const DemoPhase& phase )
{
    const float revealRate = NormalizedRevealRate( phase.revealRate );
    return director.appliedRevealRatePhaseIndex == director.currentPhaseIndex &&
           director.appliedRevealRate == revealRate;
}

void ResetPhaseStyleApplication( DemoDirectorPlaybackState& director )
{
    director.appliedStylePhaseIndex = -1;
    director.appliedStylePath[0] = '\0';
}

void ResetPhaseEntryApplications( DemoDirectorPlaybackState& director )
{
    ResetPhaseStyleApplication( director );
    director.appliedRevealRatePhaseIndex = -1;
}

void SetPredictionRevealRatePreservingCursor( RunReplayPredictionState& prediction, double revealRate )
{
    const double normalizedRevealRate = revealRate > 0.0 ? revealRate : 1.0;
    const double previousRevealRate =
        prediction.revealClock.secondsPerSecond > 0.0 ? prediction.revealClock.secondsPerSecond : 1.0;
    if ( prediction.revealClock.anchorValid )
    {
        const auto now = std::chrono::steady_clock::now();
        const double elapsedSeconds =
            (std::max)( 0.0, std::chrono::duration<double>( now - prediction.revealClock.anchor ).count() );
        const double revealedSeconds = elapsedSeconds * previousRevealRate;
        prediction.revealClock.anchor =
            now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>( revealedSeconds / normalizedRevealRate ) );
    }
    prediction.revealClock.secondsPerSecond = normalizedRevealRate;
}

bool ActivePredictionLastFrame( const RunReplayPredictionState& prediction, ReplayFrameIndex& outLastFrame )
{
    const bool usingBuildFrames = prediction.BuildPrefixShouldBePresented();
    const auto& activeFrames = usingBuildFrames ? prediction.build.buildFrames : prediction.simulation.frames;
    const std::size_t activeFrameCount = usingBuildFrames ? prediction.PublishedBuildFrameCount() : activeFrames.size();
    if ( activeFrameCount < 2 )
    {
        outLastFrame = 0;
        return false;
    }

    outLastFrame = activeFrames[activeFrameCount - 1].frameIndex;
    return outLastFrame > 0;
}

ReplayFrameIndex PredictionRevealFrameForAdvance( const RunReplayPredictionState& prediction,
                                                  ReplayFrameIndex lastAvailableFrame )
{
    if ( !prediction.revealClock.anchorValid || lastAvailableFrame == 0 )
    {
        return 0;
    }

    const double availableSeconds = static_cast<double>( lastAvailableFrame ) * PHYSICS_FIXED_DT;
    if ( availableSeconds <= 0.0 )
    {
        return 0;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds =
        (std::max)( 0.0, std::chrono::duration<double>( now - prediction.revealClock.anchor ).count() );
    const double revealSecondsPerSecond =
        prediction.revealClock.secondsPerSecond > 0.0 ? prediction.revealClock.secondsPerSecond : 1.0;
    const double revealedSeconds = (std::min)( availableSeconds, elapsedSeconds * revealSecondsPerSecond );
    const double revealFrame = revealedSeconds / static_cast<double>( PHYSICS_FIXED_DT );
    return (std::min)( lastAvailableFrame, static_cast<ReplayFrameIndex>( revealFrame ) );
}

bool PredictionRevealProgress01( const RunReplayPredictionState& prediction, float& outProgress )
{
    ReplayFrameIndex lastFrame = 0;
    if ( !ActivePredictionLastFrame( prediction, lastFrame ) )
    {
        outProgress = 0.0f;
        return false;
    }

    const ReplayFrameIndex revealFrame = PredictionRevealFrameForAdvance( prediction, lastFrame );
    // Concept: RevealAtLeast is keyed to the same frame cursor that gates the
    // visible causal overlay. It reads presentation state only; advancing a
    // director phase must never dirty prediction or mutate solver samples.
    outProgress = std::clamp( static_cast<float>( revealFrame ) / static_cast<float>( lastFrame ), 0.0f, 1.0f );
    return true;
}

bool CurrentPhaseRequestsAdvance( const DemoDirectorPlaybackState& director,
                                  const RunReplayPredictionState& prediction )
{
    if ( !IsCurrentPhaseValid( director ) )
    {
        return false;
    }

    const DemoPhase& phase = CurrentPhase( director );
    switch ( phase.advance )
    {
    case PhaseAdvance::Manual:
        return false;
    case PhaseAdvance::Timer:
        return director.phaseElapsedSeconds >= (std::max)( 0.0f, phase.timerSeconds );
    case PhaseAdvance::RevealAtLeast:
    {
        float revealProgress = 0.0f;
        return PredictionRevealProgress01( prediction, revealProgress ) &&
               revealProgress >= std::clamp( phase.revealThreshold, 0.0f, 1.0f );
    }
    }
    return false;
}

void ApplyPhaseRevealRateIfNeeded( DemoDirectorPlaybackState& director, RunReplayPredictionState& prediction )
{
    if ( !IsCurrentPhaseValid( director ) )
    {
        return;
    }

    const DemoPhase& phase = CurrentPhase( director );
    if ( PhaseRevealRateApplied( director, phase ) )
    {
        return;
    }

    const float revealRate = NormalizedRevealRate( phase.revealRate );
    // Why: changing rate mid-prediction should alter only future pacing.
    // Re-anchoring preserves already revealed prediction seconds so the causal
    // tree never snaps backward when a director phase slows the unfold.
    SetPredictionRevealRatePreservingCursor( prediction, static_cast<double>( revealRate ) );
    director.appliedRevealRatePhaseIndex = director.currentPhaseIndex;
    director.appliedRevealRate = revealRate;
    ++director.appliedRevealRateCount;
    std::printf( "[demo-director] applied reveal rate %.3f for phase %d (%s)\n",
                 static_cast<double>( revealRate ),
                 director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>" );
}

void ApplyPhaseStyleIfNeeded( DemoDirectorPlaybackState& director, SceneRuntimeStyleContext styleContext )
{
    if ( !IsCurrentPhaseValid( director ) )
    {
        return;
    }

    const DemoPhase& phase = CurrentPhase( director );
    if ( PhaseStyleAttempted( director, phase ) )
    {
        return;
    }

    RememberPhaseStyleAttempt( director, phase );
    if ( phase.stylePath[0] == '\0' )
    {
        return;
    }

    TestScene styleScene;
    const SbResult loadResult = TestScene::TryLoadStyleFromFile( phase.stylePath, styleContext.assets, styleScene );
    if ( loadResult.ok )
    {
        ApplyLiveStyleScene( styleContext, styleScene );
        ++director.appliedStyleCount;
        std::printf( "[demo-director] applied style %s for phase %d (%s)\n",
                     phase.stylePath,
                     director.currentPhaseIndex,
                     phase.name[0] ? phase.name : "<unnamed>" );
    }
    else
    {
        const char* message = loadResult.error.message[0] != '\0' ? loadResult.error.message : "style load failed";
        std::fprintf( stderr, "[demo-director] style error for %s: %s\n", phase.stylePath, message );
    }
}
} // namespace

namespace DemoDirectorPlayback
{
bool LoadShotList( RunCameraState& camera, Environment::CameraCollection& cameras, const char* path )
{
    DemoShotList loadedShotList;
    if ( !LoadDemoShotList( path, loadedShotList ) || loadedShotList.phaseCount <= 0 )
    {
        std::printf( "[demo-director] %s: no playable phases loaded\n", path && path[0] ? path : "<null-path>" );
        return false;
    }

    DemoDirectorPlaybackState nextState;
    nextState.activeShotList = loadedShotList;
    nextState.hasActiveShotList = true;
    nextState.currentPhaseIndex = 0;
    nextState.blendStartPose = CaptureCurrentPose( cameras );
    nextState.poseCapturedAtGrab = nextState.blendStartPose;
    CopyShotListPath( nextState, path );
    camera.director = nextState;

    std::printf( "[demo-director] loaded %d phase(s) from %s\n",
                 loadedShotList.phaseCount,
                 path && path[0] ? path : "<null-path>" );
    return true;
}

bool AdvancePhase( RunCameraState& camera, Environment::CameraCollection& cameras )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( !HasPlayableShotList( director ) )
    {
        return false;
    }

    int nextPhase = director.currentPhaseIndex + 1;
    if ( nextPhase >= director.activeShotList.phaseCount )
    {
        if ( !director.activeShotList.loop )
        {
            return false;
        }
        nextPhase = 0;
    }

    director.currentPhaseIndex = nextPhase;
    director.phaseElapsedSeconds = 0.0f;
    ResetPhaseEntryApplications( director );
    ResetBlendFromCurrentPose( director, cameras );
    return true;
}

void EnterMode( RunCameraState& camera, Environment::CameraCollection& cameras )
{
    DemoDirectorPlaybackState& director = camera.director;
    director.grabbed = false;
    director.phaseElapsedSeconds = 0.0f;
    ResetPhaseEntryApplications( director );
    ResetBlendFromCurrentPose( director, cameras );
    if ( director.hasActiveShotList &&
         ( director.currentPhaseIndex < 0 || director.currentPhaseIndex >= director.activeShotList.phaseCount ) )
    {
        director.currentPhaseIndex = 0;
    }
}

bool BeginGrab( RunCameraState& camera, Environment::CameraCollection& cameras )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( camera.mode != RunCameraMode::Director || director.grabbed || !HasPlayableShotList( director ) )
    {
        return false;
    }

    const DemoCameraPose pose = CaptureCurrentPose( cameras );
    director.poseCapturedAtGrab = pose;
    director.blendStartPose = pose;
    director.blendElapsedSeconds = 0.0f;
    director.grabbed = true;
    cameras.SetPrimaryPose( pose.eye, pose.view, pose.up );
    return true;
}

bool EndGrab( RunCameraState& camera, Environment::CameraCollection& cameras )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( camera.mode != RunCameraMode::Director || !director.grabbed || !HasPlayableShotList( director ) )
    {
        return false;
    }

    director.poseCapturedAtGrab = CaptureCurrentPose( cameras );
    director.blendStartPose = director.poseCapturedAtGrab;
    director.blendElapsedSeconds = 0.0f;
    director.grabbed = false;
    return true;
}

bool SetCurrentPhasePose( RunCameraState& camera, Environment::CameraCollection& cameras )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( !IsCurrentPhaseValid( director ) )
    {
        return false;
    }

    const DemoCameraPose pose = CaptureCurrentPose( cameras );
    DemoPhase& phase = CurrentPhase( director );
    phase.camera = pose;
    director.poseCapturedAtGrab = pose;
    director.blendStartPose = pose;
    director.blendElapsedSeconds = 0.0f;
    std::printf( "[demo-director] captured pose for phase %d (%s)\n",
                 director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>" );
    return true;
}

bool SetCurrentPhaseStyle( RunCameraState& camera, const char* stylePath )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( !IsCurrentPhaseValid( director ) )
    {
        return false;
    }

    DemoPhase& phase = CurrentPhase( director );
    std::snprintf( phase.stylePath, sizeof( phase.stylePath ), "%s", stylePath ? stylePath : "" );
    // Why: automation may retarget a phase's look while Director mode is already
    // active. Clearing only the style attempt lets the next tick apply the new
    // style without recounting reveal-rate application for the same phase.
    ResetPhaseStyleApplication( director );
    std::printf( "[demo-director] set style for phase %d (%s) to %s\n",
                 director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>",
                 phase.stylePath[0] ? phase.stylePath : "<empty>" );
    return true;
}

bool SelectNextPhaseForAuthoring( RunCameraState& camera, Environment::CameraCollection& cameras )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( !HasPlayableShotList( director ) )
    {
        return false;
    }

    int nextPhase = director.currentPhaseIndex + 1;
    if ( nextPhase < 0 || nextPhase >= director.activeShotList.phaseCount )
    {
        nextPhase = 0;
    }

    director.currentPhaseIndex = nextPhase;
    director.phaseElapsedSeconds = 0.0f;
    ResetPhaseEntryApplications( director );
    ResetBlendFromCurrentPose( director, cameras );
    const DemoPhase& phase = CurrentPhase( director );
    std::printf( "[demo-director] selected phase %d (%s)\n",
                 director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>" );
    return true;
}

bool SaveShotList( const RunCameraState& camera )
{
    const DemoDirectorPlaybackState& director = camera.director;
    if ( !HasPlayableShotList( director ) || !director.activeShotListPath[0] )
    {
        return false;
    }

    const bool saved = SaveDemoShotList( director.activeShotListPath, director.activeShotList );
    std::printf( "[demo-director] %s shot list to %s\n",
                 saved ? "saved" : "failed to save",
                 director.activeShotListPath );
    return saved;
}

void Tick( RunCameraState& camera,
           Environment::CameraCollection& cameras,
           RunReplayPredictionState& prediction,
           SceneRuntimeStyleContext styleContext,
           float cameraDt )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( camera.mode != RunCameraMode::Director || !HasPlayableShotList( director ) )
    {
        return;
    }

    if ( director.currentPhaseIndex < 0 || director.currentPhaseIndex >= director.activeShotList.phaseCount )
    {
        director.currentPhaseIndex = 0;
        director.phaseElapsedSeconds = 0.0f;
        ResetPhaseEntryApplications( director );
        ResetBlendFromCurrentPose( director, cameras );
    }

    // Why: Style JSON is cold phase-entry authoring data. Remembering the phase
    // and path keeps it out of the per-frame camera blend unless the phase or
    // authored style path actually changes.
    ApplyPhaseStyleIfNeeded( director, styleContext );
    ApplyPhaseRevealRateIfNeeded( director, prediction );
    if ( director.grabbed )
    {
        return;
    }

    const DemoPhase& phase = CurrentPhase( director );
    director.phaseElapsedSeconds += cameraDt;
    director.blendElapsedSeconds += cameraDt;

    const float blendAlpha = PhaseBlendAlpha( phase, director.blendElapsedSeconds );
    const DemoCameraPose pose = LerpPose( director.blendStartPose, phase.camera, blendAlpha );
    cameras.SetPrimaryPose( pose.eye, pose.view, pose.up );
    if ( CurrentPhaseRequestsAdvance( director, prediction ) )
    {
        AdvancePhase( camera, cameras );
    }
}
} // namespace DemoDirectorPlayback
} // namespace Basics
} // namespace SkullbonezCore
