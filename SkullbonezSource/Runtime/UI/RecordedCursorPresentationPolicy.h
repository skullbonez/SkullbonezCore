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
  - SkullbonezSource/Runtime/Automation/RecordedCursorFrame.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
*/
#pragma once

#include "../Automation/RecordedCursorFrame.h"

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

constexpr RecordedCursorPlaybackPhase ClassifyRecordedCursorPlaybackPhase( bool automationEnabled, bool recordedManifest,
                                                                           bool automationFailed, bool automationFinished,
                                                                           bool framePublished,
                                                                           bool publishedRealTurn ) noexcept
{
    if ( !automationEnabled || !recordedManifest )
    {
        return RecordedCursorPlaybackPhase::Inactive;
    }

    if ( automationFailed )
    {
        return RecordedCursorPlaybackPhase::Failed;
    }

    if ( automationFinished )
    {
        return RecordedCursorPlaybackPhase::Completed;
    }

    if ( !framePublished )
    {
        return RecordedCursorPlaybackPhase::Inactive;
    }

    return publishedRealTurn ? RecordedCursorPlaybackPhase::PublishedRealTurn
                             : RecordedCursorPlaybackPhase::NeutralBaselineFrame;
}

// Invariant: capture owners outrank mode labels because a tool or camera can
// retain a cursorless gesture while UI and replay modes transition around it.
constexpr RecordedCursorPointerDisposition
ClassifyRecordedCursorPointerDisposition( bool cameraLookCaptured, bool toolGestureCaptured, bool editorViewportLook,
                                          bool replayInspectionLook, bool placementPreview, bool mouseLook ) noexcept
{
    if ( cameraLookCaptured )
    {
        return RecordedCursorPointerDisposition::CameraLookCapture;
    }

    if ( toolGestureCaptured )
    {
        return RecordedCursorPointerDisposition::ToolGestureCapture;
    }

    if ( editorViewportLook )
    {
        return RecordedCursorPointerDisposition::EditorViewportLook;
    }

    if ( replayInspectionLook )
    {
        return RecordedCursorPointerDisposition::ReplayInspectionLook;
    }

    if ( placementPreview )
    {
        return RecordedCursorPointerDisposition::PlacementPreview;
    }

    return mouseLook ? RecordedCursorPointerDisposition::MouseLook : RecordedCursorPointerDisposition::CursorBearing;
}

constexpr bool ShouldPresentRecordedCursor( RecordedCursorPlaybackPhase phase, RecordedCursorPointerDisposition disposition,
                                            bool pointerResolved, bool recordedAppFocused,
                                            bool positionInsideClient ) noexcept
{
    return phase == RecordedCursorPlaybackPhase::PublishedRealTurn &&
           disposition == RecordedCursorPointerDisposition::CursorBearing && pointerResolved && recordedAppFocused &&
           positionInsideClient;
}

constexpr RecordedCursorFrame FilterRecordedCursorFrame( const RecordedCursorFrame& frame, RecordedCursorPlaybackPhase phase,
                                                         RecordedCursorPointerDisposition disposition, int clientWidth,
                                                         int clientHeight, bool sceneReplaced, bool frameFailed ) noexcept
{
    const bool positionInsideClient = frame.pointerResolved && clientWidth > 0 && clientHeight > 0 && frame.clientX >= 0 &&
                                      frame.clientY >= 0 && frame.clientX < clientWidth && frame.clientY < clientHeight;

    if ( sceneReplaced || frameFailed || !frame.publishedRealTurn ||
         !ShouldPresentRecordedCursor( phase, disposition, frame.pointerResolved, frame.recordedAppFocused,
                                       positionInsideClient ) )
    {
        return {};
    }

    return frame;
}
} // namespace SkullbonezCore::Runtime
