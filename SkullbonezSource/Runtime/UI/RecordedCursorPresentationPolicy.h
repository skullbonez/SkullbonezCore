/*
File: SkullbonezSource/Runtime/UI/RecordedCursorPresentationPolicy.h
Purpose:
  Classifies whether one recorded-manifest pointer turn may be presented as a
  software cursor.

Summary:
  Runtime/Automation supplies detached recorded-turn facts, while App maps its
  existing interaction policy onto one cursor disposition. This pure Runtime/UI
  predicate joins those values without retaining input state or reaching the
  native cursor and capture owners.

Glossary:
  Recorded cursor: Software-drawn playback marker that does not represent or
    control the live operating-system pointer.
  Published real turn: Selected manifest turn with recorded input evidence;
    the neutral frame used only to apply a zero-turn baseline is excluded.

Invariants:
  - Visibility depends only on recorded playback and logical interaction facts.
  - The policy has no InputRouter, native window, renderer, or retained owner access.
  - Passive Replay inspection and ordinary UI capture remain cursor-bearing.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class RecordedCursorPlaybackPhase : std::uint8_t
{
    Inactive,
    NeutralBaselineFrame,
    PublishedRealTurn,
    Failed,
    Completed
};

// Concept: this is a presentation classification, not another input mode.
// App derives it from the already-routed mode, pointer policy, and interaction
// capture owner, then discards it with the frame.
enum class RecordedCursorPointerDisposition : std::uint8_t
{
    CursorBearing,
    MouseLook,
    EditorViewportLook,
    ReplayInspectionLook,
    PlacementPreview,
    CameraLookCapture,
    ToolGestureCapture
};

constexpr bool ShouldPresentRecordedCursor( RecordedCursorPlaybackPhase phase, RecordedCursorPointerDisposition disposition,
                                            bool pointerResolved, bool recordedAppFocused,
                                            bool positionInsideClient ) noexcept
{
    return phase == RecordedCursorPlaybackPhase::PublishedRealTurn &&
           disposition == RecordedCursorPointerDisposition::CursorBearing && pointerResolved && recordedAppFocused &&
           positionInsideClient;
}
} // namespace SkullbonezCore::Runtime
