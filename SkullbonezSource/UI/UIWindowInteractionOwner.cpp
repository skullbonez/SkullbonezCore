/*
File: SkullbonezSource/UI/UIWindowInteractionOwner.cpp
Purpose:
  Implements stateful in-game UI window and widget interaction ownership.

Summary:
  This owner translates detached UI input snapshots into typed command values
  and retains the window, widget, tab, and gesture state shared with drawing.
  It also maps recording anchors through window-local normalized coordinates so
  playback follows the same UI region after layout resize. InGameUI borrows only
  a synchronous WidgetView and is never reachable from this owner.

Invariants:
  - Device input produces commands; runtime subsystem mutation remains outside UI.
  - WidgetView is not retained beyond the caller's immediate draw operation.
  - Layout bounds and hit-test bounds are the same widget state.
  - Anchor resolution uses current window bounds and never retains a viewport pointer.

Related:
  - SkullbonezSource/UI/UIWindowInteractionOwner.h
  - SkullbonezSource/UI/UI.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "UI.h"
#include "UIWindowInteractionOwner.h"
#include "../Core/Profiler.h"
#include "UIFrameComposition.h"
#include "UIInput.h"
#include "UIDrawWidgets.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;
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

void UIWindowInteractionOwner::ResetPresentationResources()
{
    m_backdropBlur.ResetResources();
    m_cache.Reset();
}

UIWindowInteractionOwner::WidgetView UIWindowInteractionOwner::Widgets()
{
    // Lifetime: the draw composer borrows these references synchronously. The
    // owner remains alive for the whole call and no reference may be retained.
    return { m_window,
             m_interaction,
             m_blurPreviewEnabled,
             m_activeTab,
             m_tabBar,
             m_blurToggle,
             m_vsyncToggle,
             m_timelineToggle,
             m_histogramToggle,
             m_hitboxToggle,
             m_rendererCombo,
             m_reflectionCombo,
             m_renderTargetCombo,
             m_cameraModeCombo,
             m_cinematicMasterToggle,
             m_renderShadowToggle,
             m_saveRenderDefaultsButton,
             m_saveTrajectoryStyleButton,
             m_renderSliders,
             m_backdropBlur,
             m_cache,
             m_scrollBar,
             m_mouseX,
             m_mouseY,
             m_lastScreenW,
             m_lastScreenH,
             m_lastModelCapacity,
             m_lastSolverBallCount,
             m_lastSolverBoxCount,
             m_lastWorkerThreadCount,
             m_lastMaxWorkerThreadCount,
             m_lastRenderTargetPreviewCount,
             m_lastRenderTargetDisabledMask,
             m_selectedRenderTargetPreview,
             m_controlsTab,
             m_editorTab,
             m_optionsTab,
             m_physicsTab,
             m_profilerTab,
             m_memoryOverlay,
             m_sceneTab,
             m_skyTab,
             m_cinematicTab,
             m_scrollY,
             m_scrollbarVisibleUntil,
             m_activeSlider,
             m_hitboxOverlayEnabled,
             m_editorMiniPalettePressActive,
             m_editorMiniPaletteFlyoutOpen,
             m_editorMiniPalettePressedEntry,
             m_editorMiniPalettePressedObjectType,
             m_editorMiniPalettePressedTreePlacement,
             m_editorMiniPalettePressedHoldMode,
             m_editorMiniPalettePressStart };
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
    m_cache.Reset();
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Visibility );

    if ( visible )
    {
        m_window.isMinimized = false;
        m_scrollbarVisibleUntil = now + 1.2;
        CancelEditorMiniPaletteInteraction();
    }
    else
    {
        m_window.isMinimized = true;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        m_interaction.blocksCameraMouse = false;
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


void UIWindowInteractionOwner::SetMinimized( bool minimized, double now )
{
    if ( m_window.isMinimized == minimized )
    {
        return;
    }

    const UIRect currentBounds = Chrome::WindowRect( m_window );
    const UIRect minimizedBounds = MinimizedRect( m_lastScreenW, m_lastScreenH, m_window.minimizedWidth );
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    CancelEditorMiniPaletteInteraction();

    if ( minimized )
    {
        m_window.isMinimized = true;
        Chrome::BeginWindowAnimation( m_window, currentBounds, minimizedBounds, now, true );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        CinematicTab::CloseCombo( m_cinematicTab );
        m_renderTargetCombo.Close();
        m_cameraModeCombo.Close();
        m_activeSlider = 0;
    }
    else
    {
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
    const int tabIndex = static_cast<int>( tab );

    if ( tabIndex < 0 || tabIndex >= static_cast<int>( InGameUITab::Count ) )
    {
        tab = InGameUITab::Scene;
    }

    m_activeTab = tab;
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    CloseSceneCombo();
    m_editorTab.objectCombo.Close();
    CinematicTab::CloseCombo( m_cinematicTab );
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Content );
    m_cache.Reset();
}


InGameUITab UIWindowInteractionOwner::GetActiveTab() const
{
    return m_activeTab;
}


void UIWindowInteractionOwner::CancelInputCapture()
{
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    m_editorTab.objectCombo.Close();
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    CancelEditorMiniPaletteInteraction();
    ProfilerTab::CancelPerformanceHistogramInteraction( m_profilerTab );
}


bool UIWindowInteractionOwner::BlocksCameraMouse() const
{
    return m_interaction.blocksCameraMouse;
}


bool UIWindowInteractionOwner::BlocksKeyboard() const
{
    return m_window.isVisible && !m_window.isMinimized &&
           ( m_sceneTab.combo.IsOpen() || CinematicTab::IsComboOpen( m_cinematicTab ) || m_editorTab.objectCombo.IsOpen() ||
             m_renderTargetCombo.IsOpen() );
}


bool UIWindowInteractionOwner::WantsNativeMouseCursor() const
{
    return ( m_window.isVisible && !m_window.isMinimized ) || m_interaction.blocksCameraMouse ||
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
    return m_window.isVisible || IsPerformanceHistogramEnabled() || IsMemoryOverlayEnabled();
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
    m_scrollbarVisibleUntil = 1.2;
    m_cache.Reset();
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
        return EditorTab::ContentHeight();
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
}


InputControl::UIPointerOverride UIWindowInteractionOwner::InputOverride() const
{
    return InputControl::UIPointerOverride { m_hasMouseOverride, m_mouseOverrideX, m_mouseOverrideY };
}


InGameUIInputResult
UIWindowInteractionOwner::UpdateInput( const InputControl::UIInputSnapshot& input, int screenWidth, int screenHeight,
                                       double now, bool editorModeEnabled, bool editorPlacementMode, bool editorPlaceStatic,
                                       bool editorTerrainAlign, int cameraModeIndex, uint32_t cameraModeEnabledMask,
                                       std::span<const char* const> sceneOptionView, int selectedSceneOption )
{
    InGameUIInputResult result;
    int screenW = screenWidth;
    int screenH = screenHeight;
    const char* const* sceneOptions = sceneOptionView.data();
    const int sceneOptionCount = static_cast<int>( sceneOptionView.size() );

    // Concept: UI input produces command intents and capture state. The run loop
    // owns applying scene, physics, renderer, and editor mutations.
    cameraModeIndex = std::clamp( cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
    cameraModeEnabledMask &= ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u;
    m_interaction.blocksCameraMouse = false;
    const int wheelDelta = input.wheelDelta;
    result.unhandledWheelDelta = wheelDelta;
    m_mouseX = input.mouseX;
    m_mouseY = input.mouseY;

    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    const bool leftNow = input.leftDown;

    // Concept: the standalone histogram remains interactive even when the main
    // diagnostics window is hidden, so it gets first chance at mouse input.
    const bool histogramWasInteracting = ProfilerTab::PerformanceHistogramIsInteracting( m_profilerTab );

    if ( ProfilerTab::HandlePerformanceHistogramInput( m_profilerTab, result, screenW, screenH, m_mouseX, m_mouseY, leftNow,
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

        m_interaction.blocksCameraMouse = true;
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
    const int titleH = 44;
    const int tabH = 44;
    const int bottomH = 78;
    const int contentPad = 18;
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );

    if ( !m_window.hasAppliedDefaultPlacement )
    {
        Chrome::ApplyDefaultWindowPlacement( m_window, screenW, screenH );
    }

    Chrome::ClampWindowToScreen( m_window, screenW, screenH, minW, minH, margin );

    if ( m_window.isMinimized )
    {
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
        const bool showEditorMiniPalette = editorModeEnabled;
        const UIRect cameraModeComboBounds = MinimizedCameraModeComboBounds( minimized );
        m_cameraModeCombo.SetLabelVisible( false );
        m_cameraModeCombo.SetBounds( cameraModeComboBounds.x, cameraModeComboBounds.y, cameraModeComboBounds.w,
                                     cameraModeComboBounds.h );

        m_cameraModeCombo.SetDropUp( true );
        bool cameraModeComboHandled = false;
        bool insideCameraModeCombo = false;
        const uint32_t cameraModeDisabledMask = ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) & ~cameraModeEnabledMask;

        if ( !showEditorMiniPalette )
        {
            if ( m_editorMiniPalettePressActive )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
            }

            CancelEditorMiniPaletteInteraction();

            const bool comboOptionHit = m_cameraModeCombo.IsOpen() &&
                                        m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT ) >= 0;

            const bool comboDropdownHit = m_cameraModeCombo.IsOpen() &&
                                          m_cameraModeCombo.DropdownBounds( CAMERA_MODE_OPTION_COUNT )
                                              .Contains( m_mouseX, m_mouseY );

            insideCameraModeCombo = m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) || comboOptionHit || comboDropdownHit;

            if ( input.leftPressed && m_cameraModeCombo.IsOpen() )
            {
                const int option = m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT );
                const bool optionDisabled = option >= 0 && option < 32 && ( cameraModeDisabledMask & ( 1u << option ) ) != 0;

                if ( option >= 0 && option < CAMERA_MODE_OPTION_COUNT && !optionDisabled )
                {
                    result.commands.run.requestedCameraMode = option;
                    result.commands.ui.userInteracted = true;
                    m_cameraModeCombo.Close();
                    m_cache.Reset();
                    cameraModeComboHandled = true;
                }
                else if ( m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) )
                {
                    m_cameraModeCombo.ToggleOpen();
                    result.commands.ui.userInteracted = true;
                    m_cache.Reset();
                    cameraModeComboHandled = true;
                }
                else if ( option < 0 )
                {
                    m_cameraModeCombo.Close();
                    result.commands.ui.userInteracted = true;
                    m_cache.Reset();
                    cameraModeComboHandled = true;
                }
            }
            else if ( input.leftPressed && m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_cameraModeCombo.ToggleOpen();
                result.commands.ui.userInteracted = true;
                m_cache.Reset();
                cameraModeComboHandled = true;
            }
        }
        else
        {
            m_cameraModeCombo.Close();
        }

        EditorMiniPaletteLayout editorMiniPalette;
        bool editorMiniPaletteHandled = false;
        bool insideEditorMiniPalette = false;
        bool editorMinimizedStatusHandled = false;
        bool insideEditorMinimizedStatusControl = false;

        if ( showEditorMiniPalette )
        {
            const EditorMinimizedStatusLayout statusLayout = BuildEditorMinimizedStatusLayout( minimized,
                                                                                               editorPlacementMode,
                                                                                               editorPlaceStatic,
                                                                                               editorTerrainAlign );

            const bool insideModeChip = statusLayout.modeChip.Contains( m_mouseX, m_mouseY );
            const bool insideBodyChip = statusLayout.bodyChip.Contains( m_mouseX, m_mouseY );
            const bool insideAlignChip = statusLayout.alignChip.Contains( m_mouseX, m_mouseY );
            insideEditorMinimizedStatusControl = insideModeChip || insideBodyChip || insideAlignChip;

            if ( input.leftPressed && insideModeChip )
            {
                result.commands.editor.togglePlacementMode = true;
                result.commands.ui.userInteracted = true;
                editorMinimizedStatusHandled = true;
            }
            else if ( input.leftPressed && insideBodyChip )
            {
                result.commands.editor.togglePlaceStatic = true;
                result.commands.ui.userInteracted = true;
                editorMinimizedStatusHandled = true;
            }
            else if ( input.leftPressed && insideAlignChip )
            {
                result.commands.editor.toggleTerrainAlign = true;
                result.commands.ui.userInteracted = true;
                editorMinimizedStatusHandled = true;
            }

            if ( m_editorMiniPalettePressActive && !leftNow && !input.leftReleased )
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

            editorMiniPalette = BuildEditorMiniPaletteLayout( screenW, screenH, minimized, m_editorMiniPalettePressedEntry,
                                                              m_editorMiniPaletteFlyoutOpen );

            insideEditorMiniPalette = EditorMiniPaletteContains( editorMiniPalette, m_mouseX, m_mouseY );

            const auto SelectEditorMiniPaletteObject = [&]( int objectType, bool requestPlaceStatic = false,
                                                            bool placeStatic = false ) -> void
            {
                result.commands.editor.requestedObjectType = std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );

                if ( requestPlaceStatic )
                {
                    result.commands.editor.requestPlaceStatic = true;
                    result.commands.editor.requestedPlaceStatic = placeStatic;
                }

                result.commands.editor.enterPlacementMode = true;
                result.commands.ui.userInteracted = true;
                editorMiniPaletteHandled = true;
            };

            if ( input.leftPressed && insideEditorMiniPalette )
            {
                const int pressedButton = HitEditorMiniPaletteButton( editorMiniPalette, m_mouseX, m_mouseY );

                if ( pressedButton >= 0 )
                {
                    const EditorMiniPaletteEntry& entry = kEditorMiniPaletteEntries[pressedButton];

                    if ( entry.holdMode != EDITOR_MINI_HOLD_MODE_NONE )
                    {
                        m_editorMiniPalettePressActive = true;
                        m_editorMiniPaletteFlyoutOpen = false;
                        m_editorMiniPalettePressedEntry = pressedButton;
                        m_editorMiniPalettePressedObjectType = entry.objectType;
                        m_editorMiniPalettePressedTreePlacement = entry.treePlacement;
                        m_editorMiniPalettePressedHoldMode = entry.holdMode;
                        m_editorMiniPalettePressStart = now;
                        result.commands.ui.userInteracted = true;
                        editorMiniPaletteHandled = true;
                        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
                    }
                    else
                    {
                        SelectEditorMiniPaletteObject( entry.objectType );
                    }
                }
            }

            if ( m_editorMiniPalettePressActive )
            {
                result.commands.ui.userInteracted = true;
                editorMiniPaletteHandled = true;

                if ( input.leftReleased )
                {
                    int selectedObjectType = -1;
                    bool requestPlaceStatic = false;
                    bool requestedPlaceStatic = false;

                    if ( m_editorMiniPaletteFlyoutOpen )
                    {
                        const int flyoutOption = HitEditorMiniPaletteFlyoutOption( editorMiniPalette, m_mouseX, m_mouseY );

                        if ( flyoutOption >= 0 )
                        {
                            if ( m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
                            {
                                selectedObjectType = EditorMiniTreeObjectType( flyoutOption,
                                                                               m_editorMiniPalettePressedTreePlacement );
                            }
                            else if ( m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
                            {
                                selectedObjectType = EditorMiniRagdollObjectType( flyoutOption );
                            }
                        }
                    }
                    else if ( m_editorMiniPalettePressedEntry >= 0 &&
                              m_editorMiniPalettePressedEntry < editorMiniPalette.buttonCount &&
                              editorMiniPalette.buttons[m_editorMiniPalettePressedEntry].Contains( m_mouseX, m_mouseY ) )
                    {
                        selectedObjectType = m_editorMiniPalettePressedObjectType;
                    }

                    if ( selectedObjectType >= 0 )
                    {
                        requestPlaceStatic = EditorMiniSelectionRequestsStatic( m_editorMiniPalettePressedHoldMode,
                                                                                m_editorMiniPalettePressedTreePlacement,
                                                                                requestedPlaceStatic );

                        SelectEditorMiniPaletteObject( selectedObjectType, requestPlaceStatic, requestedPlaceStatic );
                    }

                    CancelEditorMiniPaletteInteraction();
                    result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
                }
            }
        }

        if ( insideMinimized || insideCameraModeCombo || cameraModeComboHandled || insideEditorMiniPalette ||
             insideEditorMinimizedStatusControl || m_editorMiniPalettePressActive )
        {
            result.unhandledWheelDelta = 0;
        }

        if ( input.leftPressed && insideMinimized && !cameraModeComboHandled && !editorMiniPaletteHandled &&
             !editorMinimizedStatusHandled )
        {
            SetMinimized( false, now );
            result.commands.ui.userInteracted = true;
        }

        m_interaction.blocksCameraMouse = insideMinimized || insideCameraModeCombo || cameraModeComboHandled ||
                                          insideEditorMiniPalette || insideEditorMinimizedStatusControl ||
                                          m_editorMiniPalettePressActive;

        return result;
    }

    const UIRect inputBounds = Chrome::CurrentWindowRect( m_window, now );
    const int inputX = static_cast<int>( std::round( inputBounds.x ) );
    const int inputY = static_cast<int>( std::round( inputBounds.y ) );
    const int inputW = static_cast<int>( std::round( inputBounds.w ) );
    const int inputH = static_cast<int>( std::round( inputBounds.h ) );
    const UIRect inputHitBounds = { static_cast<float>( inputX ), static_cast<float>( inputY ), static_cast<float>( inputW ),
                                    static_cast<float>( inputH ) };

    const bool inside = m_mouseX >= inputX && m_mouseX <= inputX + inputW && m_mouseY >= inputY &&
                        m_mouseY <= inputY + inputH;

    const bool inTitle = inside && m_mouseY < inputY + titleH;
    const bool inTabs = inside && m_mouseY >= inputY + titleH && m_mouseY < inputY + titleH + tabH;
    const bool inResize = !m_window.isMaximized && inside && Chrome::IsResizeHotspot( inputHitBounds, m_mouseX, m_mouseY );

    const int contentY = inputY + titleH + tabH + 12;
    const int contentH = (std::max)( 24, inputH - titleH - tabH - bottomH - contentPad );
    const int bottomY = inputY + inputH - bottomH;
    const bool inContent = inside && m_mouseY >= contentY && m_mouseY <= contentY + contentH;
    const float maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - contentH ) );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( inputHitBounds );

    if ( inside )
    {
        result.unhandledWheelDelta = 0;
    }

    m_tabBar.SetBounds( static_cast<float>( inputX + 14 ), static_cast<float>( inputY + titleH ),
                        static_cast<float>( inputW - 28 ), static_cast<float>( tabH ) );

    const float footerX = static_cast<float>( inputX );
    const float footerY = static_cast<float>( bottomY );
    const UIRect rendererComboBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterComboBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect perfBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );

    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    m_vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    m_hitboxToggle.SetBounds( hitboxBounds.x, hitboxBounds.y, hitboxBounds.w, hitboxBounds.h );
    m_histogramToggle.SetBounds( perfBounds.x, perfBounds.y, perfBounds.w, perfBounds.h );
    m_timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
        m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );
        m_renderTargetCombo.SetDropUp( false );
    }

    if ( ( leftNow && ( inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0 ) ) ||
         ( wheelDelta != 0 && inside ) )
    {
        result.commands.ui.userInteracted = true;
    }

    if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::UpdateFilterTyping( m_sceneTab, result, input, sceneOptions, sceneOptionCount );
    }

    bool wheelHandled = false;

    if ( wheelDelta != 0 && m_sceneTab.combo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab, sceneOptions, sceneOptionCount, m_mouseX, m_mouseY,
                                                   wheelDelta, contentX, rowBase, contentW );
    }

    if ( wheelDelta != 0 && inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / UI_MOUSE_WHEEL_DELTA * 42.0f;
        m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }

    if ( input.leftPressed )
    {
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
        else if ( inResize )
        {
            m_interaction.isResizing = true;
            m_interaction.resizeStartMouseX = m_mouseX;
            m_interaction.resizeStartMouseY = m_mouseY;
            m_interaction.resizeStartW = inputW;
            m_interaction.resizeStartH = inputH;
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }
        else if ( inTitle )
        {
            m_interaction.isDragging = true;
            m_interaction.dragOffsetX = m_mouseX - inputX;
            m_interaction.dragOffsetY = m_mouseY - inputY;
            result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
        }
        else if ( inTabs )
        {
            static const int kTabCount = static_cast<int>( InGameUITab::Count );
            const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );

            if ( index >= 0 && index < kTabCount )
            {
                SetActiveTab( static_cast<InGameUITab>( index ) );
                m_scrollbarVisibleUntil = now + 1.0;
            }
        }
        else if ( m_sceneTab.combo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Scene )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                SceneTab::HandleOpenComboClick( m_sceneTab, result, sceneOptions, sceneOptionCount, m_mouseX, m_mouseY,
                                                contentX, rowBase, contentW );
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
        else if ( CinematicTab::IsComboOpen( m_cinematicTab ) )
        {
            CinematicTab::HandleOpenComboClick( m_cinematicTab, result, sceneOptions, sceneOptionCount, m_mouseX, m_mouseY );

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
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
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

            if ( option >= 0 && option < 3 )
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
        else if ( inContent && m_activeTab == InGameUITab::Profiler )
        {
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;

            if ( ProfilerTab::HandleContentClick( m_profilerTab, result, m_activeSlider, inputX + contentPad, contentY,
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
        else if ( inContent && m_activeTab == InGameUITab::Memory )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;

            if ( MemoryTab::HandleContentClick( m_memoryOverlay, result, m_activeSlider, m_mouseX, m_mouseY, contentX,
                                                scrolledY, contentW ) )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
                m_scrollbarVisibleUntil = now + 1.2;
            }

            m_rendererCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Scene )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            bool sceneClickHandled = SceneTab::HandleHeaderClick( m_sceneTab, result, m_mouseX, m_mouseY, contentX, rowBase,
                                                                  contentW );

            if ( !sceneClickHandled )
            {
                sceneClickHandled = SceneTab::HandleClosedComboClick( m_sceneTab, input, sceneOptions, sceneOptionCount,
                                                                      selectedSceneOption, m_mouseX, m_mouseY );
            }

            if ( !sceneClickHandled )
            {
                sceneClickHandled = SceneTab::HandleTimeScaleClick( m_sceneTab, result, m_activeSlider, m_mouseX, m_mouseY,
                                                                    contentX, rowBase, contentW );
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
        else if ( inContent && m_activeTab == InGameUITab::Editor )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            EditorTab::HandleContentClick( m_editorTab, result, m_mouseX, m_mouseY, contentX, rowBase, contentW );
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CloseSceneCombo();
            CinematicTab::CloseCombo( m_cinematicTab );
        }
        else if ( inContent && m_activeTab == InGameUITab::Physics )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
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
        else if ( inContent && m_activeTab == InGameUITab::Options )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;

            if ( OptionsTab::HandleContentClick( m_optionsTab, result, m_activeSlider, m_mouseX, m_mouseY, contentX, rowBase,
                                                 contentW, m_lastModelCapacity ) )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            }

            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Render )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
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
                                                               sliderY - UI_RENDER_SECTION_H + 1.0f,
                                                               UI_TRAJECTORY_SAVE_BUTTON_W, 20.0f );

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
        }
        else if ( inContent && m_activeTab == InGameUITab::Targets )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
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
        else if ( inContent && m_activeTab == InGameUITab::Sky )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
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
        else if ( inContent && m_activeTab == InGameUITab::Cinematic )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            const bool capturedSlider = CinematicTab::HandleContentClick( m_cinematicTab, result, m_activeSlider, m_mouseX,
                                                                          m_mouseY, contentX, scrolledY, contentW );

            if ( capturedSlider )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            }

            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Keys )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;

            if ( ControlsTab::HandleContentClick( m_controlsTab, result, m_activeSlider, m_mouseX, m_mouseY, contentX,
                                                  rowBase, contentW, m_lastModelCapacity, m_lastSolverBallCount,
                                                  m_lastSolverBoxCount ) )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            }

            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
        }
        else if ( inside && m_mouseY >= inputY + inputH - bottomH )
        {
            if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
                m_rendererCombo.Close();
                CloseSceneCombo();
                CinematicTab::CloseCombo( m_cinematicTab );
                m_editorTab.objectCombo.Close();
                m_renderTargetCombo.Close();
            }
            else if ( m_blurToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_blurPreviewEnabled = !m_blurPreviewEnabled;
                m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
            }
            else if ( m_vsyncToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.renderer.toggleVsync = true;
            }
            else if ( m_hitboxToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetHitboxOverlayEnabled( !m_hitboxOverlayEnabled );
            }
            else if ( m_histogramToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetPerformanceHistogramEnabled( !ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
            }
            else if ( m_timelineToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetProfilerTimelineEnabled( !ProfilerTab::TimelineEnabled( m_profilerTab ) );
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

    if ( leftNow && m_activeSlider != 0 )
    {
        // Why: sliders update previews continuously while dragged. Heavy operations
        // such as rebuilding generated bodies are delayed until mouse release,
        // but cheap scalar controls are emitted every frame for immediate feedback.
        if ( !SceneTab::UpdateActiveSlider( m_sceneTab, m_activeSlider, m_mouseX, result ) &&
             !ProfilerTab::UpdateActiveSlider( m_profilerTab, m_activeSlider, m_mouseX, m_lastMaxWorkerThreadCount,
                                               result ) &&
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

    if ( leftNow && m_interaction.isDragging )
    {
        const int oldX = m_window.x;
        const int oldY = m_window.y;
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX, margin,
                                 (std::max)( margin, screenW - m_window.width - margin ) );

        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY, margin,
                                 (std::max)( margin, screenH - m_window.height - margin ) );

        if ( oldX != m_window.x || oldY != m_window.y )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }

    if ( leftNow && m_interaction.isResizing )
    {
        const int oldW = m_window.width;
        const int oldH = m_window.height;
        m_window.width = std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, minW, maxW );

        m_window.height = std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, minH, maxH );

        m_scrollbarVisibleUntil = now + 1.4;

        if ( oldW != m_window.width || oldH != m_window.height )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }

    if ( input.leftReleased )
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

    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_interaction.blocksCameraMouse = inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0;

    return result;
}
