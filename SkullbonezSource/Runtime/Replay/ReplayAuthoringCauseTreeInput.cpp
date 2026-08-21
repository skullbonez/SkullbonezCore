/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp
Purpose:
  Implements Replay-owned cause-window layout, pointer input, and row-selection state.

Summary:
  ReplayAuthoring retains the bounded cause-row surface and applies input
  gestures plus fixed-capacity filter editing over that lower Replay value. App
  may route an attached title hit into the same drag offsets, while Prediction
  composes row contents in its own package and this file never names Prediction
  state or scheduling.

Invariants:
  - Window placement and row selection mutate only ReplayAuthoring state.
  - Move and resize arithmetic delegates to ReplayOverlayLayout so attachment
    clamping and unit tests exercise the same anchor mutation.
  - Pointer capture is released when a cause-window drag ends or becomes unavailable.
  - Selected rows are returned as values; host-camera transitions remain in Runtime/App.
  - Filtered visible rows map back to original source indices before selection;
    keyboard focus blocks later runtime actions without taking camera ownership.

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


void ReplayAuthoring::EnsureCauseTreeWindowPlacement( int screenWidth, int screenHeight, float desiredAttachedLeftWidth,
                                                      float minimumAttachedLeftWidth ) noexcept
{
    ReplayOverlay::EnsureReplayCauseWindowPlacement( m_causeTree, screenWidth, screenHeight, desiredAttachedLeftWidth,
                                                     minimumAttachedLeftWidth );
}


void ReplayAuthoring::SetCauseTreePointer( int mouseX, int mouseY, bool blocked ) noexcept
{
    m_causeTree.mouseX = mouseX;
    m_causeTree.mouseY = mouseY;
    m_causeTree.pointerBlocked = blocked;
}


void ReplayAuthoring::MoveCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    ReplayOverlay::MoveReplayCauseWindow( m_causeTree, mouseX, mouseY, screenWidth, screenHeight );
}


void ReplayAuthoring::ResizeCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    ReplayOverlay::ResizeReplayCauseWindow( m_causeTree, mouseX, mouseY, screenWidth, screenHeight );
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
    const InputKeySnapshot& filterKeys = inputRouter.DeviceFrame().keys;
    const std::array<uint64_t, InputKeySnapshot::WORD_COUNT> previousFilterKeys = m_causeTree.filterKeysWasDown;
    m_causeTree.filterKeysWasDown = filterKeys.Words();
    const auto filterKeyPressed = [&]( int virtualKey ) noexcept
    {
        if ( virtualKey < 0 || virtualKey >= InputKeySnapshot::VIRTUAL_KEY_COUNT )
        {
            return false;
        }

        const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
        const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
        return filterKeys.IsDown( virtualKey ) && ( previousFilterKeys[word] & bit ) == 0u;
    };
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
        m_causeTree.filterFocused = false;
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

    bool filterChanged = false;
    const auto setFilter = [&]( RunReplayCauseTreeFilter filter )
    {
        if ( m_causeTree.filter != filter )
        {
            m_causeTree.filter = filter;
            filterChanged = true;
        }
    };
    const auto appendFilterChar = [&]( char value )
    { filterChanged = ReplayOverlay::AppendReplayCauseFilterCharacter( m_causeTree, value ) || filterChanged; };

    if ( m_causeTree.filterFocused )
    {
        for ( int key = 'A'; key <= 'Z'; ++key )
        {
            if ( filterKeyPressed( key ) )
            {
                appendFilterChar( static_cast<char>( 'a' + key - 'A' ) );
            }
        }

        for ( int key = '0'; key <= '9'; ++key )
        {
            if ( filterKeyPressed( key ) )
            {
                appendFilterChar( static_cast<char>( key ) );
            }
        }

        if ( filterKeyPressed( VK_SPACE ) )
        {
            appendFilterChar( ' ' );
        }

        if ( filterKeyPressed( VK_OEM_MINUS ) )
        {
            appendFilterChar( filterKeys.IsDown( VK_SHIFT ) ? '_' : '-' );
        }

        if ( filterKeyPressed( VK_OEM_PERIOD ) )
        {
            appendFilterChar( '.' );
        }

        if ( filterKeyPressed( VK_BACK ) && m_causeTree.filterText[0] != '\0' )
        {
            filterChanged = ReplayOverlay::BackspaceReplayCauseFilter( m_causeTree ) || filterChanged;
        }

        if ( filterKeyPressed( VK_DELETE ) )
        {
            filterChanged = ReplayOverlay::ClearReplayCauseFilterText( m_causeTree ) || filterChanged;
        }

        if ( filterKeyPressed( VK_ESCAPE ) )
        {
            if ( m_causeTree.filterText[0] != '\0' )
            {
                filterChanged = ReplayOverlay::ClearReplayCauseFilterText( m_causeTree ) || filterChanged;
            }
            else
            {
                m_causeTree.filterFocused = false;
            }
        }
    }

    const auto clampFilterScroll = [&]()
    {
        m_causeTree.scrollY = std::clamp( m_causeTree.scrollY, 0.0f,
                                          ReplayOverlay::ReplayCauseWindowMaxScroll( m_causeTree ) );
        ReplayOverlay::ReplayCauseWindowProjection projection;
        ReplayOverlay::BuildReplayCauseWindowProjection( m_causeTree, projection );
        const int selectedVisible = projection.VisibleRow( m_causeTree.selectedRow );

        if ( selectedVisible < 0 )
        {
            return;
        }

        const float rowTop = static_cast<float>( selectedVisible ) * ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT;
        const float rowBottom = rowTop + ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT;

        if ( rowTop < m_causeTree.scrollY )
        {
            m_causeTree.scrollY = rowTop;
        }
        else if ( rowBottom > m_causeTree.scrollY + content.h )
        {
            m_causeTree.scrollY = rowBottom - content.h;
        }
    };

    if ( filterChanged )
    {
        clampFilterScroll();
    }

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
        if ( leftPressed )
        {
            m_causeTree.filterFocused = false;
        }

        return false;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterField ) )
    {
        m_causeTree.filterFocused = true;
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayCauseTree,
                                                         InteractionExitReason::EnterReplay );
        return true;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterFunnel ) )
    {
        const RunReplayCauseTreeFilter next = m_causeTree.filter == RunReplayCauseTreeFilter::All
                                                  ? RunReplayCauseTreeFilter::Prediction
                                                  : ( m_causeTree.filter == RunReplayCauseTreeFilter::Prediction
                                                          ? RunReplayCauseTreeFilter::Contacts
                                                          : RunReplayCauseTreeFilter::All );
        setFilter( next );
        m_causeTree.filterFocused = false;
        clampFilterScroll();
        return true;
    }

    if ( leftPressed && ( isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterAll ) ||
                          isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterPrediction ) ||
                          isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterContacts ) ) )
    {
        setFilter( isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterAll )
                       ? RunReplayCauseTreeFilter::All
                       : ( isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterPrediction )
                               ? RunReplayCauseTreeFilter::Prediction
                               : RunReplayCauseTreeFilter::Contacts ) );
        m_causeTree.filterFocused = false;
        clampFilterScroll();
        return true;
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
        if ( leftPressed )
        {
            m_causeTree.filterFocused = false;
        }

        const float localY = static_cast<float>( mouse.y ) - content.y + CauseTree().scrollY;
        const int visibleRow = static_cast<int>( floorf( localY / ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        ReplayOverlay::ReplayCauseWindowProjection projection;
        ReplayOverlay::BuildReplayCauseWindowProjection( CauseTree(), projection );
        const int rowIndex = projection.SourceRow( visibleRow );
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
