/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp
Purpose:
  Implements Replay-owned cause-window layout, pointer input, and row-selection state.

Summary:
  ReplayAuthoring retains the bounded cause-row surface and applies input
  gestures over that lower Replay value. Prediction composes row contents in
  its own package, while this file never names Prediction state or scheduling.

Invariants:
  - Window placement and row selection mutate only ReplayAuthoring state.
  - Pointer capture is released when a cause-window drag ends or becomes unavailable.
  - Selected rows are returned as values; host-camera transitions remain in Runtime/App.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayAuthoring.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
  - SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayAuthoring.h"

#include "ReplayOverlayLayout.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"

#include <cmath>

using namespace SkullbonezCore::Runtime;

void ReplayAuthoring::BeginCauseTreeInputFrame() noexcept
{
    m_causeTree.pointerBlocked = true;
}


void ReplayAuthoring::EnsureCauseTreeWindowPlacement( int screenWidth, int screenHeight ) noexcept
{
    ReplayOverlay::EnsureReplayCauseWindowPlacement( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::SetCauseTreePointer( int mouseX, int mouseY, bool blocked ) noexcept
{
    m_causeTree.mouseX = mouseX;
    m_causeTree.mouseY = mouseY;
    m_causeTree.pointerBlocked = blocked;
}


void ReplayAuthoring::MoveCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.x = mouseX - m_causeTree.dragOffsetX;
    m_causeTree.y = mouseY - m_causeTree.dragOffsetY;
    ReplayOverlay::ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::ResizeCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.width = m_causeTree.resizeStartWidth + ( mouseX - m_causeTree.resizeStartMouseX );
    m_causeTree.height = m_causeTree.resizeStartHeight + ( mouseY - m_causeTree.resizeStartMouseY );
    ReplayOverlay::ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::ScrollCauseTreeWindow( float delta, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.scrollY += delta;
    ReplayOverlay::ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::BeginCauseTreeResize( int mouseX, int mouseY ) noexcept
{
    m_causeTree.resizeStartMouseX = mouseX;
    m_causeTree.resizeStartMouseY = mouseY;
    m_causeTree.resizeStartWidth = m_causeTree.width;
    m_causeTree.resizeStartHeight = m_causeTree.height;
}


void ReplayAuthoring::BeginCauseTreeMove( int mouseX, int mouseY ) noexcept
{
    m_causeTree.dragOffsetX = mouseX - m_causeTree.x;
    m_causeTree.dragOffsetY = mouseY - m_causeTree.y;
}


bool ReplayAuthoring::TryGetCauseTreeRow( int rowIndex, RunReplayCauseTreeRow& outRow ) const noexcept
{
    if ( rowIndex < 0 || rowIndex >= static_cast<int>( m_causeTree.rows.size() ) )
    {
        return false;
    }

    outRow = m_causeTree.rows[static_cast<std::size_t>( rowIndex )];
    return true;
}


void ReplayAuthoring::SetCauseTreeFocus( int rowIndex, Physics::PhysicsSceneObjectId focusedId ) noexcept
{
    m_causeTree.selectedRow = rowIndex;
    m_causeTree.focusedId = focusedId;
}


bool ReplayAuthoring::TickCauseTreeInput( ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner,
                                          InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                          bool rowsReady, bool uiBlocksMouse, int wheelDelta, bool editorModeEnabled,
                                          int screenWidth, int screenHeight, int& outFocusRow,
                                          bool& outExitInspectionCamera )
{
    outFocusRow = -1;
    outExitInspectionCamera = false;
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );

    // Concept: this phase owns only the explanatory window and reports row or
    // exit actions. ReplayRuntime resolves a selected row from current stores
    // before performing any host-camera transition.
    const RuntimeMouseEdges& pointer = inputRouter.UiSnapshot().mouse;
    const bool leftPressed = pointer.leftPressed;
    const bool leftReleased = pointer.leftReleased;
    BeginCauseTreeInputFrame();
    const int screenW = screenWidth;
    const int screenH = screenHeight;
    const auto causeTreeDragMode = [&]()
    {
        const RuntimeInteractionGesture& gesture = interaction.Gesture();

        return gesture.kind == RuntimeInteractionGestureKind::ReplayCauseTreeDrag ? gesture.axis : -1;
    };

    const auto endCauseTreeDragIfReleased = [&]()
    {
        if ( leftReleased && causeTreeDragMode() >= 0 )
        {
            inputRouter.ReleaseNativeCapture();

            interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
    };

    if ( editorModeEnabled || screenW <= 0 || screenH <= 0 )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    if ( !rowsReady )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    EnsureCauseTreeWindowPlacement( screenW, screenH );
    const RuntimePointerEvent& runtimePointer = inputRouter.RuntimeSnapshot().pointer;

    if ( !runtimePointer.hasClientPosition )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    const POINT mouse { runtimePointer.clientX, runtimePointer.clientY };

    SetCauseTreePointer( mouse.x, mouse.y, uiBlocksMouse );
    ReplayOverlay::ReplayCauseWindowSurface surface;
    ReplayOverlay::BuildReplayCauseWindowSurface( CauseTree(), surface );
    surface.ResolvePointer( mouse.x, mouse.y, uiBlocksMouse );
    const auto isHotControl = [&]( ReplayOverlay::ReplayCauseWindowControl control )
    { return surface.hasHotControl && surface.hotControl == ReplayOverlay::ReplayCauseWindowControlId( control ); };

    const RuntimeUiControl* contentControl = surface.Find( ReplayOverlay::ReplayCauseWindowControlId( ReplayOverlay::ReplayCauseWindowControl::Content ) );

    if ( !contentControl )
    {
        SB_FATAL( "ReplayCauseWindowSurface", "Content control is missing from the cause-window surface." );
    }

    const UI::UIRect content = contentControl->drawRect;

    if ( causeTreeDragMode() == 0 )
    {
        MoveCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );

        if ( leftReleased )
        {
            inputRouter.ReleaseNativeCapture();
            interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }

        return true;
    }

    if ( causeTreeDragMode() == 1 )
    {
        ResizeCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );

        if ( leftReleased )
        {
            inputRouter.ReleaseNativeCapture();
            interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }

        return true;
    }

    if ( uiBlocksMouse || !surface.consumesPointer )
    {
        return false;
    }

    if ( wheelDelta != 0 )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayCauseTree,
                                                         InteractionExitReason::EnterReplay );

        const float wheelRows = static_cast<float>( wheelDelta ) / 120.0f;
        ScrollCauseTreeWindow( -wheelRows * ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f, screenW, screenH );
        return true;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::Resize ) )
    {
        RuntimeInteractionGesture gesture;
        gesture.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
        gesture.button = RuntimePointerButton::Left;
        gesture.startX = mouse.x;
        gesture.startY = mouse.y;
        gesture.axis = 1;

        if ( !interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayCauseTree,
                                                 gesture ) )
        {
            return false;
        }

        BeginCauseTreeResize( mouse.x, mouse.y );
        inputRouter.RequestNativeCapture();
        return true;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::Title ) )
    {
        RuntimeInteractionGesture gesture;
        gesture.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
        gesture.button = RuntimePointerButton::Left;
        gesture.startX = mouse.x;
        gesture.startY = mouse.y;
        gesture.axis = 0;

        if ( !interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayCauseTree,
                                                 gesture ) )
        {
            return false;
        }

        BeginCauseTreeMove( mouse.x, mouse.y );
        inputRouter.RequestNativeCapture();
        return true;
    }

    if ( isHotControl( ReplayOverlay::ReplayCauseWindowControl::Content ) )
    {
        const float localY = static_cast<float>( mouse.y ) - content.y + CauseTree().scrollY;
        const int rowIndex = static_cast<int>( floorf( localY / ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        RunReplayCauseTreeRow selectedRow;

        if ( TryGetCauseTreeRow( rowIndex, selectedRow ) )
        {
            if ( leftPressed )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayCauseTree,
                                                                 InteractionExitReason::EnterReplay );

                outFocusRow = rowIndex;
            }
        }
        else if ( leftPressed )
        {
            const bool ownedSimulationPause = presentationOwner.ClearCameraFocus();
            ClearCauseTreeFocus();
            const ReplayScrubberView scrubber = scrubberOwner.View();

            if ( ownedSimulationPause && scrubber.liveAdvanceHeld && !scrubber.historicalSamplePaused )
            {
                scrubberOwner.SetLiveAdvanceHeld( false );
            }

            presentationOwner.ClearPathState();
            ResetCauseTreeRows();
            QueuePredictionCacheReset();
            outExitInspectionCamera = true;
        }
    }

    return true;
}
