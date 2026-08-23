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


ReplayCauseTreeInputResult ReplayAuthoring::TickCauseTreeInput( ReplayPresentation& presentationOwner,
                                                                ReplayScrubber& scrubberOwner,
                                                                const ReplayCauseTreeInputFrame& frame )
{
    ReplayCauseTreeInputResult result;
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );

    // Concept: this phase owns only the explanatory window and reports row or
    // exit actions. ReplayRuntime resolves a selected row from current stores
    // before performing any host-camera transition.
    const bool leftPressed = frame.leftPressed;
    const bool leftReleased = frame.leftReleased;
    BeginCauseTreeInputFrame();
    m_causeTree.filterKeysWasDown = frame.currentFilterKeys;
    const int screenW = frame.screenWidth;
    const int screenH = frame.screenHeight;
    const auto causeTreeDragMode = [&]()
    {
        return frame.gesture.kind == ReplayToolGestureKind::CauseTreeDrag ? frame.gesture.axis : -1;
    };

    const auto endCauseTreeDragIfReleased = [&]()
    {
        if ( leftReleased && causeTreeDragMode() >= 0 )
        {
            result.interaction.releaseNativeCapture = true;
            result.interaction.endGesture = true;
        }
    };

    if ( frame.editorModeEnabled || screenW <= 0 || screenH <= 0 )
    {
        endCauseTreeDragIfReleased();
        return result;
    }

    if ( !frame.rowsReady )
    {
        m_causeTree.filterFocused = false;
        endCauseTreeDragIfReleased();
        return result;
    }

    EnsureCauseTreeWindowPlacement( screenW, screenH );
    if ( !frame.hasClientPosition )
    {
        endCauseTreeDragIfReleased();
        return result;
    }

    const int mouseX = frame.mouseX;
    const int mouseY = frame.mouseY;

    SetCauseTreePointer( mouseX, mouseY, frame.uiBlocksMouse );
    ReplayOverlay::ReplayCauseWindowSurface surface;
    ReplayOverlay::BuildReplayCauseWindowSurface( CauseTree(), surface );
    surface.ResolvePointer( mouseX, mouseY, frame.uiBlocksMouse );
    const auto isHotControl = [&]( ReplayOverlay::ReplayCauseWindowControl control )
    { return surface.hasHotControl && surface.hotControl == ReplayOverlay::ReplayCauseWindowControlId( control ); };

    const ReplayOverlay::ReplayOverlayControl* contentControl =
        surface.Find( ReplayOverlay::ReplayCauseWindowControlId( ReplayOverlay::ReplayCauseWindowControl::Content ) );

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
        const std::size_t characterCount = (std::min)( frame.filterCharacterCount,
                                                       frame.filterCharacters.size() );

        for ( std::size_t characterIndex = 0; characterIndex < characterCount; ++characterIndex )
        {
            appendFilterChar( frame.filterCharacters[characterIndex] );
        }

        if ( frame.filterBackspacePressed && m_causeTree.filterText[0] != '\0' )
        {
            filterChanged = ReplayOverlay::BackspaceReplayCauseFilter( m_causeTree ) || filterChanged;
        }

        if ( frame.filterDeletePressed )
        {
            filterChanged = ReplayOverlay::ClearReplayCauseFilterText( m_causeTree ) || filterChanged;
        }

        if ( frame.filterEscapePressed || frame.filterReturnPressed )
        {
            if ( frame.filterEscapePressed && m_causeTree.filterText[0] != '\0' )
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
        MoveCauseTreeWindow( mouseX, mouseY, screenW, screenH );

        if ( leftReleased )
        {
            result.interaction.releaseNativeCapture = true;
            result.interaction.endGesture = true;
        }

        result.consumesMouse = true;
        return result;
    }

    if ( causeTreeDragMode() == 1 )
    {
        ResizeCauseTreeWindow( mouseX, mouseY, screenW, screenH );

        if ( leftReleased )
        {
            result.interaction.releaseNativeCapture = true;
            result.interaction.endGesture = true;
        }

        result.consumesMouse = true;
        return result;
    }

    if ( frame.uiBlocksMouse || !surface.consumesPointer )
    {
        if ( leftPressed )
        {
            m_causeTree.filterFocused = false;
        }

        return result;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::FilterField ) )
    {
        m_causeTree.filterFocused = true;
        result.interaction.worldOwner = ReplayWorldOwnerRequest::CauseTree;
        result.consumesMouse = true;
        return result;
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
        result.consumesMouse = true;
        return result;
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
        result.consumesMouse = true;
        return result;
    }

    if ( frame.wheelDelta != 0 )
    {
        result.interaction.worldOwner = ReplayWorldOwnerRequest::CauseTree;
        const float wheelRows = static_cast<float>( frame.wheelDelta ) / 120.0f;
        ScrollCauseTreeWindow( -wheelRows * ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f, screenW, screenH );
        result.consumesMouse = true;
        return result;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::Resize ) )
    {
        BeginCauseTreeResize( mouseX, mouseY );
        result.interaction.beginGesture = ReplayToolGestureKind::CauseTreeDrag;
        result.interaction.gestureStartX = mouseX;
        result.interaction.gestureStartY = mouseY;
        result.interaction.gestureAxis = 1;
        result.interaction.requestNativeCapture = true;
        result.consumesMouse = true;
        return result;
    }

    if ( leftPressed && isHotControl( ReplayOverlay::ReplayCauseWindowControl::Title ) )
    {
        BeginCauseTreeMove( mouseX, mouseY );
        result.interaction.beginGesture = ReplayToolGestureKind::CauseTreeDrag;
        result.interaction.gestureStartX = mouseX;
        result.interaction.gestureStartY = mouseY;
        result.interaction.gestureAxis = 0;
        result.interaction.requestNativeCapture = true;
        result.consumesMouse = true;
        return result;
    }

    if ( isHotControl( ReplayOverlay::ReplayCauseWindowControl::Content ) )
    {
        if ( leftPressed )
        {
            m_causeTree.filterFocused = false;
        }

        const float localY = static_cast<float>( mouseY ) - content.y + CauseTree().scrollY;
        const int visibleRow = static_cast<int>( floorf( localY / ReplayOverlay::REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        ReplayOverlay::ReplayCauseWindowProjection projection;
        ReplayOverlay::BuildReplayCauseWindowProjection( CauseTree(), projection );
        const int rowIndex = projection.SourceRow( visibleRow );
        RunReplayCauseTreeRow selectedRow;

        if ( TryGetCauseTreeRow( rowIndex, selectedRow ) )
        {
            if ( leftPressed )
            {
                result.interaction.worldOwner = ReplayWorldOwnerRequest::CauseTree;
                result.focusRow = rowIndex;
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
            result.exitInspectionCamera = true;
        }
    }

    result.consumesMouse = true;
    return result;
}
