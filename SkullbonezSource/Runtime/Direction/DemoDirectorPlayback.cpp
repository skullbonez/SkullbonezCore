/*
File: SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp
Purpose:
  Applies Demo Director shot-list camera poses, phase styles, and reveal pacing.

Summary:
  The director never invents framing or looks. A loaded shot list owns authored
  camera/style phases; this owner advances playback state and publishes bounded
  commands that App applies to camera, scene-style, and replay presentation owners.

Glossary:
  Phase pose: Authored eye, view target, and up vector stored in `.shot.json`.
  Blend start pose: Camera pose captured when a phase transition begins.
  Grab: Temporary operator ownership of the camera while Director mode remains
    selected.
  Director advance: Manual, timer, or reveal-synced rule that selects the next
    authored phase without changing simulation ownership.
  Phase style: Optional `.style.json` applied through SceneCinematicPolicy when a
    phase becomes active.
  Reveal rate: Authored multiplier for prediction seconds revealed per real
    second while this phase is active.

Invariants:
  - Director playback is presentation-only; it must not mutate physics state.
  - Direction emits camera/style values and never borrows their mutable owners.
  - Reveal-rate writes only affect replay overlay presentation timing; they
    must not mark prediction dirty or rebuild private physics state.
  - Empty or missing shot lists leave Director mode as a no-op.

Related:
  - SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h
  - SkullbonezSource/Runtime/Camera/DemoDirector.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "DemoDirectorPlayback.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
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

void ResetBlendFromCurrentPose( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose )
{
    director.blendStartPose = currentPose;
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
    return director.appliedRevealRatePhaseIndex == director.currentPhaseIndex && director.appliedRevealRate == revealRate;
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

bool CurrentPhaseRequestsAdvance( const DemoDirectorPlaybackState& director, DemoDirectorPredictionView prediction )
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

        // Concept: RevealAtLeast consumes a value sampled from the prediction
        // owner. Director never borrows or mutates the reveal clock itself.
        return prediction.revealAvailable && prediction.revealProgress >= std::clamp( phase.revealThreshold, 0.0f, 1.0f );
    }

    return false;
}

void ApplyPhaseRevealRateIfNeeded( DemoDirectorPlaybackState& director, DemoDirectorTickResult& result )
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
    result.requestedRevealRate = revealRate;
    result.applyRevealRate = true;
    director.appliedRevealRatePhaseIndex = director.currentPhaseIndex;
    director.appliedRevealRate = revealRate;
    ++director.appliedRevealRateCount;
    std::printf( "[demo-director] applied reveal rate %.3f for phase %d (%s)\n", static_cast<double>( revealRate ),
                 director.currentPhaseIndex, phase.name[0] ? phase.name : "<unnamed>" );
}

void PublishPhaseStyleIfNeeded( DemoDirectorPlaybackState& director, DemoDirectorTickResult& result )
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

    std::snprintf( result.stylePath, sizeof( result.stylePath ), "%s", phase.stylePath );
    result.applyStyle = true;
}
} // namespace

namespace DemoDirectorPlayback
{

bool LoadShotList( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose, const char* path )
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
    nextState.blendStartPose = currentPose;
    nextState.poseCapturedAtGrab = nextState.blendStartPose;
    CopyShotListPath( nextState, path );
    director = nextState;

    std::printf( "[demo-director] loaded %d phase(s) from %s\n", loadedShotList.phaseCount,
                 path && path[0] ? path : "<null-path>" );

    return true;
}


bool AdvancePhase( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose )
{
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
    ResetBlendFromCurrentPose( director, currentPose );
    return true;
}

void EnterMode( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose )
{
    director.grabbed = false;
    director.phaseElapsedSeconds = 0.0f;
    ResetPhaseEntryApplications( director );
    ResetBlendFromCurrentPose( director, currentPose );

    if ( director.hasActiveShotList &&
         ( director.currentPhaseIndex < 0 || director.currentPhaseIndex >= director.activeShotList.phaseCount ) )
    {
        director.currentPhaseIndex = 0;
    }
}

bool BeginGrab( DemoDirectorPlaybackState& director, bool directorModeActive, const DemoCameraPose& currentPose,
                DemoDirectorCameraCommand& outCommand )
{
    outCommand = DemoDirectorCameraCommand {};

    if ( !directorModeActive || director.grabbed || !HasPlayableShotList( director ) )
    {
        return false;
    }

    director.poseCapturedAtGrab = currentPose;
    director.blendStartPose = currentPose;
    director.blendElapsedSeconds = 0.0f;
    director.grabbed = true;
    outCommand.pose = currentPose;
    outCommand.applyPose = true;
    return true;
}

bool EndGrab( DemoDirectorPlaybackState& director, bool directorModeActive, const DemoCameraPose& currentPose )
{
    if ( !directorModeActive || !director.grabbed || !HasPlayableShotList( director ) )
    {
        return false;
    }

    director.poseCapturedAtGrab = currentPose;
    director.blendStartPose = director.poseCapturedAtGrab;
    director.blendElapsedSeconds = 0.0f;
    director.grabbed = false;
    return true;
}

bool SetCurrentPhasePose( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose )
{
    if ( !IsCurrentPhaseValid( director ) )
    {
        return false;
    }

    DemoPhase& phase = CurrentPhase( director );
    phase.camera = currentPose;
    director.poseCapturedAtGrab = currentPose;
    director.blendStartPose = currentPose;
    director.blendElapsedSeconds = 0.0f;
    std::printf( "[demo-director] captured pose for phase %d (%s)\n", director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>" );

    return true;
}


bool SetCurrentPhaseStyle( DemoDirectorPlaybackState& director, const char* stylePath )
{
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
    std::printf( "[demo-director] set style for phase %d (%s) to %s\n", director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>", phase.stylePath[0] ? phase.stylePath : "<empty>" );

    return true;
}


bool SelectNextPhaseForAuthoring( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose )
{
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
    ResetBlendFromCurrentPose( director, currentPose );
    const DemoPhase& phase = CurrentPhase( director );
    std::printf( "[demo-director] selected phase %d (%s)\n", director.currentPhaseIndex,
                 phase.name[0] ? phase.name : "<unnamed>" );

    return true;
}

bool SaveShotList( const DemoDirectorPlaybackState& director )
{
    if ( !HasPlayableShotList( director ) || !director.activeShotListPath[0] )
    {
        return false;
    }

    const bool saved = SaveDemoShotList( director.activeShotListPath, director.activeShotList );
    std::printf( "[demo-director] %s shot list to %s\n", saved ? "saved" : "failed to save", director.activeShotListPath );

    return saved;
}

DemoDirectorTickResult Tick( DemoDirectorPlaybackState& director, bool directorModeActive,
                             DemoDirectorPredictionView prediction, const DemoCameraPose& currentPose, float cameraDt )
{
    DemoDirectorTickResult result;

    if ( !directorModeActive || !HasPlayableShotList( director ) )
    {
        return result;
    }

    if ( director.currentPhaseIndex < 0 || director.currentPhaseIndex >= director.activeShotList.phaseCount )
    {
        director.currentPhaseIndex = 0;
        director.phaseElapsedSeconds = 0.0f;
        ResetPhaseEntryApplications( director );
        ResetBlendFromCurrentPose( director, currentPose );
    }

    // Why: Style JSON is cold phase-entry authoring data. Remembering the phase
    // and path keeps it out of the per-frame camera blend unless the phase or
    // authored style path actually changes.
    PublishPhaseStyleIfNeeded( director, result );

    ApplyPhaseRevealRateIfNeeded( director, result );

    if ( director.grabbed )
    {
        return result;
    }

    const DemoPhase& phase = CurrentPhase( director );
    director.phaseElapsedSeconds += cameraDt;
    director.blendElapsedSeconds += cameraDt;

    const float blendAlpha = PhaseBlendAlpha( phase, director.blendElapsedSeconds );
    result.cameraPose = LerpPose( director.blendStartPose, phase.camera, blendAlpha );
    result.applyCameraPose = true;

    if ( CurrentPhaseRequestsAdvance( director, prediction ) )
    {
        AdvancePhase( director, result.cameraPose );
    }

    return result;
}

void CompleteStyleApplication( DemoDirectorPlaybackState& director, bool succeeded, const char* errorMessage )
{
    if ( succeeded )
    {
        ++director.appliedStyleCount;
        std::printf( "[demo-director] applied style %s for phase %d\n", director.appliedStylePath,
                     director.currentPhaseIndex );
        return;
    }

    const char* message = errorMessage && errorMessage[0] != '\0' ? errorMessage : "style load failed";
    std::fprintf( stderr, "[demo-director] style error for %s: %s\n", director.appliedStylePath, message );
}
} // namespace DemoDirectorPlayback
} // namespace Runtime
} // namespace SkullbonezCore
