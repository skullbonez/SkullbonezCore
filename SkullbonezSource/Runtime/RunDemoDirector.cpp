/*
File: SkullbonezSource/Runtime/RunDemoDirector.cpp
Purpose:
  Applies Demo Director shot-list camera poses during Director camera mode.

Mental model:
  The director never invents framing. A loaded shot list owns authored poses,
  RunCameraState owns playback timers, and this file blends the selected phase
  into CameraCollection before the render view matrix is built.

Glossary:
  Phase pose: Authored eye, view target, and up vector stored in `.shot.json`.
  Blend start pose: Camera pose captured when a phase transition begins.
  Director advance: Manual phase step used for early automation proof before
    timer/reveal advance rules are wired.

Invariants:
  - Director playback is presentation-only; it must not mutate physics or scene
    object state.
  - Camera writes go through CameraCollection::SetPrimaryPose so render camera,
    listener, replay, and screenshot paths keep using the normal camera owner.
  - Empty or missing shot lists leave Director mode as a no-op.

Related:
  - SkullbonezSource/Runtime/RunDemoDirector.h
  - SkullbonezSource/Runtime/DemoDirector.h
  - SkullbonezSource/Runtime/RunState.h
  - fable_plans/08-demo-director-progress.md
*/
#include "RunDemoDirector.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

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

DemoCameraPose CaptureCurrentPose( const RunSubsystemState& systems )
{
    DemoCameraPose pose;
    if ( !systems.cameras )
    {
        return pose;
    }

    pose.eye = systems.cameras->GetCameraTranslation();
    pose.view = systems.cameras->GetCameraView();
    pose.up = systems.cameras->GetCameraUp();
    return pose;
}

void ResetBlendFromCurrentPose( DemoDirectorPlaybackState& director, const RunSubsystemState& systems )
{
    director.blendStartPose = CaptureCurrentPose( systems );
    director.blendElapsedSeconds = 0.0f;
}
} // namespace

namespace DemoDirectorPlayback
{
bool LoadShotList( RunCameraState& camera, const RunSubsystemState& systems, const char* path )
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
    nextState.blendStartPose = CaptureCurrentPose( systems );
    nextState.poseCapturedAtGrab = nextState.blendStartPose;
    camera.director = nextState;

    std::printf( "[demo-director] loaded %d phase(s) from %s\n",
                 loadedShotList.phaseCount,
                 path && path[0] ? path : "<null-path>" );
    return true;
}

bool AdvancePhase( RunCameraState& camera, const RunSubsystemState& systems )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( !director.hasActiveShotList || director.activeShotList.phaseCount <= 0 )
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
    ResetBlendFromCurrentPose( director, systems );
    return true;
}

void EnterMode( RunCameraState& camera, const RunSubsystemState& systems )
{
    DemoDirectorPlaybackState& director = camera.director;
    director.grabbed = false;
    director.phaseElapsedSeconds = 0.0f;
    ResetBlendFromCurrentPose( director, systems );
    if ( director.hasActiveShotList &&
         ( director.currentPhaseIndex < 0 || director.currentPhaseIndex >= director.activeShotList.phaseCount ) )
    {
        director.currentPhaseIndex = 0;
    }
}

void Tick( RunCameraState& camera, const RunSubsystemState& systems, float cameraDt )
{
    DemoDirectorPlaybackState& director = camera.director;
    if ( camera.mode != RunCameraMode::Director || director.grabbed || !director.hasActiveShotList ||
         director.activeShotList.phaseCount <= 0 || !systems.cameras )
    {
        return;
    }

    if ( director.currentPhaseIndex < 0 || director.currentPhaseIndex >= director.activeShotList.phaseCount )
    {
        director.currentPhaseIndex = 0;
        director.phaseElapsedSeconds = 0.0f;
        ResetBlendFromCurrentPose( director, systems );
    }

    const DemoPhase& phase = director.activeShotList.phases[static_cast<std::size_t>( director.currentPhaseIndex )];
    director.phaseElapsedSeconds += cameraDt;
    director.blendElapsedSeconds += cameraDt;

    const float blendAlpha = PhaseBlendAlpha( phase, director.blendElapsedSeconds );
    const DemoCameraPose pose = LerpPose( director.blendStartPose, phase.camera, blendAlpha );
    systems.cameras->SetPrimaryPose( pose.eye, pose.view, pose.up );
}
} // namespace DemoDirectorPlayback
} // namespace Basics
} // namespace SkullbonezCore
