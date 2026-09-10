/*
File: SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.cpp
Purpose:
  Implements stateful in-game UI window and widget interaction ownership.

Summary:
  This owner translates detached UI input snapshots into typed command values
  and retains the window, widget, tab, and gesture state shared with drawing.
  It also maps recording anchors through window-local normalized coordinates so
  playback follows the same UI region after layout resize. The owner also
  records its widget state directly; no complete reference surface crosses out.

Invariants:
  - Device input produces commands; runtime subsystem mutation remains outside UI.
  - Private draw operations access retained state directly and never publish reference-view bags.
  - Layout bounds and hit-test bounds are the same widget state.
  - Anchor resolution uses current window bounds and never retains a viewport pointer.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.h
  - SkullbonezSource/Runtime/UI/GameUI/UI.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "UI.h"
#include "UIWindowInteractionOwner.h"
#include "../../../Core/Profiler.h"
#include "UIFrameComposition.h"
#include "../../../UI/UIInput.h"
#include "../../../UI/UILayout.h"
#include "../../../UI/UIDrawWidgets.h"
#include "../../../UI/UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::FrameComposition;

namespace
{
// Concept: input snapshots retain the Win32 wheel-unit contract as a plain
// value; UI policy must not recover it through an incidental renderer include.
constexpr float UI_MOUSE_WHEEL_DELTA = 120.0f;
} // namespace

UIWindowInteractionOwner::UIWindowInteractionOwner() : m_activeTab( InGameUITab::Scene )
{
}

void UIWindowInteractionOwner::UpdatePresentationInput( const InputControl::UIInputSnapshot& input, int width, int height,
                                                        bool enabled )
{
    m_presentationEnabled = enabled;
    m_lastScreenW = (std::max)( 1, width );
    m_lastScreenH = (std::max)( 1, height );
    m_editorTab.objectCombo.SetScrollable( enabled );
    m_presentationPointerBlocked = false;
    m_presentationHeaderHovered = false;
    if ( !enabled )
    {
        m_profilerTab.histogramDockedBounds = {};
        m_memoryOverlay.dockedBounds = {};
        m_presentationRects = ComputePresentationRects( PresentationState {}, width, height );
        return;
    }

    UpdateToolsVisibility();
    m_presentationRects = ComputePresentationRects( m_presentation, width, height );
    if ( ProfilerTab::PerformanceHistogramContains( m_profilerTab, input.mouseX, input.mouseY ) )
    {
        // Floating diagnostics receive their own pointer input before the dock
        // or Tools surface underneath them can select a control.
        m_presentationPointerBlocked = true;
        return;
    }
    const HeaderRects header = ComputeHeaderRects( m_presentationRects.header, m_presentation.workspace );
    const bool headerHovered = m_presentationRects.header.Contains( input.mouseX, input.mouseY );
    m_presentationHeaderHovered = headerHovered;
    if ( input.leftPressed && !input.rightDown && !input.middleDown && header.close.Contains( input.mouseX, input.mouseY ) )
    {
        ReturnToGame();
        m_presentationPointerBlocked = true;
        return;
    }
    // Invariant: a captured world/tool gesture may cross chrome without
    // becoming a shell click. Only the router's new press edge selects layout.
    if ( input.leftPressed && !input.rightDown && !input.middleDown && headerHovered && !HasOpenPopup() )
    {
        if ( header.layout.Contains( input.mouseX, input.mouseY ) )
        {
            if ( m_presentation.preferences.layout == LayoutMode::Editor )
            {
                SetVisible( false, 0.0 );
                m_presentation.detailsOpen = false;
                m_presentation.toolsOpen = false;
                m_presentation.preferences.layout = LayoutMode::Canvas;
            }
            else
            {
                m_presentation.preferences.layout = LayoutMode::Editor;
            }
            m_tooltip.Dismiss();
        }
        else if ( header.workspace.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.workspace = m_presentation.workspace == Workspace::Scene ? Workspace::SolverLab
                                                                                    : Workspace::Scene;
            m_tooltip.Dismiss();
        }
        else if ( header.scene.Contains( input.mouseX, input.mouseY ) ||
                  header.scenes.Contains( input.mouseX, input.mouseY ) )
        {
            SetActiveTab( InGameUITab::Scene );
            SetVisible( true, 0.0 );
            m_presentation.preferences.layout = LayoutMode::Editor;
            m_window.animationActive = false;
        }
        else if ( header.skull.Contains( input.mouseX, input.mouseY ) ||
                  header.tools.Contains( input.mouseX, input.mouseY ) )
        {
            SetMinimized( !m_window.isMinimized, 0.0 );
            m_window.isVisible = true;
            if ( !m_window.isMinimized )
            {
                m_presentation.preferences.layout = LayoutMode::Editor;
            }
            m_window.animationActive = false;
        }
    }
    if ( input.leftPressed && !input.rightDown && !input.middleDown && !HasOpenPopup() )
    {
        const bool profiler = DiagnosticDetailsBounds( m_presentationRects.markerHistory )
                                  .Contains( input.mouseX, input.mouseY );
        const bool memory = DiagnosticDetailsBounds( m_presentationRects.memoryWaterline )
                                .Contains( input.mouseX, input.mouseY );
        if ( profiler || memory )
        {
            SetActiveTab( profiler ? InGameUITab::Profiler : InGameUITab::Memory );
            SetVisible( true, 0.0 );
            m_presentation.focusedDiagnostic = profiler ? 1 : 2;
        }
    }
    if ( m_presentation.workspace == Workspace::Scene && input.wheelDelta != 0 && !HasOpenPopup() &&
         m_presentationRects.replayControls.Contains( input.mouseX, input.mouseY ) )
    {
        m_presentation.replayScroll = std::clamp( m_presentation.replayScroll -
                                                      static_cast<float>( input.wheelDelta ) / 1200.0f,
                                                  0.0f, 1.0f );
    }
    if ( input.leftPressed && !HasOpenPopup() )
    {
        if ( m_presentationRects.detailsReplayTab.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.detailsCauses = false;
            m_tooltip.Dismiss();
        }
        else if ( m_presentationRects.detailsCausesTab.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.detailsCauses = true;
            m_tooltip.Dismiss();
        }
    }
    UpdateDockPresentationInput( input );
    UpdateToolsDrawerBounds( input, width, height );
    m_profilerTab.histogramDockedBounds = m_presentationRects.markerHistory;
    m_memoryOverlay.dockedBounds = m_presentationRects.memoryWaterline;
    const float popupBottom = m_presentationRects.markerHistory.h > 0.0f     ? m_presentationRects.markerHistory.y
                              : m_presentationRects.memoryWaterline.h > 0.0f ? m_presentationRects.memoryWaterline.y
                                                                             : static_cast<float>( height );
    const UIRect popupViewport { 0.0f, m_presentationRects.header.h, static_cast<float>( width ),
                                 popupBottom - m_presentationRects.header.h };
    for ( UIComboBox* combo : { &m_sceneTab.combo, &m_sceneTab.recordingCombo, &m_sceneTab.solverLabCombo, &m_rendererCombo,
                                &m_reflectionCombo, &m_renderTargetCombo, &m_cameraModeCombo, &m_editorTab.objectCombo,
                                &m_cinematicTab.modeCombo, &m_toolsTabCombo, &m_toolsDisplayCombo } )
    {
        combo->SetPopupViewport( popupViewport );
    }
    m_cameraModeCombo.SetPopupViewport( { 0.0f, 0.0f, static_cast<float>( width ), popupBottom } );
    m_presentationPointerBlocked = headerHovered || !m_presentationRects.viewport.Contains( input.mouseX, input.mouseY ) ||
                                   m_presentationRects.right.Contains( input.mouseX, input.mouseY ) ||
                                   m_presentationRects.transport.Contains( input.mouseX, input.mouseY ) ||
                                   m_presentationRects.replayDetails.Contains( input.mouseX, input.mouseY );
}

void UIWindowInteractionOwner::ReturnToGame()
{
    m_toolsTabPressed = false;
    CancelInputCapture();
    SetVisible( false, 0.0 );
    m_presentation.preferences.layout = LayoutMode::Canvas;
    m_presentation.workspace = Workspace::Scene;
    m_presentation.detailsOpen = false;
    m_presentation.toolsOpen = false;
    m_presentationRects = ComputePresentationRects( m_presentation, m_lastScreenW, m_lastScreenH );
}

void UIWindowInteractionOwner::UpdateToolsVisibility()
{
    m_presentation.toolsOpen = m_window.isVisible && !m_window.isMinimized;
}

void UIWindowInteractionOwner::UpdateToolsDrawerBounds( const InputControl::UIInputSnapshot& input, int width, int height )
{
    UpdateToolsVisibility();
    if ( input.leftPressed && !input.rightDown && !input.middleDown && !HasOpenPopup() &&
         m_presentationRects.replayDetails.Contains( input.mouseX, input.mouseY ) )
    {
        m_toolsTabPressed = true;
        m_toolsTabWasOpen = m_presentation.toolsOpen;
        m_toolsTabDragged = false;
        m_interaction.resizeStartMouseY = input.mouseY;
        m_interaction.resizeStartH = m_toolsTabWasOpen ? static_cast<int>( m_presentationRects.drawer.h ) : 0;
    }
    if ( m_toolsTabPressed && !m_toolsTabDragged && input.leftDown &&
         std::abs( input.mouseY - m_interaction.resizeStartMouseY ) > 4 )
    {
        // A tab drag begins with the closed drawer's zero height, so its top
        // follows the pointer instead of jumping to the remembered open size.
        m_toolsTabDragged = true;
        m_interaction.isResizing = true;
        m_interaction.resizeRegion = 1;
        m_presentation.preferences.layout = LayoutMode::Editor;
        SetVisible( true, 0.0 );
        m_presentation.toolsOpen = true;
    }
    if ( m_toolsTabPressed && input.leftReleased )
    {
        if ( !m_toolsTabDragged )
        {
            SetVisible( !m_toolsTabWasOpen, 0.0 );
            if ( !m_toolsTabWasOpen )
            {
                m_presentation.preferences.layout = LayoutMode::Editor;
            }
        }
        UpdateToolsVisibility();
    }
    if ( m_presentation.toolsOpen && input.leftPressed && !input.rightDown && !input.middleDown && !HasOpenPopup() )
    {
        if ( m_presentationRects.drawerResize.Contains( input.mouseX, input.mouseY ) )
        {
            m_interaction.isResizing = true;
            m_interaction.resizeRegion = 1;
            m_interaction.resizeStartMouseY = input.mouseY;
            m_interaction.resizeStartH = static_cast<int>( m_presentationRects.drawer.h );
        }
        else if ( ComputeToolsChromeRects( m_presentationRects.drawer, true ).close.Contains( input.mouseX, input.mouseY ) )
        {
            SetMinimized( true, 0.0 );
            m_presentation.toolsOpen = false;
        }
    }
    if ( m_presentation.toolsOpen && m_interaction.isResizing && m_interaction.resizeRegion == 1 && input.leftDown )
    {
        // Invariant: resize is applied before App publishes the viewport for
        // picking and rendering. InputRouter owns the native capture request.
        m_presentation.preferences.drawerHeight = std::clamp( static_cast<float>( m_interaction.resizeStartH +
                                                                                  m_interaction.resizeStartMouseY -
                                                                                  input.mouseY ),
                                                              (std::min)( 100.0f, static_cast<float>( height ) * 0.8f ),
                                                              static_cast<float>( height ) * 0.8f );
    }
    m_presentation.editorInTools = m_presentation.toolsOpen && m_activeTab == InGameUITab::Editor;
    m_presentationRects = ComputePresentationRects( m_presentation, width, height );
    m_window.animationActive = false;
    if ( m_presentation.toolsOpen )
    {
        // Why: SetWindowBounds resets inspection scroll. Dock placement only
        // updates geometry; the existing tab presenters retain their state.
        m_window.x = static_cast<int>( m_presentationRects.drawer.x );
        m_window.y = static_cast<int>( m_presentationRects.drawer.y );
        m_window.width = static_cast<int>( m_presentationRects.drawer.w );
        m_window.height = static_cast<int>( m_presentationRects.drawer.h );
        m_window.isMaximized = false;
        m_window.hasAppliedDefaultPlacement = true;
    }
}

void UIWindowInteractionOwner::UpdateDockPresentationInput( const InputControl::UIInputSnapshot& input )
{
    if ( input.leftPressed && !HasOpenPopup() && !input.rightDown && !input.middleDown &&
         m_presentation.preferences.layout == LayoutMode::Editor )
    {
        const bool left = !m_presentation.preferences.leftFolded &&
                          m_presentationRects.leftResize.Contains( input.mouseX, input.mouseY );
        const bool right = !m_presentation.preferences.rightFolded &&
                           m_presentationRects.rightResize.Contains( input.mouseX, input.mouseY );
        if ( left || right )
        {
            m_interaction.isResizing = true;
            m_interaction.resizeRegion = left ? 2 : 3;
            m_interaction.resizeStartMouseX = input.mouseX;
            m_interaction.resizeStartW = static_cast<int>( left ? m_presentationRects.left.w : m_presentationRects.right.w );
        }
    }
    if ( m_interaction.isResizing && m_interaction.resizeRegion >= 2 && input.leftDown )
    {
        const bool left = m_interaction.resizeRegion == 2;
        const float maximum = m_presentationRects.window.w * ( left ? 0.25f : 0.3f );
        const float width = static_cast<float>( m_interaction.resizeStartW +
                                                ( left ? 1 : -1 ) * ( input.mouseX - m_interaction.resizeStartMouseX ) );
        const float bounded = std::clamp( width, (std::min)( 220.0f, maximum ), maximum );
        if ( left )
        {
            m_presentation.preferences.leftWidth = bounded;
        }
        else
        {
            m_presentation.preferences.rightWidth = bounded;
        }
    }
    if ( input.leftPressed && !HasOpenPopup() && !m_interaction.isResizing && !input.rightDown && !input.middleDown )
    {
        if ( m_presentationRects.editorTab.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.preferences.leftFolded = !m_presentation.preferences.leftFolded && !m_presentation.editorReplay;
            m_presentation.editorReplay = false;
            m_tooltip.Dismiss();
        }
        else if ( m_presentationRects.editorReplayTab.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.preferences.leftFolded = !m_presentation.preferences.leftFolded && m_presentation.editorReplay;
            m_presentation.editorReplay = true;
            m_tooltip.Dismiss();
        }
        else if ( m_presentationRects.leftFold.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.preferences.leftFolded = !m_presentation.preferences.leftFolded;
            m_tooltip.Dismiss();
        }
        else if ( m_presentationRects.rightFold.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.preferences.rightFolded = !m_presentation.preferences.rightFolded;
            m_tooltip.Dismiss();
        }
        else if ( m_presentationRects.causeTab.Contains( input.mouseX, input.mouseY ) )
        {
            m_presentation.preferences.rightFolded = false;
            m_tooltip.Dismiss();
        }
    }
    if ( input.wheelDelta != 0 && !HasOpenPopup() &&
         m_presentationRects.editorControls.Contains( input.mouseX, input.mouseY ) )
    {
        m_presentation.editorScroll = (std::max)( 0.0f, m_presentationRects.editorScroll -
                                                            static_cast<float>( input.wheelDelta ) * ( 34.0f / 120.0f ) );
        // Geometry owns the grid-dependent scroll limit, including narrow docks.
        m_presentation.editorScroll = ComputePresentationRects( m_presentation, m_lastScreenW, m_lastScreenH ).editorScroll;
    }
}

EditorMiniPaletteLayout UIWindowInteractionOwner::PresentedEditorPalette() const
{
    UIRect content = m_presentationRects.editorControls;
    float scroll = m_presentationRects.editorScroll;
    if ( m_presentation.editorInTools )
    {
        content = ComputeToolsChromeRects( { static_cast<float>( m_window.x ), static_cast<float>( m_window.y ),
                                             static_cast<float>( m_window.width ), static_cast<float>( m_window.height ) },
                                           true )
                      .content;
        scroll = m_scrollY;
    }
    if ( content.w <= 0.0f )
    {
        return {};
    }
    return BuildEditorMiniPaletteLayout( m_lastScreenW, m_lastScreenH, {}, m_editorMiniPalettePressedEntry,
                                         m_editorMiniPaletteFlyoutOpen,
                                         { content.x, content.y + 316.0f - scroll, content.w, content.h }, content );
}

bool UIWindowInteractionOwner::HandleEditorDockInput( const InputControl::UIInputSnapshot& input,
                                                      InGameUIInputResult& result )
{
    const UIRect& bounds = m_presentationRects.editorControls;
    if ( !m_presentationEnabled || m_interaction.isResizing || bounds.w <= 0.0f ||
         ( !bounds.Contains( m_mouseX, m_mouseY ) && !m_editorTab.objectCombo.IsOpen() ) )
    {
        return false;
    }
    if ( HasOpenPopup() && !m_editorTab.objectCombo.IsOpen() )
    {
        return false;
    }
    m_blocksCameraMouse = true;
    result.unhandledWheelDelta = 0;
    if ( input.wheelDelta != 0 && m_editorTab.objectCombo.IsOpen() )
    {
        m_editorTab.objectCombo.ScrollOptions( -input.wheelDelta / 120, EditorTab::OBJECT_TYPE_COUNT );
    }
    if ( input.leftPressed )
    {
        if ( EditorTab::HandleContentClick( m_editorTab, result, m_mouseX, m_mouseY, bounds.x,
                                            bounds.y + 42.0f - m_presentationRects.editorScroll, bounds.w ) )
        {
            result.commands.ui.userInteracted = true;
        }
    }
    return true;
}

void UIWindowInteractionOwner::ResetPresentationResources()
{
    m_backdropBlur.ResetResources();
    m_cache.Reset();
}

bool UIWindowInteractionOwner::IsVisible() const
{
    return m_window.isVisible;
}


bool UIWindowInteractionOwner::IsMinimized() const
{
    return m_window.isMinimized;
}


void UIWindowInteractionOwner::SetVisible( bool visible, double now )
{
    m_window.isVisible = visible;
    if ( !visible )
    {
        m_toolsTabCombo.Close();
        m_toolsDisplayCombo.Close();
    }
    m_cache.Reset();
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Visibility );

    if ( visible )
    {
        // Opening Tools releases diagnostic space once. F5/F6 can reopen it
        // while Tools remains open, without being hidden on every frame.
        SetPerformanceHistogramEnabled( false );
        SetMemoryOverlayEnabled( false );
        m_window.isMinimized = false;
        m_scrollbarVisibleUntil = now + 1.2;
        CancelEditorMiniPaletteInteraction();
    }
    else
    {
        m_window.isMinimized = true;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        m_blocksCameraMouse = false;
        CancelActiveSliderPreview();
        CancelEditorMiniPaletteInteraction();
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
}


void UIWindowInteractionOwner::ToggleVisible( double now )
{
    if ( m_presentationEnabled && ( !m_window.isVisible || m_window.isMinimized ) )
    {
        m_presentation.preferences.layout = LayoutMode::Editor;
    }
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }

    SetMinimized( !m_window.isMinimized, now );
}


void UIWindowInteractionOwner::CancelEditorMiniPaletteInteraction()
{
    m_editorMiniPalettePressActive = false;
    m_editorMiniPaletteFlyoutOpen = false;
    m_editorMiniPalettePressedEntry = -1;
    m_editorMiniPalettePressedObjectType = -1;
    m_editorMiniPalettePressedTreePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    m_editorMiniPalettePressedHoldMode = EDITOR_MINI_HOLD_MODE_NONE;
    m_editorMiniPalettePressStart = 0.0;
}


void UIWindowInteractionOwner::CancelActiveSliderPreview()
{
    // Invariant: a preview belongs to the current pointer capture only. Losing
    // that capture restores every deferred control to its last runtime snapshot.
    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    ProfilerTab::ResetPreviewState( m_profilerTab );
    MemoryTab::ResetPreviewState( m_memoryOverlay );
}


void UIWindowInteractionOwner::SetMinimized( bool minimized, double now )
{
    if ( m_window.isMinimized == minimized )
    {
        return;
    }

    if ( minimized )
    {
        m_toolsTabCombo.Close();
        m_toolsDisplayCombo.Close();
    }
    const UIRect currentBounds = Chrome::WindowRect( m_window );
    const UIRect minimizedBounds = Layout::MinimizedRect( m_lastScreenW, m_lastScreenH, m_window.minimizedWidth );
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_blocksCameraMouse = false;
    CancelEditorMiniPaletteInteraction();

    if ( minimized )
    {
        CancelActiveSliderPreview();
        m_window.isMinimized = true;
        Chrome::BeginWindowAnimation( m_window, currentBounds, minimizedBounds, now, true );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
    else
    {
        SetPerformanceHistogramEnabled( false );
        SetMemoryOverlayEnabled( false );
        m_window.isMinimized = false;
        m_cameraModeCombo.Close();
        Chrome::BeginWindowAnimation( m_window, minimizedBounds, Chrome::WindowRect( m_window ), now, false );
        m_scrollbarVisibleUntil = now + 1.2;
    }

    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::WindowState );
    m_cache.Reset();
}


void UIWindowInteractionOwner::SetActiveTab( InGameUITab tab )
{
    m_tooltip.Dismiss();
    const int tabIndex = static_cast<int>( tab );

    if ( tabIndex < 0 || tabIndex >= static_cast<int>( InGameUITab::Count ) )
    {
        tab = InGameUITab::Scene;
    }

    m_toolsTabCombo.Close();
    m_toolsDisplayCombo.Close();
    m_activeTab = tab;
    m_presentation.preferences.lastTool = static_cast<int>( tab );
    if ( m_presentationEnabled )
    {
        m_presentation.editorInTools = m_presentation.toolsOpen && tab == InGameUITab::Editor;
        m_presentationRects = ComputePresentationRects( m_presentation, m_lastScreenW, m_lastScreenH );
    }
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    CloseSceneCombo();
    m_editorTab.objectCombo.Close();
    SceneTab::CloseRecordingCombo( m_sceneTab );
    m_sceneTab.solverLabCombo.Close();
    CinematicTab::CloseCombo( m_cinematicTab );
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    CancelActiveSliderPreview();
    m_scrollbarRevealPending = false;
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Content );
    m_cache.Reset();
}


InGameUITab UIWindowInteractionOwner::GetActiveTab() const
{
    return m_activeTab;
}


void UIWindowInteractionOwner::CancelInputCapture()
{
    m_toolsTabPressed = false;
    m_presentationHeaderHovered = false;
    m_tooltip.Dismiss();
    // Focus loss can skip UpdateInput for many rendered frames. Keep hover
    // feedback dismissed until the router supplies the next focused sample.
    m_tooltipGestureActive = true;
    m_toolsTabCombo.Close();
    m_toolsDisplayCombo.Close();
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_blocksCameraMouse = false;
    CancelActiveSliderPreview();
    m_editorTab.objectCombo.Close();
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    CancelEditorMiniPaletteInteraction();
    ProfilerTab::CancelPerformanceHistogramInteraction( m_profilerTab );
}


bool UIWindowInteractionOwner::BlocksCameraMouse() const
{
    return m_blocksCameraMouse || m_presentationPointerBlocked;
}

bool UIWindowInteractionOwner::HasOpenPopup() const
{
    return m_profilerTab.histogramSelectorOpen || m_toolsTabCombo.IsOpen() || m_toolsDisplayCombo.IsOpen() ||
           m_sceneTab.combo.IsOpen() || m_sceneTab.recordingCombo.IsOpen() || m_sceneTab.solverLabCombo.IsOpen() ||
           m_rendererCombo.IsOpen() || m_reflectionCombo.IsOpen() || m_renderTargetCombo.IsOpen() ||
           m_editorTab.objectCombo.IsOpen() || m_cinematicTab.modeCombo.IsOpen() || m_cameraModeCombo.IsOpen();
}

bool UIWindowInteractionOwner::BlocksReplayMouse() const
{
    const UIRect revealBounds { 0.0f, m_presentationRects.transport.y, m_presentationRects.window.w,
                                m_presentationRects.transport.h };
    const bool replayRegion = revealBounds.Contains( m_mouseX, m_mouseY ) ||
                              m_presentationRects.replayControls.Contains( m_mouseX, m_mouseY );
    // Chrome still blocks world input. Only its Replay region is offered to
    // the existing Replay handler; a popup or captured UI gesture takes priority.
    return m_blocksCameraMouse || ( m_presentationPointerBlocked && !( m_presentationEnabled && replayRegion ) );
}

bool UIWindowInteractionOwner::BlocksCauseMouse() const
{
    const bool causeRegion = m_presentationEnabled && m_presentationRects.causeControls.Contains( m_mouseX, m_mouseY );
    return m_blocksCameraMouse || ( m_presentationPointerBlocked && !causeRegion );
}


bool UIWindowInteractionOwner::BlocksKeyboard() const
{
    return m_profilerTab.histogramSelectorOpen || m_toolsTabCombo.IsOpen() || m_toolsDisplayCombo.IsOpen() ||
           m_rendererCombo.IsOpen() || m_reflectionCombo.IsOpen() || m_cameraModeCombo.IsOpen() ||
           ( m_presentationEnabled && m_editorTab.objectCombo.IsOpen() ) ||
           ( m_window.isVisible && !m_window.isMinimized &&
             ( m_sceneTab.combo.IsOpen() || m_sceneTab.recordingCombo.IsOpen() || m_sceneTab.solverLabCombo.IsOpen() ||
               CinematicTab::IsComboOpen( m_cinematicTab ) || m_editorTab.objectCombo.IsOpen() ||
               m_renderTargetCombo.IsOpen() ) );
}


bool UIWindowInteractionOwner::WantsNativeMouseCursor() const
{
    return ( m_window.isVisible && !m_window.isMinimized ) || BlocksCameraMouse() ||
           ProfilerTab::PerformanceHistogramIsInteracting( m_profilerTab );
}


void UIWindowInteractionOwner::SetWindowBounds( int x, int y, int width, int height )
{
    m_window.x = x;
    m_window.y = y;
    m_window.width = width;
    m_window.height = height;
    m_window.restoreX = x;
    m_window.restoreY = y;
    m_window.restoreW = width;
    m_window.restoreH = height;
    m_window.hasAppliedDefaultPlacement = true;
    m_window.isMaximized = false;
    m_window.animationActive = false;
    m_scrollY = 0.0f;
    m_scrollbarVisibleUntil = 0.0;
    m_scrollbarRevealPending = false;
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    m_cache.Reset();
}

bool UIWindowInteractionOwner::CaptureInteractionAnchor( int clientX, int clientY, char* output,
                                                         std::size_t outputSize ) const
{
    if ( !output || outputSize == 0u || !m_window.isVisible || m_window.width <= 1 || m_window.height <= 1 ||
         clientX < m_window.x || clientY < m_window.y || clientX >= m_window.x + m_window.width ||
         clientY >= m_window.y + m_window.height )
    {
        return false;
    }

    // Concept: the stable anchor is local to the owning UI window rather than
    // the process viewport. Layout movement and window-size changes therefore
    // preserve the same semantic window location, with viewport normalization
    // retained independently as fallback evidence.
    const float localX = static_cast<float>( clientX - m_window.x ) / static_cast<float>( m_window.width - 1 );
    const float localY = static_cast<float>( clientY - m_window.y ) / static_cast<float>( m_window.height - 1 );
    const int written = std::snprintf( output, outputSize, "operator-ui:%.6f,%.6f", localX, localY );
    return written > 0 && static_cast<std::size_t>( written ) < outputSize;
}

bool UIWindowInteractionOwner::ResolveInteractionAnchor( const char* anchor, int& clientX, int& clientY ) const
{
    constexpr const char* PREFIX = "operator-ui:";

    if ( !anchor || std::strncmp( anchor, PREFIX, std::strlen( PREFIX ) ) != 0 || !m_window.isVisible ||
         m_window.width <= 1 || m_window.height <= 1 )
    {
        return false;
    }

    float localX = 0.0f;
    float localY = 0.0f;

    if ( sscanf_s( anchor + std::strlen( PREFIX ), "%f,%f", &localX, &localY ) != 2 || !std::isfinite( localX ) ||
         !std::isfinite( localY ) || localX < 0.0f || localX > 1.0f || localY < 0.0f || localY > 1.0f )
    {
        return false;
    }

    clientX = m_window.x + static_cast<int>( std::lround( localX * static_cast<float>( m_window.width - 1 ) ) );
    clientY = m_window.y + static_cast<int>( std::lround( localY * static_cast<float>( m_window.height - 1 ) ) );
    return true;
}


void UIWindowInteractionOwner::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
        m_cache.Reset();
    }
}


void UIWindowInteractionOwner::SetRendererComboOpen( bool open )
{
    m_rendererCombo.SetOpen( open );

    if ( open )
    {
        SceneTab::CloseRecordingCombo( m_sceneTab );
        m_sceneTab.solverLabCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
}


void UIWindowInteractionOwner::SetWaterComboOpen( bool open )
{
    m_reflectionCombo.SetOpen( open );

    if ( open )
    {
        m_rendererCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
    }
}


void UIWindowInteractionOwner::SetSceneComboOpen( bool open )
{
    m_sceneTab.combo.SetOpen( open );

    if ( open )
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
        SceneTab::RequestFilterKeySync( m_sceneTab );
    }
    else
    {
        SceneTab::ClearFilter( m_sceneTab );
    }
}


void UIWindowInteractionOwner::SetSceneFilter( const char* filter )
{
    SceneTab::SetFilter( m_sceneTab, filter );
}


void UIWindowInteractionOwner::SetProfilerExpandAll( bool expandAll )
{
    ProfilerTab::SetExpandAll( m_profilerTab, expandAll );
    m_cache.Reset();
}


void UIWindowInteractionOwner::SetProfilerTimelineEnabled( bool enabled )
{
    ProfilerTab::SetTimelineEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


void UIWindowInteractionOwner::SetPerformanceHistogramEnabled( bool enabled )
{
    ProfilerTab::SetPerformanceHistogramEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


bool UIWindowInteractionOwner::IsPerformanceHistogramEnabled() const
{
    return ProfilerTab::PerformanceHistogramEnabled( m_profilerTab );
}


void UIWindowInteractionOwner::TogglePerformanceHistogramEnabled()
{
    SetPerformanceHistogramEnabled( !IsPerformanceHistogramEnabled() );
}


void UIWindowInteractionOwner::SetMemoryOverlayEnabled( bool enabled )
{
    MemoryTab::SetOverlayEnabled( m_memoryOverlay, enabled );
    m_cache.Reset();
}


bool UIWindowInteractionOwner::IsMemoryOverlayEnabled() const
{
    return MemoryTab::OverlayEnabled( m_memoryOverlay );
}


void UIWindowInteractionOwner::ToggleMemoryOverlayEnabled()
{
    SetMemoryOverlayEnabled( !IsMemoryOverlayEnabled() );
}


bool UIWindowInteractionOwner::NeedsUiTextPass() const
{
    return m_presentationEnabled || m_window.isVisible || IsPerformanceHistogramEnabled() || IsMemoryOverlayEnabled();
}


void UIWindowInteractionOwner::SetHitboxOverlayEnabled( bool enabled )
{
    if ( m_hitboxOverlayEnabled != enabled )
    {
        m_hitboxOverlayEnabled = enabled;
        m_cache.Reset();
    }
}


void UIWindowInteractionOwner::SetScrollY( float scrollY )
{
    m_scrollY = (std::max)( 0.0f, scrollY );
    m_scrollbarRevealPending = true;
    m_cache.Reset();
}


void UIWindowInteractionOwner::PrepareForDraw( double now )
{
    if ( !m_scrollbarRevealPending )
    {
        return;
    }

    // Why: programmatic callers do not own the runtime clock. Anchor feedback
    // to the next visible full-window draw instead of an absolute startup time.
    m_scrollbarVisibleUntil = (std::max)( m_scrollbarVisibleUntil, now + 1.2 );
    m_scrollbarRevealPending = false;
}


void UIWindowInteractionOwner::SetMouseOverride( bool enabled, int x, int y )
{
    m_hasMouseOverride = enabled;
    m_mouseOverrideX = x;
    m_mouseOverrideY = y;

    if ( enabled )
    {
        m_mouseX = x;
        m_mouseY = y;
    }
}


void UIWindowInteractionOwner::SetMaximized( bool maximized, int screenW, int screenH, double now )
{
    if ( Chrome::SetMaximized( m_window, maximized, screenW, screenH, now ) )
    {
        m_scrollbarVisibleUntil = 0.0;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    }
}


int UIWindowInteractionOwner::ContentHeight() const
{
    switch ( m_activeTab )
    {
    case InGameUITab::Scene:
        return SceneTab::ContentHeight();
    case InGameUITab::Keys:
        return ControlsTab::ContentHeight();
    case InGameUITab::Profiler:
        return ProfilerTab::ContentHeight( m_profilerTab );
    case InGameUITab::Memory:
        return MemoryTab::ContentHeight();
    case InGameUITab::Editor:
        return m_presentationEnabled ? 500 : EditorTab::ContentHeight();
    case InGameUITab::Physics:
        return PhysicsTab::ContentHeight();
    case InGameUITab::Options:
        return OptionsTab::ContentHeight();
    case InGameUITab::Render:
        return RenderContentHeight();
    case InGameUITab::Targets:
        return RenderTargetsContentHeight();
    case InGameUITab::Sky:
        return SkyTab::ContentHeight();
    case InGameUITab::Cinematic:
        return CinematicTab::ContentHeight();
    default:
        return ControlsTab::ContentHeight();
    }
}


void UIWindowInteractionOwner::CloseSceneCombo()
{
    SceneTab::CloseCombo( m_sceneTab );
    SceneTab::CloseRecordingCombo( m_sceneTab );
    m_sceneTab.solverLabCombo.Close();
}


InputControl::UIPointerOverride UIWindowInteractionOwner::InputOverride() const
{
    return InputControl::UIPointerOverride { m_hasMouseOverride, m_mouseOverrideX, m_mouseOverrideY };
}


UIWindowInteractionOwner::MinimizedControlResult
UIWindowInteractionOwner::HandleMinimizedCameraMode( const InputControl::UIInputSnapshot& input, const UIRect& minimized,
                                                     bool showEditorMiniPalette, uint32_t cameraModeEnabledMask,
                                                     InGameUIInputResult& result )
{
    MinimizedControlResult control;
    if ( m_presentationEnabled && m_presentation.preferences.layout == LayoutMode::Canvas && !m_presentationHeaderHovered &&
         !m_cameraModeCombo.IsOpen() )
    {
        return control;
    }
    const UIRect bounds = m_presentationEnabled ? ComputeHeaderRects( m_presentationRects.header ).camera
                                                : MinimizedCameraModeComboBounds( minimized );
    m_cameraModeCombo.SetLabelVisible( false );
    m_cameraModeCombo.SetBounds( bounds.x, bounds.y, bounds.w, bounds.h );
    m_cameraModeCombo.SetDropUp( !m_presentationEnabled );

    if ( showEditorMiniPalette && !m_presentationEnabled )
    {
        m_cameraModeCombo.Close();
        return control;
    }

    if ( m_editorMiniPalettePressActive )
    {
        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
    }
    CancelEditorMiniPaletteInteraction();

    const bool optionHit = m_cameraModeCombo.IsOpen() &&
                           m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT ) >= 0;
    const bool dropdownHit = m_cameraModeCombo.IsOpen() &&
                             m_cameraModeCombo.DropdownBounds( CAMERA_MODE_OPTION_COUNT ).Contains( m_mouseX, m_mouseY );
    control.inside = m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) || optionHit || dropdownHit;

    if ( input.leftPressed && m_cameraModeCombo.IsOpen() )
    {
        const int option = m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT );
        const uint32_t disabledMask = ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) & ~cameraModeEnabledMask;
        const bool optionDisabled = option >= 0 && option < 32 && ( disabledMask & ( 1u << option ) ) != 0;

        if ( option >= 0 && option < CAMERA_MODE_OPTION_COUNT && !optionDisabled )
        {
            result.commands.run.requestedCameraMode = option;
            m_cameraModeCombo.Close();
            control.handled = true;
        }
        else if ( m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) )
        {
            m_cameraModeCombo.ToggleOpen();
            control.handled = true;
        }
        else if ( option < 0 )
        {
            m_cameraModeCombo.Close();
            control.handled = true;
        }
    }
    else if ( input.leftPressed && m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) )
    {
        m_cameraModeCombo.ToggleOpen();
        control.handled = true;
    }

    if ( control.handled )
    {
        result.commands.ui.userInteracted = true;
        m_cache.Reset();
    }
    return control;
}

UIWindowInteractionOwner::MinimizedControlResult
UIWindowInteractionOwner::HandleMinimizedEditorStatus( const InputControl::UIInputSnapshot& input, const UIRect& minimized,
                                                       bool editorPlacementMode, bool editorPlaceStatic,
                                                       bool editorTerrainAlign, InGameUIInputResult& result )
{
    MinimizedControlResult control;
    const EditorMinimizedStatusLayout layout = BuildEditorMinimizedStatusLayout( minimized, editorPlacementMode,
                                                                                 editorPlaceStatic, editorTerrainAlign );
    const bool insideMode = layout.modeChip.Contains( m_mouseX, m_mouseY );
    const bool insideBody = layout.bodyChip.Contains( m_mouseX, m_mouseY );
    const bool insideAlign = layout.alignChip.Contains( m_mouseX, m_mouseY );
    control.inside = insideMode || insideBody || insideAlign;

    if ( input.leftPressed && insideMode )
    {
        result.commands.editor.togglePlacementMode = true;
        control.handled = true;
    }
    else if ( input.leftPressed && insideBody )
    {
        result.commands.editor.togglePlaceStatic = true;
        control.handled = true;
    }
    else if ( input.leftPressed && insideAlign )
    {
        result.commands.editor.toggleTerrainAlign = true;
        control.handled = true;
    }

    result.commands.ui.userInteracted = result.commands.ui.userInteracted || control.handled;
    return control;
}

void UIWindowInteractionOwner::SelectEditorMiniPaletteObject( InGameUIInputResult& result, int objectType,
                                                              bool requestPlaceStatic, bool placeStatic )
{
    result.commands.editor.requestedObjectType = std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );
    if ( requestPlaceStatic )
    {
        result.commands.editor.requestPlaceStatic = true;
        result.commands.editor.requestedPlaceStatic = placeStatic;
    }
    result.commands.editor.enterPlacementMode = true;
    result.commands.ui.userInteracted = true;
}

bool UIWindowInteractionOwner::BeginEditorMiniPalettePress( const EditorMiniPaletteLayout& layout, double now,
                                                            InGameUIInputResult& result )
{
    const int pressedButton = HitEditorMiniPaletteButton( layout, m_mouseX, m_mouseY );
    if ( pressedButton < 0 )
    {
        return false;
    }

    const EditorMiniPaletteEntry& entry = kEditorMiniPaletteEntries[pressedButton];
    if ( entry.holdMode == EDITOR_MINI_HOLD_MODE_NONE )
    {
        SelectEditorMiniPaletteObject( result, entry.objectType, false, false );
        return true;
    }

    m_editorMiniPalettePressActive = true;
    m_editorMiniPaletteFlyoutOpen = false;
    m_editorMiniPalettePressedEntry = pressedButton;
    m_editorMiniPalettePressedObjectType = entry.objectType;
    m_editorMiniPalettePressedTreePlacement = entry.treePlacement;
    m_editorMiniPalettePressedHoldMode = entry.holdMode;
    m_editorMiniPalettePressStart = now;
    result.commands.ui.userInteracted = true;
    result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
    return true;
}

void UIWindowInteractionOwner::FinishEditorMiniPalettePress( const EditorMiniPaletteLayout& layout,
                                                             InGameUIInputResult& result )
{
    int selectedObjectType = -1;
    if ( m_editorMiniPaletteFlyoutOpen )
    {
        const int option = HitEditorMiniPaletteFlyoutOption( layout, m_mouseX, m_mouseY );
        if ( option >= 0 && m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
        {
            selectedObjectType = EditorMiniTreeObjectType( option, m_editorMiniPalettePressedTreePlacement );
        }
        else if ( option >= 0 && m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
        {
            selectedObjectType = EditorMiniRagdollObjectType( option );
        }
    }
    else if ( m_editorMiniPalettePressedEntry >= 0 && m_editorMiniPalettePressedEntry < layout.buttonCount &&
              HitEditorMiniPaletteButton( layout, m_mouseX, m_mouseY ) == m_editorMiniPalettePressedEntry )
    {
        selectedObjectType = m_editorMiniPalettePressedObjectType;
    }

    if ( selectedObjectType >= 0 )
    {
        bool placeStatic = false;
        const bool requestPlaceStatic = EditorMiniSelectionRequestsStatic( m_editorMiniPalettePressedHoldMode,
                                                                           m_editorMiniPalettePressedTreePlacement,
                                                                           placeStatic );
        SelectEditorMiniPaletteObject( result, selectedObjectType, requestPlaceStatic, placeStatic );
    }

    CancelEditorMiniPaletteInteraction();
    result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
}

UIWindowInteractionOwner::MinimizedControlResult
UIWindowInteractionOwner::HandleEditorMiniPalette( const InputControl::UIInputSnapshot& input, int screenW, int screenH,
                                                   const UIRect& minimized, double now, InGameUIInputResult& result )
{
    MinimizedControlResult control;
    if ( m_editorMiniPalettePressActive && !input.leftDown && !input.leftReleased )
    {
        CancelEditorMiniPaletteInteraction();
        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
    }
    if ( m_editorMiniPalettePressActive && !m_editorMiniPaletteFlyoutOpen &&
         m_editorMiniPalettePressedHoldMode != EDITOR_MINI_HOLD_MODE_NONE &&
         now - m_editorMiniPalettePressStart >= EDITOR_MINI_HOLD_SECONDS )
    {
        m_editorMiniPaletteFlyoutOpen = true;
    }

    const EditorMiniPaletteLayout layout = m_presentationEnabled
                                               ? PresentedEditorPalette()
                                               : BuildEditorMiniPaletteLayout( screenW, screenH, minimized,
                                                                               m_editorMiniPalettePressedEntry,
                                                                               m_editorMiniPaletteFlyoutOpen );
    control.inside = EditorMiniPaletteContains( layout, m_mouseX, m_mouseY );
    if ( input.leftPressed && control.inside )
    {
        control.handled = BeginEditorMiniPalettePress( layout, now, result );
    }
    if ( m_editorMiniPalettePressActive )
    {
        control.handled = true;
        result.commands.ui.userInteracted = true;
        if ( input.leftReleased )
        {
            FinishEditorMiniPalettePress( layout, result );
        }
    }
    return control;
}

InGameUIInputResult UIWindowInteractionOwner::HandleMinimizedInput( const InputControl::UIInputSnapshot& input, int screenW,
                                                                    int screenH, double now, bool editorModeEnabled,
                                                                    bool editorPlacementMode, bool editorPlaceStatic,
                                                                    bool editorTerrainAlign, uint32_t cameraModeEnabledMask )
{
    InGameUIInputResult result;
    result.unhandledWheelDelta = input.wheelDelta;
    const UIRect minimized = Layout::MinimizedRect( screenW, screenH, m_window.minimizedWidth );
    const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
    const MinimizedControlResult camera = HandleMinimizedCameraMode( input, minimized, editorModeEnabled,
                                                                     cameraModeEnabledMask, result );
    MinimizedControlResult status;
    MinimizedControlResult palette;
    if ( editorModeEnabled )
    {
        status = HandleMinimizedEditorStatus( input, minimized, editorPlacementMode, editorPlaceStatic, editorTerrainAlign,
                                              result );
        palette = HandleEditorMiniPalette( input, screenW, screenH, minimized, now, result );
    }

    const bool blocksCamera = insideMinimized || camera.BlocksCamera() || status.BlocksCamera() || palette.BlocksCamera() ||
                              m_editorMiniPalettePressActive;
    if ( blocksCamera )
    {
        result.unhandledWheelDelta = 0;
    }
    if ( input.leftPressed && insideMinimized && !camera.handled && !status.handled && !palette.handled )
    {
        SetMinimized( false, now );
        result.commands.ui.userInteracted = true;
    }
    m_blocksCameraMouse = blocksCamera;
    return result;
}


void UIWindowInteractionOwner::UpdateActiveSliderInput( InGameUIInputResult& result )
{
    // Why: sliders update previews continuously while dragged. Heavy operations
    // such as rebuilding generated bodies are delayed until mouse release,
    // but cheap scalar controls are emitted every frame for immediate feedback.
    if ( !SceneTab::UpdateActiveSlider( m_sceneTab, m_activeSlider, m_mouseX, result ) &&
         !ProfilerTab::UpdateActiveSlider( m_profilerTab, m_activeSlider, m_mouseX, m_lastMaxWorkerThreadCount, result ) &&
         !MemoryTab::UpdateActiveSlider( m_memoryOverlay, m_activeSlider, m_mouseX, result ) &&
         !OptionsTab::UpdateActiveSlider( m_optionsTab, m_activeSlider, m_mouseX, m_lastModelCapacity, result ) &&
         !PhysicsTab::UpdateActiveSlider( m_physicsTab, m_activeSlider, m_mouseX, result ) )
    {
        const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );

        if ( renderSlider >= 0 )
        {
            SetRenderSliderResult( result, m_renderSliders[renderSlider], m_mouseX, kRenderSliderSpecs[renderSlider] );
        }
        else
        {
            if ( !SkyTab::UpdateActiveSlider( m_skyTab, m_activeSlider, m_mouseX, result ) &&
                 !CinematicTab::UpdateActiveSlider( m_cinematicTab, m_activeSlider, m_mouseX, result ) )
            {
                ControlsTab::UpdateActiveSlider( m_controlsTab, m_activeSlider, m_mouseX, m_lastModelCapacity,
                                                 m_lastSolverBallCount, m_lastSolverBoxCount, result );
            }
        }
    }
}


void UIWindowInteractionOwner::UpdateWindowDragAndResize( bool leftNow, int screenW, int screenH, double now )
{
    if ( m_presentationEnabled )
    {
        return;
    }
    const int margin = 10;
    const int marginX = (std::min)( margin, ( screenW - 1 ) / 2 );
    const int marginY = (std::min)( margin, ( screenH - 1 ) / 2 );
    const int maxW = (std::max)( 1, screenW - marginX * 2 );
    const int maxH = (std::max)( 1, screenH - marginY * 2 );
    const int effectiveMinW = (std::min)( 520, maxW );
    const int effectiveMinH = (std::min)( 250, maxH );

    if ( leftNow && m_interaction.isDragging )
    {
        const int oldX = m_window.x;
        const int oldY = m_window.y;
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX, marginX,
                                 (std::max)( marginX, screenW - m_window.width - marginX ) );

        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY, marginY,
                                 (std::max)( marginY, screenH - m_window.height - marginY ) );

        if ( oldX != m_window.x || oldY != m_window.y )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }

    if ( leftNow && m_interaction.isResizing )
    {
        const int oldW = m_window.width;
        const int oldH = m_window.height;
        m_window.width = std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, effectiveMinW,
                                     maxW );

        m_window.height = std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, effectiveMinH,
                                      maxH );

        m_scrollbarVisibleUntil = now + 1.4;

        if ( oldW != m_window.width || oldH != m_window.height )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }
}


void UIWindowInteractionOwner::FinishPointerRelease( InGameUIInputResult& result )
{
    // Invariant: commit deferred slider previews exactly once on release. This avoids
    // rebuilding solver objects or generated model pools every mouse-move
    // while still letting the drawn slider thumb track the user's drag.
    if ( !SceneTab::CommitActiveSlider( m_sceneTab, m_activeSlider, result ) &&
         !ProfilerTab::CommitActiveSlider( m_profilerTab, m_activeSlider, result ) &&
         !MemoryTab::CommitActiveSlider( m_memoryOverlay, m_activeSlider, result ) &&
         !OptionsTab::CommitActiveSlider( m_optionsTab, m_activeSlider, result ) &&
         !PhysicsTab::CommitActiveSlider( m_physicsTab, m_activeSlider, result ) )
    {
        const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );

        if ( renderSlider >= 0 )
        {
            SetRenderSliderResult( result, m_renderSliders[renderSlider], m_mouseX, kRenderSliderSpecs[renderSlider] );
        }
        else
        {
            if ( !SkyTab::CommitActiveSlider( m_skyTab, m_activeSlider, m_mouseX, result ) &&
                 !CinematicTab::CommitActiveSlider( m_cinematicTab, m_activeSlider, m_mouseX, result ) )
            {
                ControlsTab::CommitActiveSlider( m_controlsTab, m_activeSlider, result );
            }
        }
    }

    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    ProfilerTab::ResetPreviewState( m_profilerTab );
    MemoryTab::ResetPreviewState( m_memoryOverlay );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
}

float UIWindowInteractionOwner::WindowPointerLayout::ContentX() const
{
    return chrome.content.x;
}

float UIWindowInteractionOwner::WindowPointerLayout::ContentWidth() const
{
    return chrome.content.w;
}

float UIWindowInteractionOwner::WindowPointerLayout::RowBase( float scrollY ) const
{
    return static_cast<float>( contentY ) + 42.0f - scrollY;
}

float UIWindowInteractionOwner::WindowPointerLayout::ScrolledY( float scrollY ) const
{
    return static_cast<float>( contentY ) - scrollY;
}

bool UIWindowInteractionOwner::WindowPointerLayout::InFooter( int mouseY ) const
{
    return inside && mouseY >= bottomY;
}

UIWindowInteractionOwner::WindowOptionView
UIWindowInteractionOwner::BuildWindowOptionView( const SceneNavigationModel& sceneNavigation ) const
{
    WindowOptionView view;
    view.scenes = std::span<const char* const>( sceneNavigation.browser.namePtrs.empty()
                                                    ? nullptr
                                                    : sceneNavigation.browser.namePtrs.data(),
                                                sceneNavigation.browser.namePtrs.size() );
    view.recordings = std::span<const char* const>( sceneNavigation.recordings.namePtrs.empty()
                                                        ? nullptr
                                                        : sceneNavigation.recordings.namePtrs.data(),
                                                    sceneNavigation.recordings.namePtrs.size() );
    view.selectedScene = sceneNavigation.browser.selectedSceneIndex;
    view.selectedRecording = sceneNavigation.recordings.paths.empty() ? -1 : sceneNavigation.recordings.selectedIndex;
    return view;
}

UIWindowInteractionOwner::WindowPointerLayout UIWindowInteractionOwner::PrepareWindowPointerLayout( double now )
{
    WindowPointerLayout layout;
    const UIRect inputBounds = Chrome::CurrentWindowRect( m_window, now );
    layout.inputX = static_cast<int>( std::round( inputBounds.x ) );
    layout.inputY = static_cast<int>( std::round( inputBounds.y ) );
    layout.inputW = static_cast<int>( std::round( inputBounds.w ) );
    layout.inputH = static_cast<int>( std::round( inputBounds.h ) );
    layout.hitBounds = { static_cast<float>( layout.inputX ), static_cast<float>( layout.inputY ),
                         static_cast<float>( layout.inputW ), static_cast<float>( layout.inputH ) };
    layout.inside = layout.hitBounds.Contains( m_mouseX, m_mouseY );
    layout.chrome = ComputeToolsChromeRects( layout.hitBounds, m_presentationEnabled );
    layout.inTitle = layout.inside && layout.chrome.title.Contains( m_mouseX, m_mouseY );
    layout.inTabs = layout.inside && layout.chrome.tabs.Contains( m_mouseX, m_mouseY );
    layout.inResize = !m_presentationEnabled && !m_window.isMaximized && layout.inside &&
                      Chrome::IsResizeHotspot( layout.hitBounds, m_mouseX, m_mouseY );
    layout.contentY = static_cast<int>( layout.chrome.content.y );
    layout.contentH = static_cast<int>( layout.chrome.content.h );
    layout.bottomY = static_cast<int>( layout.chrome.footer.y );
    layout.inContent = layout.inside && layout.chrome.content.Contains( m_mouseX, m_mouseY );
    layout.maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - layout.contentH ) );

    m_tabBar.SetBounds( static_cast<float>( layout.inputX + 14 ), static_cast<float>( layout.inputY + 44 ),
                        static_cast<float>( layout.inputW - 28 ), 44.0f );
    const float footerX = static_cast<float>( layout.inputX );
    const float footerY = static_cast<float>( layout.bottomY );
    const UIRect rendererBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect performanceBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererBounds.x, rendererBounds.y, rendererBounds.w, rendererBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterBounds.x, waterBounds.y, waterBounds.w, waterBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    m_vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    m_hitboxToggle.SetBounds( hitboxBounds.x, hitboxBounds.y, hitboxBounds.w, hitboxBounds.h );
    m_histogramToggle.SetBounds( performanceBounds.x, performanceBounds.y, performanceBounds.w, performanceBounds.h );
    m_timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );
    PrepareCompactToolsControls( layout.chrome );
    m_renderTargetCombo.SetBounds( layout.ContentX(), layout.ScrolledY( m_scrollY ) + UI_TARGETS_COMBO_Y,
                                   layout.ContentWidth(), 24.0f );
    m_renderTargetCombo.SetDropUp( false );
    return layout;
}


void UIWindowInteractionOwner::HandleWindowWheel( const InputControl::UIInputSnapshot& input, InGameUIInputResult& result,
                                                  const WindowPointerLayout& layout, const WindowOptionView& options,
                                                  double now )
{
    const int wheelDelta = input.wheelDelta;
    if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::UpdateFilterTyping( m_sceneTab, result, input, options.scenes.data(),
                                      static_cast<int>( options.scenes.size() ) );
    }

    bool wheelHandled = false;
    if ( wheelDelta != 0 && ( m_toolsTabCombo.IsOpen() || m_toolsDisplayCombo.IsOpen() ) )
    {
        if ( m_toolsTabCombo.IsOpen() )
        {
            m_toolsTabCombo.ScrollOptions( -wheelDelta / 120, static_cast<int>( InGameUITab::Count ) );
        }
        else
        {
            m_toolsDisplayCombo.ScrollOptions( -wheelDelta / 120, 7 );
        }
        return;
    }

    if ( wheelDelta != 0 && m_editorTab.objectCombo.IsOpen() && m_activeTab == InGameUITab::Editor )
    {
        m_editorTab.objectCombo.ScrollOptions( -wheelDelta / 120, EditorTab::OBJECT_TYPE_COUNT );
        wheelHandled = true;
    }

    if ( wheelDelta != 0 && m_sceneTab.combo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab, options.scenes.data(),
                                                   static_cast<int>( options.scenes.size() ), m_mouseX, m_mouseY, wheelDelta,
                                                   contentX, rowBase, contentW );
    }

    else if ( wheelDelta != 0 && m_sceneTab.recordingCombo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();
        wheelHandled = SceneTab::HandleRecordingComboWheel( m_sceneTab, static_cast<int>( options.recordings.size() ),
                                                            m_mouseX, m_mouseY, wheelDelta, contentX, rowBase, contentW );
    }

    if ( wheelDelta != 0 && layout.inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / UI_MOUSE_WHEEL_DELTA * ( layout.chrome.compact ? 12.0f : 42.0f );
        m_scrollY = std::clamp( m_scrollY, 0.0f, layout.maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }
}

bool UIWindowInteractionOwner::HandleWindowChromePress( InGameUIInputResult& result, const WindowPointerLayout& layout,
                                                        int screenW, int screenH, double now )
{
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( layout.hitBounds );
    if ( m_presentationEnabled && layout.inTitle )
    {
        // The shared pre-input pass handles close and resize before viewport
        // publication. The drawer title has no floating-window drag controls.
        return true;
    }
    const bool chromeHit = titleButtons.close.Contains( m_mouseX, m_mouseY ) ||
                           titleButtons.minimize.Contains( m_mouseX, m_mouseY ) ||
                           titleButtons.maximize.Contains( m_mouseX, m_mouseY ) || layout.inResize || layout.inTitle ||
                           layout.inTabs;
    if ( !chromeHit )
    {
        return false;
    }
    if ( titleButtons.close.Contains( m_mouseX, m_mouseY ) )
    {
        // Why: close and minimize are distinct promises in the drawn
        // chrome. Closing removes the panel entirely; the separate
        // minimize button keeps the bottom-left restore affordance.
        SetVisible( false, now );
    }
    else if ( titleButtons.minimize.Contains( m_mouseX, m_mouseY ) )
    {
        SetMinimized( true, now );
    }
    else if ( titleButtons.maximize.Contains( m_mouseX, m_mouseY ) )
    {
        SetMaximized( !m_window.isMaximized, screenW, screenH, now );
    }
    else if ( layout.inResize )
    {
        m_interaction.isResizing = true;
        m_interaction.resizeStartMouseX = m_mouseX;
        m_interaction.resizeStartMouseY = m_mouseY;
        m_interaction.resizeStartW = layout.inputW;
        m_interaction.resizeStartH = layout.inputH;
        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
    }
    else if ( layout.inTitle )
    {
        m_interaction.isDragging = true;
        m_interaction.dragOffsetX = m_mouseX - layout.inputX;
        m_interaction.dragOffsetY = m_mouseY - layout.inputY;
        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
    }
    else if ( layout.inTabs )
    {
        if ( layout.chrome.compact )
        {
            m_toolsTabCombo.ToggleOpen();
            return true;
        }
        static const int kTabCount = static_cast<int>( InGameUITab::Count );
        const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );

        if ( index >= 0 && index < kTabCount )
        {
            SetActiveTab( static_cast<InGameUITab>( index ) );
            m_scrollbarVisibleUntil = now + 1.0;
        }
    }
    return true;
}

bool UIWindowInteractionOwner::HandleOpenControlPress( InGameUIInputResult& result, const WindowPointerLayout& layout,
                                                       const WindowOptionView& options )
{
    if ( m_toolsTabCombo.IsOpen() )
    {
        const int index = m_toolsTabCombo.HitOption( m_mouseX, m_mouseY, static_cast<int>( InGameUITab::Count ) );
        m_toolsTabCombo.Close();
        if ( index >= 0 )
        {
            SetActiveTab( static_cast<InGameUITab>( index ) );
        }
        return true;
    }
    if ( m_toolsDisplayCombo.IsOpen() )
    {
        const int index = m_toolsDisplayCombo.HitOption( m_mouseX, m_mouseY, 7 );
        m_toolsDisplayCombo.Close();
        if ( index > 0 )
        {
            ApplyFooterAction( index, result );
        }
        return true;
    }
    const bool controlOpen = m_sceneTab.combo.IsOpen() || m_sceneTab.recordingCombo.IsOpen() ||
                             m_sceneTab.solverLabCombo.IsOpen() || CinematicTab::IsComboOpen( m_cinematicTab ) ||
                             m_renderTargetCombo.IsOpen() || m_editorTab.objectCombo.IsOpen() ||
                             m_reflectionCombo.IsOpen() || m_rendererCombo.IsOpen();
    if ( !controlOpen )
    {
        return false;
    }
    if ( m_sceneTab.combo.IsOpen() )
    {
        if ( m_activeTab == InGameUITab::Scene )
        {
            const float contentX = layout.ContentX();
            const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
            const float contentW = layout.ContentWidth();
            SceneTab::HandleOpenComboClick( m_sceneTab, result, options.scenes.data(),
                                            static_cast<int>( options.scenes.size() ), m_mouseX, m_mouseY, contentX, rowBase,
                                            contentW );
        }
        else
        {
            CloseSceneCombo();
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
        m_renderTargetCombo.Close();
    }
    else if ( m_sceneTab.solverLabCombo.IsOpen() )
    {
        SceneTab::HandleSolverLabClick( m_sceneTab, result, m_mouseX, m_mouseY );
    }
    else if ( m_sceneTab.recordingCombo.IsOpen() )
    {
        if ( m_activeTab == InGameUITab::Scene )
        {
            const float contentX = layout.ContentX();
            const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
            const float contentW = layout.ContentWidth();
            SceneTab::HandleOpenRecordingComboClick( m_sceneTab, result, static_cast<int>( options.recordings.size() ),
                                                     m_mouseX, m_mouseY, contentX, rowBase, contentW );
        }
        else
        {
            SceneTab::CloseRecordingCombo( m_sceneTab );
            m_sceneTab.solverLabCombo.Close();
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
        m_renderTargetCombo.Close();
    }
    else if ( CinematicTab::IsComboOpen( m_cinematicTab ) )
    {
        CinematicTab::HandleOpenComboClick( m_cinematicTab, result, options.scenes.data(),
                                            static_cast<int>( options.scenes.size() ), m_mouseX, m_mouseY );

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        m_editorTab.objectCombo.Close();
        m_renderTargetCombo.Close();
    }
    else if ( m_renderTargetCombo.IsOpen() )
    {
        if ( m_activeTab == InGameUITab::Targets )
        {
            const int option = m_renderTargetCombo.HitOption( m_mouseX, m_mouseY, m_lastRenderTargetPreviewCount );
            const bool optionDisabled = option >= 0 && option < 32 &&
                                        ( m_lastRenderTargetDisabledMask & ( 1u << option ) ) != 0;

            if ( option >= 0 && option < m_lastRenderTargetPreviewCount && !optionDisabled )
            {
                m_selectedRenderTargetPreview = option;
                m_renderTargetCombo.Close();
                m_cache.Reset();
            }
            else if ( m_renderTargetCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_renderTargetCombo.ToggleOpen();
            }
            else if ( option < 0 )
            {
                m_renderTargetCombo.Close();
            }
        }
        else
        {
            m_renderTargetCombo.Close();
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
    }
    else if ( m_editorTab.objectCombo.IsOpen() )
    {
        if ( m_activeTab == InGameUITab::Editor )
        {
            const float contentX = layout.ContentX();
            const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
            const float contentW = layout.ContentWidth();
            EditorTab::HandleContentClick( m_editorTab, result, m_mouseX, m_mouseY, contentX, rowBase, contentW );
        }
        else
        {
            m_editorTab.objectCombo.Close();
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
    }
    else if ( m_reflectionCombo.IsOpen() )
    {
        const int option = m_reflectionCombo.HitOption( m_mouseX, m_mouseY, 3 );

        if ( option >= 0 && option < 3 && ( ReflectionDisabledMask() & ( 1u << option ) ) == 0 )
        {
            result.commands.water.requestedWaterReflectionMode = option;
            m_reflectionCombo.Close();
        }
        else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
        {
            m_reflectionCombo.ToggleOpen();
        }
        else
        {
            m_reflectionCombo.Close();
        }

        m_rendererCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
        m_renderTargetCombo.Close();
    }
    else if ( m_rendererCombo.IsOpen() )
    {
        const int option = m_rendererCombo.HitOption( m_mouseX, m_mouseY, 1 );

        if ( option == 0 )
        {
            m_rendererCombo.Close();
        }
        else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
        {
            m_rendererCombo.ToggleOpen();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
        else if ( !m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
        {
            m_rendererCombo.Close();
        }
    }
    return true;
}

bool UIWindowInteractionOwner::HandleDiagnosticTabPress( const InputControl::UIInputSnapshot& input,
                                                         InGameUIInputResult& result, const WindowPointerLayout& layout,
                                                         const WindowOptionView& options, double now )
{
    const bool diagnosticTab = m_activeTab == InGameUITab::Profiler || m_activeTab == InGameUITab::Memory ||
                               m_activeTab == InGameUITab::Scene || m_activeTab == InGameUITab::Editor ||
                               m_activeTab == InGameUITab::Physics || m_activeTab == InGameUITab::Options;
    if ( !layout.inContent || !diagnosticTab )
    {
        return false;
    }
    if ( layout.inContent && m_activeTab == InGameUITab::Profiler )
    {
        const float contentW = layout.ContentWidth();

        if ( ProfilerTab::HandleContentClick( m_profilerTab, result, m_activeSlider, layout.inputX + 18, layout.contentY,
                                              contentW, m_scrollY, m_mouseX, m_mouseY, m_lastWorkerThreadCount,
                                              m_lastMaxWorkerThreadCount ) )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            m_scrollbarVisibleUntil = now + 1.2;
        }

        m_rendererCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Memory )
    {
        const float contentX = layout.ContentX();
        const float contentW = layout.ContentWidth();
        const float scrolledY = static_cast<float>( layout.contentY ) - m_scrollY;

        if ( MemoryTab::HandleContentClick( m_memoryOverlay, result, m_activeSlider, m_mouseX, m_mouseY, contentX, scrolledY,
                                            contentW ) )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            m_scrollbarVisibleUntil = now + 1.2;
        }

        m_rendererCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();
        bool sceneClickHandled = SceneTab::HandleHeaderClick( m_sceneTab, result, m_mouseX, m_mouseY, contentX, rowBase,
                                                              contentW );

        if ( !sceneClickHandled )
        {
            sceneClickHandled = SceneTab::HandleClosedComboClick( m_sceneTab, input, options.scenes.data(),
                                                                  static_cast<int>( options.scenes.size() ),
                                                                  options.selectedScene, m_mouseX, m_mouseY );
        }

        if ( !sceneClickHandled )
        {
            sceneClickHandled = SceneTab::HandleClosedRecordingComboClick( m_sceneTab,
                                                                           static_cast<int>( options.recordings.size() ),
                                                                           options.selectedRecording, m_mouseX, m_mouseY,
                                                                           contentX, rowBase, contentW );
        }

        if ( !sceneClickHandled )
        {
            sceneClickHandled = SceneTab::HandleSolverLabClick( m_sceneTab, result, m_mouseX, m_mouseY );
        }

        if ( !sceneClickHandled )
        {
            sceneClickHandled = SceneTab::HandleTimeScaleClick( m_sceneTab, result, m_activeSlider, m_mouseX, m_mouseY,
                                                                contentX, rowBase, contentW );
        }

        if ( !sceneClickHandled )
        {
            sceneClickHandled = SceneTab::HandlePlaybackClick( m_sceneTab, result, m_mouseX, m_mouseY, contentX, rowBase,
                                                               contentW );
        }
        if ( !sceneClickHandled )
        {
            sceneClickHandled = SceneTab::HandleForecastClick( m_sceneTab, result, m_mouseX, m_mouseY, contentX, rowBase,
                                                               contentW );
        }

        m_rendererCombo.Close();

        if ( sceneClickHandled )
        {
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Editor )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();
        EditorTab::HandleContentClick( m_editorTab, result, m_mouseX, m_mouseY, contentX, rowBase, contentW );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Physics )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();
        const int previousActiveSlider = m_activeSlider;

        if ( PhysicsTab::HandleContentClick( m_physicsTab, result, m_activeSlider, m_mouseX, m_mouseY, contentX, rowBase,
                                             contentW ) &&
             m_activeSlider != 0 && m_activeSlider != previousActiveSlider )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }

        m_rendererCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Options )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();

        if ( OptionsTab::HandleContentClick( m_optionsTab, result, m_activeSlider, m_mouseX, m_mouseY, contentX, rowBase,
                                             contentW, m_lastModelCapacity ) )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }

        m_presentation.preferences.theme = Style::CurrentTheme();

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
    }
    return true;
}

bool UIWindowInteractionOwner::HandleRenderTabPress( InGameUIInputResult& result, const WindowPointerLayout& layout )
{
    if ( !layout.inContent || m_activeTab != InGameUITab::Render )
    {
        return false;
    }
    const float contentX = layout.ContentX();
    const float contentW = layout.ContentWidth();
    const float scrolledY = static_cast<float>( layout.contentY ) - m_scrollY;
    bool capturedSlider = false;

    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    m_renderShadowToggle.SetBounds( contentX, scrolledY + UI_RENDER_FEATURE_START_Y, colW, 24.0f );
    m_saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W,
                                          scrolledY + UI_RENDER_FEATURE_START_Y, UI_RENDER_SAVE_BUTTON_W, 24.0f );

    if ( m_renderShadowToggle.HitTest( m_mouseX, m_mouseY ) )
    {
        result.commands.renderTuning.toggleShadows = true;
    }
    else if ( m_saveRenderDefaultsButton.HitTest( m_mouseX, m_mouseY ) )
    {
        result.commands.renderTuning.saveDefaults = true;
    }
    else
    {
        const float rowBase = scrolledY + UI_RENDER_START_Y;

        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            const float sliderY = RenderSliderY( i, rowBase );

            if ( RenderSliderStartsSection( i ) &&
                 kRenderSliderSpecs[i].section == UIRenderAuthoringSection::PredictionPaths )
            {
                m_saveTrajectoryStyleButton.SetBounds( contentX + contentW - UI_TRAJECTORY_SAVE_BUTTON_W,
                                                       sliderY - UI_RENDER_SECTION_H + 1.0f, UI_TRAJECTORY_SAVE_BUTTON_W,
                                                       20.0f );

                if ( m_saveTrajectoryStyleButton.HitTest( m_mouseX, m_mouseY ) )
                {
                    // One persistence owner writes the complete ordinary
                    // profile; this local affordance saves the edited path
                    // values without creating a second config writer.
                    result.commands.renderTuning.saveDefaults = true;
                    break;
                }
            }

            m_renderSliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );

            if ( m_renderSliders[i].HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = UI_RENDER_SLIDER_BASE + i;
                SetRenderSliderResult( result, m_renderSliders[i], m_mouseX, kRenderSliderSpecs[i] );
                capturedSlider = true;
                break;
            }
        }
    }

    if ( capturedSlider )
    {
        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
    }

    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    CinematicTab::CloseCombo( m_cinematicTab );
    m_renderTargetCombo.Close();
    return true;
}

bool UIWindowInteractionOwner::HandlePresentationTabPress( InGameUIInputResult& result, const WindowPointerLayout& layout )
{
    const bool presentationTab = m_activeTab == InGameUITab::Render || m_activeTab == InGameUITab::Targets ||
                                 m_activeTab == InGameUITab::Sky || m_activeTab == InGameUITab::Cinematic ||
                                 m_activeTab == InGameUITab::Keys;
    if ( !layout.inContent || !presentationTab )
    {
        return false;
    }
    if ( HandleRenderTabPress( result, layout ) )
    {
        return true;
    }
    if ( layout.inContent && m_activeTab == InGameUITab::Targets )
    {
        const float contentX = layout.ContentX();
        const float contentW = layout.ContentWidth();
        const float scrolledY = static_cast<float>( layout.contentY ) - m_scrollY;
        m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );

        if ( m_renderTargetCombo.HitBox( m_mouseX, m_mouseY ) )
        {
            m_renderTargetCombo.ToggleOpen();
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else
        {
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Sky )
    {
        const float contentX = layout.ContentX();
        const float contentW = layout.ContentWidth();
        const float scrolledY = static_cast<float>( layout.contentY ) - m_scrollY;
        const bool capturedSlider = SkyTab::HandleContentClick( m_skyTab, result, m_activeSlider, m_mouseX, m_mouseY,
                                                                contentX, scrolledY, contentW );

        if ( capturedSlider )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Cinematic )
    {
        const float contentX = layout.ContentX();
        const float contentW = layout.ContentWidth();
        const float scrolledY = static_cast<float>( layout.contentY ) - m_scrollY;
        const bool capturedSlider = CinematicTab::HandleContentClick( m_cinematicTab, result, m_activeSlider, m_mouseX,
                                                                      m_mouseY, contentX, scrolledY, contentW );

        if ( capturedSlider )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
    }
    else if ( layout.inContent && m_activeTab == InGameUITab::Keys )
    {
        const float contentX = layout.ContentX();
        const float rowBase = static_cast<float>( layout.contentY ) + 42.0f - m_scrollY;
        const float contentW = layout.ContentWidth();

        if ( ControlsTab::HandleContentClick( m_controlsTab, result, m_activeSlider, m_mouseX, m_mouseY, contentX, rowBase,
                                              contentW, m_lastModelCapacity, m_lastSolverBallCount, m_lastSolverBoxCount ) )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }

        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
    }
    return true;
}

void UIWindowInteractionOwner::PrepareCompactToolsControls( const ToolsChromeRects& chrome )
{
    if ( !chrome.compact )
    {
        m_reflectionCombo.SetLabelVisible( true );
        m_toolsTabCombo.Close();
        m_toolsDisplayCombo.Close();
        return;
    }
    m_toolsTabCombo.SetBounds( chrome.tabs.x, chrome.tabs.y, chrome.tabs.w, chrome.tabs.h );
    m_toolsTabCombo.SetLabelVisible( false );
    m_toolsTabCombo.SetScrollable( true );
    const UIRect footer { chrome.footer.x + 6.0f, chrome.footer.y + 2.0f, chrome.footer.w - 12.0f, 24.0f };
    m_toolsDisplayCombo.SetBounds( footer.x, footer.y, footer.w, footer.h );
    m_toolsDisplayCombo.SetLabelVisible( false );
    m_toolsDisplayCombo.SetScrollable( true );
    m_toolsDisplayCombo.SetDropUp( true );
    // The reflection submenu replaces the compact footer field while open.
    // Hidden wide controls have no geometry and cannot supply hover targets.
    m_reflectionCombo.SetBounds( footer.x, footer.y, footer.w, footer.h );
    m_reflectionCombo.SetLabelVisible( false );
    m_rendererCombo.SetBounds( 0, 0, 0, 0 );
    m_blurToggle.SetBounds( 0, 0, 0, 0 );
    m_vsyncToggle.SetBounds( 0, 0, 0, 0 );
    m_hitboxToggle.SetBounds( 0, 0, 0, 0 );
    m_histogramToggle.SetBounds( 0, 0, 0, 0 );
    m_timelineToggle.SetBounds( 0, 0, 0, 0 );
}

void UIWindowInteractionOwner::ApplyFooterAction( int action, InGameUIInputResult& result )
{
    switch ( action )
    {
    case 0:
    case 1:
        if ( action == 0 )
        {
            m_rendererCombo.ToggleOpen();
            m_reflectionCombo.Close();
        }
        else
        {
            m_reflectionCombo.ToggleOpen();
            m_rendererCombo.Close();
        }
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
        m_renderTargetCombo.Close();
        break;
    case 2:
        SetBlurEnabled( !m_blurPreviewEnabled );
        break;
    case 3:
        result.commands.renderer.toggleVsync = true;
        break;
    case 4:
        SetHitboxOverlayEnabled( !m_hitboxOverlayEnabled );
        break;
    case 5:
        SetPerformanceHistogramEnabled( !ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
        break;
    case 6:
        SetProfilerTimelineEnabled( !ProfilerTab::TimelineEnabled( m_profilerTab ) );
        break;
    default:
        break;
    }
}

void UIWindowInteractionOwner::HandleWindowFallbackPress( InGameUIInputResult& result, const WindowPointerLayout& layout )
{
    if ( layout.InFooter( m_mouseY ) )
    {
        if ( layout.chrome.compact )
        {
            if ( m_toolsDisplayCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_toolsDisplayCombo.ToggleOpen();
            }
            return;
        }
        const bool hits[] = { m_rendererCombo.HitBox( m_mouseX, m_mouseY ),  m_reflectionCombo.HitBox( m_mouseX, m_mouseY ),
                              m_blurToggle.HitTest( m_mouseX, m_mouseY ),    m_vsyncToggle.HitTest( m_mouseX, m_mouseY ),
                              m_hitboxToggle.HitTest( m_mouseX, m_mouseY ),  m_histogramToggle.HitTest( m_mouseX, m_mouseY ),
                              m_timelineToggle.HitTest( m_mouseX, m_mouseY ) };
        for ( int action = 0; action < 7; ++action )
        {
            if ( hits[action] )
            {
                ApplyFooterAction( action, result );
                break;
            }
        }
    }
    else
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_editorTab.objectCombo.Close();
        m_renderTargetCombo.Close();
    }
}

void UIWindowInteractionOwner::HandleWindowPress( const InputControl::UIInputSnapshot& input, InGameUIInputResult& result,
                                                  const WindowPointerLayout& layout, const WindowOptionView& options,
                                                  int screenW, int screenH, double now )
{
    // Popups draw above the drawer title and tabs, so their selection or
    // dismissal owns the press before any underlying chrome can act.
    if ( HandleOpenControlPress( result, layout, options ) ||
         HandleWindowChromePress( result, layout, screenW, screenH, now ) ||
         HandleDiagnosticTabPress( input, result, layout, options, now ) || HandlePresentationTabPress( result, layout ) )
    {
        return;
    }
    HandleWindowFallbackPress( result, layout );
}


InGameUIInputResult UIWindowInteractionOwner::UpdateInput( const InputControl::UIInputSnapshot& input,
                                                           const SceneNavigationModel& sceneNavigation, int screenWidth,
                                                           int screenHeight, double now, bool editorModeEnabled,
                                                           bool placementModeEnabled, bool placeStaticObject,
                                                           bool autoTerrainAlign, uint32_t cameraModeEnabledMask )
{
    InGameUIInputResult result;
    const int screenW = (std::max)( 1, screenWidth );
    const int screenH = (std::max)( 1, screenHeight );
    const WindowOptionView options = BuildWindowOptionView( sceneNavigation );

    // Concept: UI input produces command intents and capture state. The run loop
    // owns applying scene, physics, renderer, and editor mutations.
    cameraModeEnabledMask &= ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u;
    m_blocksCameraMouse = false;
    const int wheelDelta = input.wheelDelta;
    result.unhandledWheelDelta = wheelDelta;
    m_mouseX = input.mouseX;
    m_mouseY = input.mouseY;

    m_lastScreenW = screenW;
    m_tooltipGestureActive = input.leftDown || input.rightDown || input.middleDown || input.wheelDelta != 0;
    m_lastScreenH = screenH;
    const bool leftNow = input.leftDown;
    const bool popupWasOpen = HasOpenPopup();

    // Concept: the standalone histogram remains interactive even when the main
    // diagnostics window is hidden, so it gets first chance at mouse input.
    const bool histogramWasInteracting = ProfilerTab::PerformanceHistogramIsInteracting( m_profilerTab );

    if ( ( !popupWasOpen || m_profilerTab.histogramSelectorOpen ) &&
         ProfilerTab::HandlePerformanceHistogramInput( m_profilerTab, result, screenW, screenH, m_mouseX, m_mouseY, leftNow,
                                                       input.leftPressed, input.leftReleased, wheelDelta ) )
    {
        const bool histogramIsInteracting = ProfilerTab::PerformanceHistogramIsInteracting( m_profilerTab );

        if ( input.leftPressed && histogramIsInteracting && !histogramWasInteracting )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }

        if ( input.leftReleased && histogramWasInteracting )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
        }

        m_blocksCameraMouse = true;
        return result;
    }


    if ( m_presentationEnabled && m_presentation.workspace == Workspace::Scene && !m_interaction.isResizing &&
         !m_editorMiniPalettePressActive &&
         ( m_cameraModeCombo.IsOpen() ||
           ( !HasOpenPopup() && ComputeHeaderRects( m_presentationRects.header ).camera.Contains( m_mouseX, m_mouseY ) ) ) )
    {
        const MinimizedControlResult camera = HandleMinimizedCameraMode( input, {}, false, cameraModeEnabledMask, result );
        if ( camera.BlocksCamera() )
        {
            m_blocksCameraMouse = true;
            result.unhandledWheelDelta = 0;
            return result;
        }
    }
    if ( m_presentationEnabled && m_presentation.workspace == Workspace::Scene && editorModeEnabled && !HasOpenPopup() &&
         !m_interaction.isResizing )
    {
        const MinimizedControlResult palette = HandleEditorMiniPalette( input, screenW, screenH, {}, now, result );
        if ( palette.BlocksCamera() || m_editorMiniPalettePressActive )
        {
            m_blocksCameraMouse = true;
            result.unhandledWheelDelta = 0;
            return result;
        }
    }
    if ( HandleEditorDockInput( input, result ) )
    {
        return result;
    }

    if ( m_presentationEnabled && !popupWasOpen &&
         ( m_presentationRects.replayControls.Contains( m_mouseX, m_mouseY ) ||
           m_presentationRects.right.Contains( m_mouseX, m_mouseY ) ||
           m_presentationRects.replayDetails.Contains( m_mouseX, m_mouseY ) ) &&
         m_activeSlider == 0 && !m_interaction.isResizing && !m_toolsTabPressed )
    {
        result.unhandledWheelDelta = 0;
        return result;
    }

    if ( m_presentationEnabled && !popupWasOpen &&
         ( DiagnosticDetailsBounds( m_presentationRects.markerHistory ).Contains( m_mouseX, m_mouseY ) ||
           DiagnosticDetailsBounds( m_presentationRects.memoryWaterline ).Contains( m_mouseX, m_mouseY ) ) &&
         m_activeSlider == 0 && !m_interaction.isResizing )
    {
        result.unhandledWheelDelta = 0;
        return result;
    }

    if ( m_presentationEnabled && ( m_interaction.isResizing || m_toolsTabPressed ) )
    {
        m_blocksCameraMouse = true;
        result.unhandledWheelDelta = 0;
        if ( input.leftPressed )
        {
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }
        if ( input.leftReleased )
        {
            m_toolsTabPressed = false;
            m_interaction.isResizing = false;
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
        }
        return result;
    }

    if ( m_presentationEnabled && !popupWasOpen && m_presentationRects.header.Contains( m_mouseX, m_mouseY ) &&
         m_activeSlider == 0 && !m_interaction.isDragging && !m_interaction.isResizing &&
         !ProfilerTab::PerformanceHistogramIsInteracting( m_profilerTab ) )
    {
        // Presentation clicks do not enter the interactive scene or mutate
        // simulation state. The router still observes mouse capture below.
        result.unhandledWheelDelta = 0;
        return result;
    }


    if ( !m_window.isVisible )
    {
        return result;
    }

    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );

    const int minW = 520;
    const int minH = 250;
    const int margin = 10;

    if ( !m_presentationEnabled && !m_window.hasAppliedDefaultPlacement )
    {
        Chrome::ApplyDefaultWindowPlacement( m_window, screenW, screenH );
    }

    if ( !m_presentationEnabled )
    {
        Chrome::ClampWindowToScreen( m_window, screenW, screenH, minW, minH, margin );
    }

    if ( m_presentationEnabled && m_window.isMinimized )
    {
        return result;
    }
    if ( m_window.isMinimized )
    {
        return HandleMinimizedInput( input, screenW, screenH, now, editorModeEnabled, placementModeEnabled,
                                     placeStaticObject, autoTerrainAlign, cameraModeEnabledMask );
    }

    const WindowPointerLayout layout = PrepareWindowPointerLayout( now );
    if ( layout.inside )
    {
        result.unhandledWheelDelta = 0;
    }

    if ( ( leftNow && ( layout.inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0 ) ) ||
         ( wheelDelta != 0 && layout.inside ) )
    {
        result.commands.ui.userInteracted = true;
    }

    HandleWindowWheel( input, result, layout, options, now );

    if ( input.leftPressed )
    {
        HandleWindowPress( input, result, layout, options, screenW, screenH, now );
    }

    if ( leftNow && m_activeSlider != 0 )
    {
        UpdateActiveSliderInput( result );
    }

    UpdateWindowDragAndResize( leftNow, screenW, screenH, now );

    if ( input.leftReleased )
    {
        FinishPointerRelease( result );
    }

    m_scrollY = std::clamp( m_scrollY, 0.0f, layout.maxScroll );
    m_blocksCameraMouse = layout.inside || popupWasOpen || m_interaction.isDragging || m_interaction.isResizing ||
                          m_activeSlider != 0;

    return result;
}
