/*
File: SkullbonezSource/Runtime/RuntimeCameraMode.h
Purpose:
  Defines the operator camera/workspace mode shared by runtime subsystems.

Mental model:
  Camera mode is operator intent, not direct camera math. Runtime input,
  launcher, editor, replay, and UI code read this enum to decide which owner may
  consume camera gestures during a frame.

Glossary:
  Demo: Authored scene camera mode used for unattended playback.
  Director: Authored shot-list camera mode used by demo phase playback.
  Inspect: Free camera mode for operator navigation.
  Attach: Camera mode that follows or orbits a selected model.
  Manipulator: Runtime tool mode that lets the operator pick up dynamic bodies.

Invariants:
  - Count is a sentinel and must not be treated as a real camera mode.
  - Enum order is user-facing cycling order; changes affect keyboard/UI mode
    transitions.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/RunCameraState.h
  - SkullbonezSource/Runtime/RuntimeInteractionController.h
*/
#pragma once

namespace SkullbonezCore::Basics
{
enum class RunCameraMode
{
    Demo = 0,
    Scene,
    Inspect,
    Attach,
    Launcher,
    Manipulator,
    Director,
    Count
};

// Concept: pure mode predicates live beside the enum. Runtime owners still pass
// their active follow/grab state when a mode's input ownership depends on it.
inline bool RunCameraModeUsesFlyControls( RunCameraMode mode, bool attachActiveFollow, bool directorGrabbed )
{
    return mode == RunCameraMode::Inspect || mode == RunCameraMode::Launcher || mode == RunCameraMode::Manipulator ||
           ( mode == RunCameraMode::Attach && attachActiveFollow ) ||
           ( mode == RunCameraMode::Director && directorGrabbed );
}

inline bool RunCameraModeUsesManualControls( RunCameraMode mode, bool attachActiveFollow, bool directorGrabbed )
{
    return RunCameraModeUsesFlyControls( mode, attachActiveFollow, directorGrabbed ) || mode == RunCameraMode::Attach ||
           mode == RunCameraMode::Director;
}

inline bool RunCameraModeUsesLauncher( RunCameraMode mode )
{
    return mode == RunCameraMode::Launcher;
}

inline bool RunCameraModeIsManipulator( RunCameraMode mode )
{
    return mode == RunCameraMode::Manipulator;
}

inline bool RunCameraModeIsAttached( RunCameraMode mode )
{
    return mode == RunCameraMode::Attach;
}
} // namespace SkullbonezCore::Basics
