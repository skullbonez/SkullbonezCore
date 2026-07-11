/*
File: SkullbonezSource/UI/UI.cpp
Purpose:
  Implements SkullbonezUI widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  UI.cpp implements SkullbonezUI widgets, layout, drawing, or UI state for the
  in-engine controls. As an implementation unit, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UI.h"
#include "UIFrameComposition.h"
#include "../Runtime/InputRouter.h"
#include "../Assets/AssetSystem.h"
#include "../Rendering/IRenderCommandContext.h"
#include "../Rendering/IRenderDiagnostics.h"
#include "../Rendering/IRenderResourceFactory.h"
#include "../Maths/Matrix4.h"
#include "../Runtime/Debug/PhysicsDebugVisualizer.h"
#include "../Core/Profiler.h"
#include "../Rendering/Text.h"
#include "UIDraw.h"
#include "UIDrawList.h"
#include "UIDrawWidgets.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabMemory.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UIStyle.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::FrameComposition;


bool InGameUI::IsVisible() const
{
    return m_window.isVisible;
}


bool InGameUI::IsMinimized() const
{
    return m_window.isMinimized;
}


void InGameUI::SetVisible( bool visible, double now )
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


void InGameUI::ToggleVisible( double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }
    SetMinimized( !m_window.isMinimized, now );
}


void InGameUI::CancelEditorMiniPaletteInteraction()
{
    m_editorMiniPalettePressActive = false;
    m_editorMiniPaletteFlyoutOpen = false;
    m_editorMiniPalettePressedEntry = -1;
    m_editorMiniPalettePressedObjectType = -1;
    m_editorMiniPalettePressedTreePlacement = EDITOR_MINI_TREE_PLACEMENT_NONE;
    m_editorMiniPalettePressedHoldMode = EDITOR_MINI_HOLD_MODE_NONE;
    m_editorMiniPalettePressStart = 0.0;
}


void InGameUI::SetMinimized( bool minimized, double now )
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


void InGameUI::ToggleMaximizeMinimize( int screenW, int screenH, double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }

    if ( m_window.isMinimized )
    {
        SetMinimized( false, now );
        return;
    }

    SetMaximized( !m_window.isMaximized, screenW, screenH, now );
}


void InGameUI::SetActiveTab( InGameUITab tab )
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
    SoundTab::ResetPreviewState( m_soundTab );
    m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Content );
    m_cache.Reset();
}


InGameUITab InGameUI::GetActiveTab() const
{
    return m_activeTab;
}


void InGameUI::CancelInputCapture()
{
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    m_activeSlider = 0;
    SceneTab::ResetPreviewState( m_sceneTab );
    OptionsTab::ResetPreviewState( m_optionsTab );
    PhysicsTab::ResetPreviewState( m_physicsTab );
    ControlsTab::ResetPreviewState( m_controlsTab );
    SoundTab::ResetPreviewState( m_soundTab );
    m_editorTab.objectCombo.Close();
    m_renderTargetCombo.Close();
    m_cameraModeCombo.Close();
    CancelEditorMiniPaletteInteraction();
    ProfilerTab::CancelPerformanceHistogramInteraction( m_profilerTab );
}


bool InGameUI::BlocksCameraMouse() const
{
    return m_interaction.blocksCameraMouse;
}


bool InGameUI::BlocksKeyboard() const
{
    return m_window.isVisible && !m_window.isMinimized &&
           ( m_sceneCombo.IsOpen() || CinematicTab::IsComboOpen( m_cinematicTab ) || m_editorTab.objectCombo.IsOpen() ||
             m_renderTargetCombo.IsOpen() );
}


bool InGameUI::WantsNativeMouseCursor() const
{
    return ( m_window.isVisible && !m_window.isMinimized ) || m_interaction.blocksCameraMouse ||
           ProfilerTab::PerformanceHistogramIsInteracting( m_profilerTab );
}


void InGameUI::SetWindowBounds( int x, int y, int width, int height )
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


void InGameUI::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Toggle );
        m_cache.Reset();
    }
}


void InGameUI::SetRendererComboOpen( bool open )
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


void InGameUI::SetWaterComboOpen( bool open )
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


void InGameUI::SetSceneComboOpen( bool open )
{
    m_sceneCombo.SetOpen( open );
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


void InGameUI::SetSceneFilter( const char* filter )
{
    SceneTab::SetFilter( m_sceneTab, filter );
}


void InGameUI::SetProfilerExpandAll( bool expandAll )
{
    ProfilerTab::SetExpandAll( m_profilerTab, expandAll );
    m_cache.Reset();
}


void InGameUI::SetProfilerTimelineEnabled( bool enabled )
{
    ProfilerTab::SetTimelineEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


void InGameUI::SetPerformanceHistogramEnabled( bool enabled )
{
    ProfilerTab::SetPerformanceHistogramEnabled( m_profilerTab, enabled );
    m_cache.Reset();
}


bool InGameUI::IsPerformanceHistogramEnabled() const
{
    return ProfilerTab::PerformanceHistogramEnabled( m_profilerTab );
}


void InGameUI::TogglePerformanceHistogramEnabled()
{
    SetPerformanceHistogramEnabled( !IsPerformanceHistogramEnabled() );
}


void InGameUI::SetMemoryOverlayEnabled( bool enabled )
{
    MemoryTab::SetOverlayEnabled( m_memoryOverlay, enabled );
    m_cache.Reset();
}


bool InGameUI::IsMemoryOverlayEnabled() const
{
    return MemoryTab::OverlayEnabled( m_memoryOverlay );
}


void InGameUI::ToggleMemoryOverlayEnabled()
{
    SetMemoryOverlayEnabled( !IsMemoryOverlayEnabled() );
}


bool InGameUI::NeedsUiTextPass() const
{
    return m_window.isVisible || IsPerformanceHistogramEnabled() || IsMemoryOverlayEnabled();
}


void InGameUI::SetHitboxOverlayEnabled( bool enabled )
{
    if ( m_hitboxOverlayEnabled != enabled )
    {
        m_hitboxOverlayEnabled = enabled;
        m_cache.Reset();
    }
}


void InGameUI::SetScrollY( float scrollY )
{
    m_scrollY = (std::max)( 0.0f, scrollY );
    m_scrollbarVisibleUntil = 1.2;
    m_cache.Reset();
}


void InGameUI::SetMouseOverride( bool enabled, int x, int y )
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


void InGameUI::SetMaximized( bool maximized, int screenW, int screenH, double now )
{
    if ( Chrome::SetMaximized( m_window, maximized, screenW, screenH, now ) )
    {
        m_scrollbarVisibleUntil = 0.0;
        m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
    }
}


void InGameUI::ResetResources( IRenderResourceFactory* resources )
{
    m_backdropBlur.ResetResources();
    ResetRenderTargetPreviewResources( m_renderTargetPreviewShader, m_renderTargetPreviewVB, resources );
    m_cache.Reset();
}


void InGameUI::DrawHitboxOverlay( const UIDrawContext& draw,
                                  const InGameUIFrameData& data,
                                  const UIRect& windowBounds,
                                  const UIRect& contentBounds,
                                  const UIRect& footerBounds ) const
{
    if ( !m_hitboxOverlayEnabled )
    {
        return;
    }

    constexpr float chromeR = 0.16f;
    constexpr float chromeG = 0.86f;
    constexpr float chromeB = 1.00f;
    constexpr float contentR = 0.30f;
    constexpr float contentG = 1.00f;
    constexpr float contentB = 0.42f;
    constexpr float footerR = 1.00f;
    constexpr float footerG = 0.22f;
    constexpr float footerB = 0.82f;
    constexpr float buttonR = 1.00f;
    constexpr float buttonG = 0.62f;
    constexpr float buttonB = 0.18f;

    DrawHitboxRect( draw, windowBounds, chromeR, chromeG, chromeB, 0.018f, 0.44f );

    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    DrawHitboxRect( draw, titleButtons.minimize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.maximize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.close, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    if ( !m_window.isMaximized )
    {
        DrawHitboxRect(
            draw,
            { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f },
            chromeR,
            chromeG,
            chromeB,
            0.050f,
            0.86f );
    }

    DrawTabHitboxes( draw, m_tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    switch ( m_activeTab )
    {
    case InGameUITab::Scene:
        DrawComboHitboxes( draw,
                           m_sceneCombo,
                           SceneDropdownHitboxOptionCount( m_sceneTab, data ),
                           contentR,
                           contentG,
                           contentB );
        DrawHitboxRect( draw, m_resetSceneButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_resetDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_saveDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_sceneTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Editor:
        DrawHitboxRect( draw, m_editorTab.editorModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_editorTab.placementModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_editorTab.staticObjectToggle.Bounds(), contentR, contentG, contentB );
        DrawComboHitboxes( draw, m_editorTab.objectCombo, EditorTab::OBJECT_TYPE_COUNT, contentR, contentG, contentB );
        break;
    case InGameUITab::Physics:
        for ( int i = 0; i < 13; ++i )
        {
            DrawHitboxRect( draw, m_physicsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }
        DrawHitboxRect( draw, m_physicsTab.pipelinePrevButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.pipelineNextButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.alphaSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.contactLingerSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.rayImpulseSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.launcherProjectileSpeedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.worldGravitySlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.terrainFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.objectFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.rollingFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoRadiusSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoInwardSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoSwirlSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoLiftSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Sound:
        SoundTab::DrawHitboxes( m_soundTab, draw, data, contentR, contentG, contentB );
        break;
    case InGameUITab::Options:
        for ( int i = 0; i < 6; ++i )
        {
            DrawHitboxRect( draw, m_optionsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }
        DrawHitboxRect( draw, m_optionsTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_optionsTab.modelCountSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Render:
        DrawHitboxRect( draw, m_renderShadowToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_saveRenderDefaultsButton.Bounds(), contentR, contentG, contentB );
        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            DrawHitboxRect( draw, m_renderSliders[i].Bounds(), contentR, contentG, contentB );
        }
        break;
    case InGameUITab::Targets:
        DrawComboHitboxes( draw, m_renderTargetCombo, m_lastRenderTargetPreviewCount, contentR, contentG, contentB );
        break;
    case InGameUITab::Keys:
        DrawHitboxRect( draw, m_controlsTab.seedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBallSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBoxSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidDensitySlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Sky:
        SkyTab::DrawHitboxes( m_skyTab, draw, contentR, contentG, contentB );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::DrawHitboxes( m_cinematicTab, draw, data, contentR, contentG, contentB );
        break;
    case InGameUITab::Profiler:
        DrawHitboxRect( draw, m_profilerTab.workerToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_profilerTab.workerThreadSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Memory:
        break;
    default:
        break;
    }

    if ( ContentHeight() > static_cast<int>( contentBounds.h ) )
    {
        DrawHitboxRect( draw, m_scrollBar.Bounds(), 0.18f, 0.82f, 0.95f, 0.060f, 0.86f );
    }

    DrawHitboxRect( draw, footerBounds, footerR, footerG, footerB, 0.020f, 0.54f );
    DrawComboHitboxes( draw, m_rendererCombo, 1, footerR, footerG, footerB );
    DrawComboHitboxes( draw, m_reflectionCombo, 3, footerR, footerG, footerB );
    DrawHitboxRect( draw, m_blurToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_vsyncToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_histogramToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_timelineToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_hitboxToggle.Bounds(), footerR, footerG, footerB );
}


int InGameUI::ContentHeight() const
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
    case InGameUITab::Sound:
        return SoundTab::ContentHeight();
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


void InGameUI::CloseSceneCombo()
{
    SceneTab::CloseCombo( m_sceneTab, m_sceneCombo );
}


InGameUIInputResult InGameUI::UpdateInput( const Basics::DeviceInputFrame& deviceFrame,
                                           const Basics::RuntimeMouseEdges& mouse,
                                           int screenW,
                                           int screenH,
                                           double now,
                                           bool editorModeEnabled,
                                           bool editorPlacementMode,
                                           bool editorPlaceStatic,
                                           bool editorTerrainAlign,
                                           int editorObjectType,
                                           int cameraModeIndex,
                                           uint32_t cameraModeEnabledMask,
                                           const char* const* sceneOptions,
                                           int sceneOptionCount,
                                           int selectedSceneOption )
{
    PROFILE_SCOPED( "Frame/UI/Input" );
    InGameUIInputResult result;
    // Concept: UI input produces command intents and capture state. The run loop
    // owns applying scene, physics, renderer, and editor mutations.
    editorObjectType = std::clamp( editorObjectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );
    (void)editorObjectType;
    cameraModeIndex = std::clamp( cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
    cameraModeEnabledMask &= ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u;
    m_interaction.blocksCameraMouse = false;
    const InputControl::UIInputSnapshot input =
        InputControl::CaptureSnapshot( deviceFrame, mouse, m_hasMouseOverride, m_mouseOverrideX, m_mouseOverrideY );
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
    if ( ProfilerTab::HandlePerformanceHistogramInput( m_profilerTab,
                                                       result,
                                                       screenW,
                                                       screenH,
                                                       m_mouseX,
                                                       m_mouseY,
                                                       leftNow,
                                                       input.leftPressed,
                                                       input.leftReleased,
                                                       wheelDelta ) )
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
        m_cameraModeCombo.SetBounds( cameraModeComboBounds.x,
                                     cameraModeComboBounds.y,
                                     cameraModeComboBounds.w,
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

            const bool comboOptionHit =
                m_cameraModeCombo.IsOpen() &&
                m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT ) >= 0;
            const bool comboDropdownHit =
                m_cameraModeCombo.IsOpen() &&
                m_cameraModeCombo.DropdownBounds( CAMERA_MODE_OPTION_COUNT ).Contains( m_mouseX, m_mouseY );
            insideCameraModeCombo =
                m_cameraModeCombo.HitBox( m_mouseX, m_mouseY ) || comboOptionHit || comboDropdownHit;
            if ( input.leftPressed && m_cameraModeCombo.IsOpen() )
            {
                const int option = m_cameraModeCombo.HitOption( m_mouseX, m_mouseY, CAMERA_MODE_OPTION_COUNT );
                const bool optionDisabled =
                    option >= 0 && option < 32 && ( cameraModeDisabledMask & ( 1u << option ) ) != 0;
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

            editorMiniPalette = BuildEditorMiniPaletteLayout( screenW,
                                                              screenH,
                                                              minimized,
                                                              m_editorMiniPalettePressedEntry,
                                                              m_editorMiniPaletteFlyoutOpen );
            insideEditorMiniPalette = EditorMiniPaletteContains( editorMiniPalette, m_mouseX, m_mouseY );

            const auto SelectEditorMiniPaletteObject =
                [&]( int objectType, bool requestPlaceStatic = false, bool placeStatic = false ) -> void
            {
                result.commands.editor.requestedObjectType =
                    std::clamp( objectType, 0, EditorTab::OBJECT_TYPE_COUNT - 1 );
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
                        const int flyoutOption =
                            HitEditorMiniPaletteFlyoutOption( editorMiniPalette, m_mouseX, m_mouseY );
                        if ( flyoutOption >= 0 )
                        {
                            if ( m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_TREE_TYPES )
                            {
                                selectedObjectType =
                                    EditorMiniTreeObjectType( flyoutOption, m_editorMiniPalettePressedTreePlacement );
                            }
                            else if ( m_editorMiniPalettePressedHoldMode == EDITOR_MINI_HOLD_MODE_RAGDOLL_MODES )
                            {
                                selectedObjectType = EditorMiniRagdollObjectType( flyoutOption );
                            }
                        }
                    }
                    else if ( m_editorMiniPalettePressedEntry >= 0 &&
                              m_editorMiniPalettePressedEntry < editorMiniPalette.buttonCount &&
                              editorMiniPalette.buttons[m_editorMiniPalettePressedEntry].Contains( m_mouseX,
                                                                                                   m_mouseY ) )
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
    const UIRect inputHitBounds = { static_cast<float>( inputX ),
                                    static_cast<float>( inputY ),
                                    static_cast<float>( inputW ),
                                    static_cast<float>( inputH ) };
    const bool inside =
        m_mouseX >= inputX && m_mouseX <= inputX + inputW && m_mouseY >= inputY && m_mouseY <= inputY + inputH;
    const bool inTitle = inside && m_mouseY < inputY + titleH;
    const bool inTabs = inside && m_mouseY >= inputY + titleH && m_mouseY < inputY + titleH + tabH;
    const bool inResize =
        !m_window.isMaximized && inside && Chrome::IsResizeHotspot( inputHitBounds, m_mouseX, m_mouseY );
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

    m_tabBar.SetBounds( static_cast<float>( inputX + 14 ),
                        static_cast<float>( inputY + titleH ),
                        static_cast<float>( inputW - 28 ),
                        static_cast<float>( tabH ) );
    const float footerX = static_cast<float>( inputX );
    const float footerY = static_cast<float>( bottomY );
    const UIRect rendererComboBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterComboBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect perfBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererComboBounds.x,
                               rendererComboBounds.y,
                               rendererComboBounds.w,
                               rendererComboBounds.h );
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
        SceneTab::UpdateFilterTyping( m_sceneTab,
                                      m_sceneCombo,
                                      result,
                                      deviceFrame.keys,
                                      sceneOptions,
                                      sceneOptionCount );
    }

    bool wheelHandled = false;
    if ( wheelDelta != 0 && m_sceneCombo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab,
                                                   m_sceneCombo,
                                                   sceneOptions,
                                                   sceneOptionCount,
                                                   m_mouseX,
                                                   m_mouseY,
                                                   wheelDelta,
                                                   contentX,
                                                   rowBase,
                                                   contentW );
    }

    if ( wheelDelta != 0 && inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / static_cast<float>( WHEEL_DELTA ) * 42.0f;
        m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }

    if ( input.leftPressed )
    {
        if ( titleButtons.minimize.Contains( m_mouseX, m_mouseY ) || titleButtons.close.Contains( m_mouseX, m_mouseY ) )
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
        else if ( m_sceneCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Scene )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                SceneTab::HandleOpenComboClick( m_sceneTab,
                                                m_sceneCombo,
                                                m_resetSceneButton,
                                                m_resetDefaultsButton,
                                                m_saveDefaultsButton,
                                                result,
                                                sceneOptions,
                                                sceneOptionCount,
                                                m_mouseX,
                                                m_mouseY,
                                                contentX,
                                                rowBase,
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
        else if ( CinematicTab::IsComboOpen( m_cinematicTab ) )
        {
            CinematicTab::HandleOpenComboClick( m_cinematicTab,
                                                result,
                                                sceneOptions,
                                                sceneOptionCount,
                                                m_mouseX,
                                                m_mouseY );
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
                const bool optionDisabled =
                    option >= 0 && option < 32 && ( m_lastRenderTargetDisabledMask & ( 1u << option ) ) != 0;
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
            if ( ProfilerTab::HandleContentClick( m_profilerTab,
                                                  result,
                                                  m_activeSlider,
                                                  inputX + contentPad,
                                                  contentY,
                                                  contentW,
                                                  m_scrollY,
                                                  m_mouseX,
                                                  m_mouseY,
                                                  m_lastWorkerThreadCount,
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
            if ( MemoryTab::HandleContentClick( m_memoryOverlay,
                                                result,
                                                m_activeSlider,
                                                m_mouseX,
                                                m_mouseY,
                                                contentX,
                                                scrolledY,
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
        else if ( inContent && m_activeTab == InGameUITab::Scene )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const bool sceneClickHandled = SceneTab::HandleContentClick( m_sceneTab,
                                                                         m_sceneCombo,
                                                                         m_resetSceneButton,
                                                                         m_resetDefaultsButton,
                                                                         m_saveDefaultsButton,
                                                                         result,
                                                                         deviceFrame.keys,
                                                                         m_activeSlider,
                                                                         sceneOptions,
                                                                         sceneOptionCount,
                                                                         selectedSceneOption,
                                                                         m_mouseX,
                                                                         m_mouseY,
                                                                         contentX,
                                                                         rowBase,
                                                                         contentW );
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
            if ( PhysicsTab::HandleContentClick( m_physicsTab,
                                                 result,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_mouseY,
                                                 contentX,
                                                 rowBase,
                                                 contentW ) &&
                 m_activeSlider != 0 && m_activeSlider != previousActiveSlider )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            }
            m_rendererCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Sound )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float scrolledY = static_cast<float>( contentY ) - m_scrollY;
            const int previousActiveSlider = m_activeSlider;
            if ( SoundTab::HandleContentClick( m_soundTab,
                                               result,
                                               m_activeSlider,
                                               m_mouseX,
                                               m_mouseY,
                                               contentX,
                                               scrolledY,
                                               contentW ) &&
                 m_activeSlider != 0 && m_activeSlider != previousActiveSlider )
            {
                result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Acquire;
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
            CinematicTab::CloseCombo( m_cinematicTab );
            m_editorTab.objectCombo.Close();
            m_renderTargetCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Options )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            if ( OptionsTab::HandleContentClick( m_optionsTab,
                                                 result,
                                                 m_activeSlider,
                                                 m_mouseX,
                                                 m_mouseY,
                                                 contentX,
                                                 rowBase,
                                                 contentW,
                                                 m_lastModelCapacity ) )
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
                                                  scrolledY + UI_RENDER_FEATURE_START_Y,
                                                  UI_RENDER_SAVE_BUTTON_W,
                                                  24.0f );
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
                    m_renderSliders[i].SetBounds( contentX, RenderSliderY( i, rowBase ), contentW, 34.0f );
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
            const bool capturedSlider = SkyTab::HandleContentClick( m_skyTab,
                                                                    result,
                                                                    m_activeSlider,
                                                                    m_mouseX,
                                                                    m_mouseY,
                                                                    contentX,
                                                                    scrolledY,
                                                                    contentW );

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
            const bool capturedSlider = CinematicTab::HandleContentClick( m_cinematicTab,
                                                                          result,
                                                                          m_activeSlider,
                                                                          m_mouseX,
                                                                          m_mouseY,
                                                                          contentX,
                                                                          scrolledY,
                                                                          contentW );

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
            if ( ControlsTab::HandleContentClick( m_controlsTab,
                                                  result,
                                                  m_activeSlider,
                                                  m_mouseX,
                                                  m_mouseY,
                                                  contentX,
                                                  rowBase,
                                                  contentW,
                                                  m_lastModelCapacity,
                                                  m_lastSolverBallCount,
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
        // Sliders update previews continuously while dragged.  Heavy operations
        // such as rebuilding generated bodies are delayed until mouse release,
        // but cheap scalar controls are emitted every frame for immediate feedback.
        if ( !SceneTab::UpdateActiveSlider( m_sceneTab, m_activeSlider, m_mouseX, result ) &&
             !ProfilerTab::UpdateActiveSlider( m_profilerTab,
                                               m_activeSlider,
                                               m_mouseX,
                                               m_lastMaxWorkerThreadCount,
                                               result ) &&
             !MemoryTab::UpdateActiveSlider( m_memoryOverlay, m_activeSlider, m_mouseX, result ) &&
             !OptionsTab::UpdateActiveSlider( m_optionsTab, m_activeSlider, m_mouseX, m_lastModelCapacity, result ) &&
             !PhysicsTab::UpdateActiveSlider( m_physicsTab, m_activeSlider, m_mouseX, result ) &&
             !SoundTab::UpdateActiveSlider( m_soundTab, m_activeSlider, m_mouseX, result ) )
        {
            const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );
            if ( renderSlider >= 0 )
            {
                SetRenderSliderResult( result,
                                       m_renderSliders[renderSlider],
                                       m_mouseX,
                                       kRenderSliderSpecs[renderSlider] );
            }
            else
            {
                if ( !SkyTab::UpdateActiveSlider( m_skyTab, m_activeSlider, m_mouseX, result ) &&
                     !CinematicTab::UpdateActiveSlider( m_cinematicTab, m_activeSlider, m_mouseX, result ) )
                {
                    ControlsTab::UpdateActiveSlider( m_controlsTab,
                                                     m_activeSlider,
                                                     m_mouseX,
                                                     m_lastModelCapacity,
                                                     m_lastSolverBallCount,
                                                     m_lastSolverBoxCount,
                                                     result );
                }
            }
        }
    }

    if ( leftNow && m_interaction.isDragging )
    {
        const int oldX = m_window.x;
        const int oldY = m_window.y;
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX,
                                 margin,
                                 (std::max)( margin, screenW - m_window.width - margin ) );
        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY,
                                 margin,
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
        m_window.width =
            std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, minW, maxW );
        m_window.height =
            std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, minH, maxH );
        m_scrollbarVisibleUntil = now + 1.4;
        if ( oldW != m_window.width || oldH != m_window.height )
        {
            m_backdropBlur.Invalidate( UIBackdropBlurInvalidationReason::Bounds );
        }
    }

    if ( input.leftReleased )
    {
        // Commit deferred slider previews exactly once on release.  This avoids
        // rebuilding solver objects or generated model pools every mouse-move
        // while still letting the drawn slider thumb track the user's drag.
        if ( !SceneTab::CommitActiveSlider( m_sceneTab, m_activeSlider, result ) &&
             !ProfilerTab::CommitActiveSlider( m_profilerTab, m_activeSlider, result ) &&
             !MemoryTab::CommitActiveSlider( m_memoryOverlay, m_activeSlider, result ) &&
             !OptionsTab::CommitActiveSlider( m_optionsTab, m_activeSlider, result ) &&
             !PhysicsTab::CommitActiveSlider( m_physicsTab, m_activeSlider, result ) &&
             !SoundTab::CommitActiveSlider( m_soundTab, m_activeSlider, result ) )
        {
            const int renderSlider = RenderSliderIndexFromActiveSlider( m_activeSlider );
            if ( renderSlider >= 0 )
            {
                SetRenderSliderResult( result,
                                       m_renderSliders[renderSlider],
                                       m_mouseX,
                                       kRenderSliderSpecs[renderSlider] );
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
        SoundTab::ResetPreviewState( m_soundTab );
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        result.nativeMouseCapture = InGameUIInputResult::NativeMouseCaptureRequest::Release;
    }

    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_interaction.blocksCameraMouse =
        inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0;
    return result;
}


void InGameUI::Draw( const InGameUIFrameData& data, const UIRenderContext& render )
{
    const bool histogramEnabled = ProfilerTab::PerformanceHistogramEnabled( m_profilerTab );
    const bool memoryOverlayEnabled = MemoryTab::OverlayEnabled( m_memoryOverlay );
    // Why: input handling runs before the next draw, so the profiler tab keeps a
    // bounded copy of the latest frame snapshot for content height and hit tests.
    ProfilerTab::SetFrameSnapshot( m_profilerTab, data.profiler );
    if ( !m_window.isVisible && !histogramEnabled && !memoryOverlayEnabled )
    {
        return;
    }
    assert( render.IsReady() );
    IRenderCommandContext& renderCommands = *render.commands;
    IRenderDiagnostics& renderDiagnostics = *render.diagnostics;
    DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/UI/Draw" );

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastModelCapacity = std::clamp( data.modelCapacity, 1, MAX_GAME_MODELS );
    m_lastSolverBallCount = std::clamp( data.solverBallCount, UI_SOLVER_COUNT_MIN, m_lastModelCapacity );
    m_lastSolverBoxCount = std::clamp( data.solverBoxCount, UI_SOLVER_COUNT_MIN, m_lastModelCapacity );
    m_lastMaxWorkerThreadCount = (std::max)( 1, data.maxWorkerThreadCount );
    m_lastWorkerThreadCount = std::clamp( data.workerThreadCount, 0, m_lastMaxWorkerThreadCount );
    m_lastRenderTargetPreviewCount = RenderTargetPreviewCount( data );
    m_lastRenderTargetDisabledMask = RenderTargetPreviewDisabledMask( data );
    m_selectedRenderTargetPreview = ResolveRenderTargetPreviewSelection( data, m_selectedRenderTargetPreview );

    auto drawHistogramOverlay = [&]()
    {
        if ( !histogramEnabled )
        {
            return;
        }

        // Why: the main diagnostics window can replay cached draw commands, but
        // marker samples, selector state, and drag/resize feedback must rebuild
        // every frame.
        m_histogramDrawList.Clear();
        const UIDrawContext histogramDraw( screenW, screenH, &m_histogramDrawList );
        ProfilerTab::DrawPerformanceHistogram( m_profilerTab, histogramDraw, data );
        FlushUIDrawList( m_histogramDrawList, renderCommands, renderDiagnostics, screenW, screenH );
    };

    auto drawMemoryOverlay = [&]()
    {
        if ( !memoryOverlayEnabled )
        {
            return;
        }

        // Why: allocator events are retained by memory level, not by sample
        // index. Draw it after the F5 panel so the F6 panel can anchor under
        // the CPU histogram's current position when both are visible.
        m_memoryOverlayDrawList.Clear();
        const UIDrawContext memoryDraw( screenW, screenH, &m_memoryOverlayDrawList );
        const float memoryX = histogramEnabled ? m_profilerTab.histogramPanelX : 16.0f;
        const float memoryY =
            histogramEnabled ? m_profilerTab.histogramPanelY + m_profilerTab.histogramPanelH + 8.0f : 16.0f;
        MemoryTab::DrawOverlay( m_memoryOverlay, memoryDraw, data, memoryX, memoryY );
        FlushUIDrawList( m_memoryOverlayDrawList, renderCommands, renderDiagnostics, screenW, screenH );
    };

    auto drawStandaloneOverlays = [&]()
    {
        drawHistogramOverlay();
        drawMemoryOverlay();
    };

    if ( histogramEnabled )
    {
        ProfilerTab::PushPerformanceHistogramSample( m_profilerTab, data );
    }
    if ( memoryOverlayEnabled )
    {
        MemoryTab::PushOverlayFrame( m_memoryOverlay, data );
    }

    if ( !m_window.isVisible )
    {
        drawStandaloneOverlays();
        return;
    }

    if ( m_window.isMinimized )
    {
        m_cache.Reset();
        UIDrawList& drawList = m_cache.MutableDrawList();
        drawList.Clear();
        const UIDrawContext draw( screenW, screenH, &drawList );
        if ( m_window.animationActive && m_window.animationToMinimized )
        {
            const UIRect animBounds = Chrome::CurrentWindowRect( m_window, data.now );
            if ( m_window.animationActive )
            {
                Chrome::DrawWindowAnimationShell( draw, animBounds );
                FlushUIDrawList( drawList, renderCommands, renderDiagnostics, screenW, screenH );
                drawStandaloneOverlays();
                return;
            }
        }

        char titleText[192] = {};
        Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
        if ( !data.editorModeEnabled )
        {
            StripMinimizedRuntimeModeSuffix( data, titleText, sizeof( titleText ) );
        }
        m_window.minimizedWidth =
            data.editorModeEnabled
                ? EditorMinimizedWidth( data, screenW )
                : (std::min)( MinimizedWidthWithCameraModeCombo( titleText, screenW ), MINIMIZED_RUN_MAX_W );
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        if ( data.editorModeEnabled )
        {
            const EditorMiniPaletteLayout editorMiniPalette =
                BuildEditorMiniPaletteLayout( screenW,
                                              screenH,
                                              minimized,
                                              m_editorMiniPalettePressedEntry,
                                              m_editorMiniPaletteFlyoutOpen );
            DrawEditorMiniPalette( draw,
                                   editorMiniPalette,
                                   data.editorObjectType,
                                   data.editorPlaceStatic,
                                   m_mouseX,
                                   m_mouseY,
                                   m_editorMiniPalettePressedTreePlacement,
                                   m_editorMiniPalettePressedHoldMode,
                                   m_editorMiniPalettePressedEntry,
                                   screenW,
                                   screenH );
            DrawEditorMinimizedWindow( draw, minimized, data, m_mouseX, m_mouseY );
        }
        else
        {
            const UIRect cameraModeComboBounds = MinimizedCameraModeComboBounds( minimized );
            m_cameraModeCombo.SetLabelVisible( false );
            m_cameraModeCombo.SetBounds( cameraModeComboBounds.x,
                                         cameraModeComboBounds.y,
                                         cameraModeComboBounds.w,
                                         cameraModeComboBounds.h );
            m_cameraModeCombo.SetDropUp( true );
            const float titleMaxW =
                (std::max)( 40.0f, cameraModeComboBounds.x - ( minimized.x + 32.0f ) - MINIMIZED_CAMERA_MODE_GAP );
            Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, titleMaxW );
            Chrome::DrawMinimizedWindow( draw, minimized, titleText );
            const int cameraModeIndex = std::clamp( data.cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
            const uint32_t cameraModeDisabledMask =
                ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) &
                ~( data.cameraModeEnabledMask & ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) );
            m_cameraModeCombo.Draw( draw,
                                    "",
                                    kCameraModeOptions,
                                    CAMERA_MODE_OPTION_COUNT,
                                    cameraModeIndex,
                                    m_mouseX,
                                    m_mouseY,
                                    cameraModeDisabledMask );
        }
        DrawEditorObjectCounter( draw, data, screenW, screenH );
        FlushUIDrawList( drawList, renderCommands, renderDiagnostics, screenW, screenH );
        drawStandaloneOverlays();
        return;
    }

    PROFILE_BEGIN( "Frame/UI/Layout" );
    const UIRect windowBounds = Chrome::CurrentWindowRect( m_window, data.now );
    const float x = windowBounds.x;
    const float y = windowBounds.y;
    const float w = windowBounds.w;
    const float h = windowBounds.h;
    const float titleH = 44.0f;
    const float tabH = 44.0f;
    const float bottomH = 78.0f;
    const float pad = 18.0f;
    const float contentX = x + pad;
    const float contentY = y + titleH + tabH + 12.0f;
    const float contentW = w - pad * 2.0f - 8.0f;
    const float contentH = (std::max)( 30.0f, h - titleH - tabH - bottomH - pad );
    const float scrolledY = contentY - m_scrollY;
    char titleText[192] = {};
    Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};
    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;
    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.fps );
        titleStatW = Text2d::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }
    Chrome::FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );
    ProfilerTab::ApplyExpandAll( m_profilerTab );

    UICacheFrameKey cacheKey;
    cacheKey.screenW = screenW;
    cacheKey.screenH = screenH;
    cacheKey.windowBounds = windowBounds;
    cacheKey.activeTab = static_cast<int>( m_activeTab );
    cacheKey.scrollY = m_scrollY;
    cacheKey.blurEnabled = m_blurPreviewEnabled;
    cacheKey.contentSignature = BuildUIContentSignature( data );
    cacheKey.styleSignature = HashBool( HashBool( 2166136261u, m_blurPreviewEnabled ), m_hitboxOverlayEnabled );
    cacheKey.interactionSignature = BuildUIInteractionSignature( m_mouseX,
                                                                 m_mouseY,
                                                                 m_rendererCombo.IsOpen(),
                                                                 m_reflectionCombo.IsOpen(),
                                                                 m_sceneCombo.IsOpen(),
                                                                 CinematicTab::IsComboOpen( m_cinematicTab ),
                                                                 m_editorTab.objectCombo.IsOpen(),
                                                                 m_renderTargetCombo.IsOpen(),
                                                                 m_cameraModeCombo.IsOpen(),
                                                                 m_selectedRenderTargetPreview,
                                                                 m_activeSlider );
    // Why: Most UI frames only move the window/scroll offset. Replaying cached
    // draw commands keeps draw-call churn low while live render-target previews
    // still rebuild every frame.
    m_cache.BeginFrame( cacheKey );
    PROFILE_END( "Frame/UI/Layout" );

    const bool drawsLiveRenderTargetPreview = m_activeTab == InGameUITab::Targets;
    if ( !drawsLiveRenderTargetPreview && m_cache.CanReplayPositionOnly( cacheKey ) )
    {
        const float replayOffsetX = m_cache.ReplayOffsetX( cacheKey );
        const float replayOffsetY = m_cache.ReplayOffsetY( cacheKey );
        FlushUIDrawList( m_cache.DrawList(),
                         renderCommands,
                         renderDiagnostics,
                         screenW,
                         screenH,
                         replayOffsetX,
                         replayOffsetY );
        drawStandaloneOverlays();
        m_cache.StoreFrame( cacheKey );
        return;
    }

    UIDrawList& drawList = m_cache.MutableDrawList();
    drawList.Clear();
    const UIDrawContext draw( screenW, screenH, &drawList );
    PROFILE_BEGIN( "Frame/UI/DrawBuild" );

    const UIRect blurBounds = { x, y, w, h };
    Text2d::FlushQuads( renderCommands );
    PROFILE_BEGIN( "Frame/UI/Blur" );
    m_backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.currentFrame, data.now, m_blurPreviewEnabled );
    PROFILE_END( "Frame/UI/Blur" );

    Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, m_blurPreviewEnabled, titleText );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    Chrome::DrawTitleButtons( draw, titleButtons, m_window.isMaximized, m_mouseX, m_mouseY );
    const UIRect objectCounterAvoidBounds = TitleButtonGroupBounds( titleButtons );
    DrawEditorObjectCounter( draw, data, screenW, screenH, &objectCounterAvoidBounds );

    static const char* kTabs[] =
        { "Prof", "Scene", "Edit", "Phys", "Sound", "Opt", "Render", "Targets", "Ctrl", "Sky", "Cine", "Mem" };
    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f },
                       Style::Radii().window,
                       palette.windowSubtle,
                       palette.innerBorder );

    if ( m_activeTab == InGameUITab::Profiler )
    {
        ProfilerTab::Draw( m_profilerTab,
                           draw,
                           data,
                           contentX,
                           contentY,
                           contentW,
                           contentH,
                           m_scrollY,
                           m_activeSlider );
    }
    else if ( m_activeTab == InGameUITab::Memory )
    {
        MemoryTab::Draw( draw,
                         m_memoryOverlay,
                         data,
                         contentX,
                         contentY,
                         contentW,
                         contentH,
                         scrolledY,
                         m_activeSlider,
                         m_mouseX,
                         m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::Draw( m_sceneTab,
                        m_sceneCombo,
                        m_resetSceneButton,
                        m_resetDefaultsButton,
                        m_saveDefaultsButton,
                        draw,
                        data,
                        contentX,
                        contentY,
                        contentW,
                        contentH,
                        scrolledY,
                        m_mouseX,
                        m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Physics )
    {
        PhysicsTab::Draw( m_physicsTab,
                          draw,
                          data,
                          contentX,
                          contentY,
                          contentW,
                          contentH,
                          scrolledY,
                          m_activeSlider,
                          m_mouseX,
                          m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Sound )
    {
        SoundTab::Draw( m_soundTab,
                        draw,
                        data,
                        contentX,
                        contentY,
                        contentW,
                        contentH,
                        scrolledY,
                        m_activeSlider,
                        m_mouseX,
                        m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Editor )
    {
        EditorTab::Draw( m_editorTab,
                         draw,
                         data,
                         contentX,
                         contentY,
                         contentW,
                         contentH,
                         scrolledY,
                         m_mouseX,
                         m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Options )
    {
        OptionsTab::Draw( m_optionsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, m_activeSlider );
    }
    else if ( m_activeTab == InGameUITab::Render )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Render" );
        DrawContentToggle( draw,
                           contentY,
                           contentH,
                           m_renderShadowToggle,
                           contentX,
                           scrolledY + UI_RENDER_FEATURE_START_Y,
                           colW,
                           "Shadows",
                           data.ordinaryRender.shadowsEnabled );
        m_saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W,
                                              scrolledY + UI_RENDER_FEATURE_START_Y,
                                              UI_RENDER_SAVE_BUTTON_W,
                                              24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_RENDER_FEATURE_START_Y, 24.0f ) )
        {
            m_saveRenderDefaultsButton.Draw( draw, "Save CFG", m_mouseX, m_mouseY );
        }

        const float baseY = scrolledY + UI_RENDER_START_Y;
        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            const RenderSliderSpec& spec = kRenderSliderSpecs[i];
            const float sliderY = RenderSliderY( i, baseY );
            if ( spec.section && IsRowVisible( contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 18.0f ) )
            {
                DrawSectionTitle( draw,
                                  contentX,
                                  contentY,
                                  contentH,
                                  sliderY - UI_RENDER_SECTION_H + 4.0f,
                                  12.0f,
                                  spec.section );
            }
            const float value =
                std::clamp( RenderValueForParam( data.ordinaryRender, spec.param ), spec.minValue, spec.maxValue );

            snprintf( buf, sizeof( buf ), spec.valueFormat, value );
            m_renderSliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );
            if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
            {
                m_renderSliders[i].Draw( draw, spec.label, buf, value, spec.minValue, spec.maxValue );
            }
        }
    }
    else if ( m_activeTab == InGameUITab::Targets )
    {
        const int targetCount = RenderTargetPreviewCount( data );
        const int selectedIndex = m_selectedRenderTargetPreview;
        const bool hasSelection = selectedIndex >= 0 && selectedIndex < targetCount;
        const UIRenderTargetPreviewResource* selected =
            hasSelection ? &data.renderTargetPreviews[selectedIndex] : nullptr;
        const bool selectedAvailable = selected && selected->available && selected->textureHandle != 0 &&
                                       selected->width > 0 && selected->height > 0;
        const Style::UIPalette& targetPalette = Style::Palette();
        const char* options[UI_RENDER_TARGET_PREVIEW_MAX] = {};
        int liveCount = 0;
        for ( int i = 0; i < targetCount; ++i )
        {
            const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
            options[i] = resource.label;
            if ( resource.available && resource.textureHandle != 0 && resource.width > 0 && resource.height > 0 )
            {
                ++liveCount;
            }
        }

        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Targets" );

        char countText[64];
        snprintf( countText, sizeof( countText ), "%d / %d live", liveCount, targetCount );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_META_Y - 24.0f, 18.0f ) )
        {
            DrawLabelValueAt( draw,
                              contentY,
                              contentH,
                              contentX,
                              scrolledY + UI_TARGETS_META_Y - 24.0f,
                              "Resources",
                              countText,
                              targetPalette.accent.r,
                              targetPalette.accent.g,
                              targetPalette.accent.b );
        }

        if ( selected )
        {
            char detailText[160];
            if ( selectedAvailable )
            {
                snprintf( detailText,
                          sizeof( detailText ),
                          "%s, %d x %d, #%u",
                          RenderTargetPreviewTypeText( *selected ),
                          selected->width,
                          selected->height,
                          selected->textureHandle );
            }
            else
            {
                snprintf( detailText, sizeof( detailText ), "%s, n/a", RenderTargetPreviewTypeText( *selected ) );
            }
            DrawLabelValueAt( draw,
                              contentY,
                              contentH,
                              contentX,
                              scrolledY + UI_TARGETS_META_Y,
                              "Selected",
                              detailText,
                              selectedAvailable ? targetPalette.textPrimary.r : targetPalette.textMuted.r,
                              selectedAvailable ? targetPalette.textPrimary.g : targetPalette.textMuted.g,
                              selectedAvailable ? targetPalette.textPrimary.b : targetPalette.textMuted.b );
        }

        const UIRect previewPanel = { contentX, scrolledY + UI_TARGETS_PREVIEW_Y, contentW, UI_TARGETS_PREVIEW_H };
        const UIRect previewClip = { contentX, contentY, contentW, contentH };
        UIRect previewImage = previewPanel;
        if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
        {
            draw.RoundedPanel( previewPanel,
                               Style::Radii().control,
                               targetPalette.windowSubtle,
                               targetPalette.innerBorder );
            const UIRect previewInset = { previewPanel.x + 10.0f,
                                          previewPanel.y + 10.0f,
                                          (std::max)( 1.0f, previewPanel.w - 20.0f ),
                                          (std::max)( 1.0f, previewPanel.h - 20.0f ) };
            previewImage = selected ? FitRectToAspect( previewInset, selected->width, selected->height ) : previewInset;
            draw.RoundedRect( previewImage.x - 1.0f,
                              previewImage.y - 1.0f,
                              previewImage.w + 2.0f,
                              previewImage.h + 2.0f,
                              Style::Radii().control,
                              0.01f,
                              0.015f,
                              0.018f,
                              0.92f );
        }

        if ( selectedAvailable && IsBlockVisible( contentY, contentH, previewImage.y, previewImage.h ) )
        {
            FlushUIDrawList( drawList, renderCommands, renderDiagnostics, screenW, screenH );
            drawList.Clear();
            DrawRenderTargetPreviewTexture( m_renderTargetPreviewShader,
                                            m_renderTargetPreviewVB,
                                            draw,
                                            *selected,
                                            previewImage,
                                            previewClip,
                                            render );
        }
        else if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_PREVIEW_Y + 116.0f, 18.0f ) )
        {
            draw.Text( previewPanel.x + 18.0f,
                       previewPanel.y + 116.0f,
                       12.0f,
                       targetPalette.textMuted.r,
                       targetPalette.textMuted.g,
                       targetPalette.textMuted.b,
                       "Not available this frame" );
        }

        if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
        {
            draw.Outline( previewImage.x,
                          previewImage.y,
                          previewImage.w,
                          previewImage.h,
                          targetPalette.border.r,
                          targetPalette.border.g,
                          targetPalette.border.b,
                          0.72f );
        }

        const char* selectedText = selected ? selected->label : "No targets";
        m_renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );
        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_COMBO_Y, 24.0f ) )
        {
            m_renderTargetCombo.Draw( draw,
                                      "View",
                                      selectedText,
                                      options,
                                      targetCount,
                                      selectedIndex,
                                      m_mouseX,
                                      m_mouseY,
                                      m_lastRenderTargetDisabledMask );
        }
    }
    else if ( m_activeTab == InGameUITab::Sky )
    {
        SkyTab::Draw( m_skyTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, m_mouseX, m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Cinematic )
    {
        CinematicTab::Draw( m_cinematicTab,
                            draw,
                            data,
                            contentX,
                            contentY,
                            contentW,
                            contentH,
                            scrolledY,
                            m_mouseX,
                            m_mouseY );
    }
    else
    {
        ControlsTab::Draw( m_controlsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY );
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar
        .Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 16.0f, by, w - 32.0f, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 462.0f : footerW;
    draw.RoundedPanel( { footerX, by + 16.0f, controlsW, 56.0f },
                       Style::Radii().control,
                       palette.windowSubtle,
                       palette.innerBorder );

    const UIRect rendererComboBounds = FooterRendererComboBounds( x, by );
    const UIRect waterComboBounds = FooterWaterComboBounds( x, by );
    const UIRect blurFooterBounds = FooterBlurBounds( x, by );
    const UIRect vsyncFooterBounds = FooterVsyncBounds( x, by );
    const UIRect hitboxFooterBounds = FooterHitboxBounds( x, by );
    const UIRect timelineFooterBounds = FooterTimelineBounds( x, by );
    const UIRect perfFooterBounds = FooterPerfBounds( x, by );
    m_rendererCombo.SetBounds( rendererComboBounds.x,
                               rendererComboBounds.y,
                               rendererComboBounds.w,
                               rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurFooterBounds.x, blurFooterBounds.y, blurFooterBounds.w, blurFooterBounds.h );
    m_vsyncToggle.SetBounds( vsyncFooterBounds.x, vsyncFooterBounds.y, vsyncFooterBounds.w, vsyncFooterBounds.h );
    m_hitboxToggle.SetBounds( hitboxFooterBounds.x, hitboxFooterBounds.y, hitboxFooterBounds.w, hitboxFooterBounds.h );
    m_histogramToggle.SetBounds( perfFooterBounds.x, perfFooterBounds.y, perfFooterBounds.w, perfFooterBounds.h );
    m_timelineToggle.SetBounds( timelineFooterBounds.x,
                                timelineFooterBounds.y,
                                timelineFooterBounds.w,
                                timelineFooterBounds.h );
    static const char* kRendererOptions[] = { "DX12" };
    static const char* kReflectionOptions[] = { "FBO", "DXR", "None" };
    m_rendererCombo.Draw( draw, "Renderer", kRendererOptions, 1, 0, m_mouseX, m_mouseY );
    DrawFooterToggle( draw, blurFooterBounds, "Blur", m_blurPreviewEnabled );
    DrawFooterToggle( draw, vsyncFooterBounds, "VSync", data.vsyncEnabled );
    DrawFooterToggle( draw, hitboxFooterBounds, "Hitboxes", m_hitboxOverlayEnabled );
    DrawFooterToggle( draw, perfFooterBounds, "Perf", ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
    DrawFooterToggle( draw, timelineFooterBounds, "Timeline", ProfilerTab::TimelineEnabled( m_profilerTab ) );
    m_reflectionCombo.Draw( draw,
                            "Water",
                            kReflectionOptions,
                            3,
                            WaterReflectionModeFromData( data ),
                            m_mouseX,
                            m_mouseY,
                            ReflectionDisabledMask() );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent =
        static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.RoundedPanel( { statsX, by + 16.0f, statsW, 56.0f },
                           Style::Radii().control,
                           palette.windowSubtle,
                           palette.innerBorder );

        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            DrawCompactFooterStat( draw,
                                   statsX,
                                   by + 23.0f,
                                   "FPS",
                                   fpsText,
                                   palette.accent.r,
                                   palette.accent.g,
                                   palette.accent.b );
            DrawCompactFooterStat( draw,
                                   statsX,
                                   by + 41.0f,
                                   "Frame",
                                   frameText,
                                   palette.textPrimary.r,
                                   palette.textPrimary.g,
                                   palette.textPrimary.b );
            DrawCompactFooterStat( draw,
                                   statsX,
                                   by + 59.0f,
                                   "Draw/UI",
                                   drawText,
                                   palette.textPrimary.r,
                                   palette.textPrimary.g,
                                   palette.textPrimary.b );
        }
        else
        {
            DrawFooterStatCell( draw,
                                statsX + 18.0f,
                                by,
                                "FPS",
                                status,
                                palette.accent.r,
                                palette.accent.g,
                                palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 78.0f, by );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            DrawFooterStatCell( draw,
                                statsX + 100.0f,
                                by,
                                "Frame Time",
                                status,
                                palette.textPrimary.r,
                                palette.textPrimary.g,
                                palette.textPrimary.b );
            DrawFooterStatDivider( draw, statsX + 190.0f, by );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            DrawFooterStatCell( draw,
                                statsX + 212.0f,
                                by,
                                "CPU",
                                status,
                                palette.accent.r,
                                palette.accent.g,
                                palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 266.0f, by );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            DrawFooterStatCell( draw,
                                statsX + 288.0f,
                                by,
                                "GPU",
                                status,
                                palette.accent.r,
                                palette.accent.g,
                                palette.accent.b );
            DrawFooterStatDivider( draw, statsX + 342.0f, by );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            DrawFooterStatCell( draw,
                                statsX + statsW - 112.0f,
                                by,
                                "Draws / UI",
                                status,
                                palette.textPrimary.r,
                                palette.textPrimary.g,
                                palette.textPrimary.b );
        }
    }
    else
    {
        if ( titleStatW > 0.0f && titleStatX + titleStatW < x + w - 116.0f )
        {
            draw.Text( titleStatX, y + 17.0f, 10.5f, palette.accent.r, palette.accent.g, palette.accent.b, titleStat );
        }
    }

    draw.Rect( x + w - 24.0f,
               y + h - 9.0f,
               14.0f,
               2.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.58f );
    draw.Rect( x + w - 18.0f,
               y + h - 15.0f,
               8.0f,
               2.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.46f );
    draw.Rect( x + w - 12.0f,
               y + h - 21.0f,
               2.0f,
               2.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.38f );

    DrawHitboxOverlay( draw,
                       data,
                       windowBounds,
                       { contentX, contentY, contentW, contentH },
                       { footerX, by + 16.0f, controlsW, 56.0f } );

    PROFILE_END( "Frame/UI/DrawBuild" );
    FlushUIDrawList( drawList, renderCommands, renderDiagnostics, screenW, screenH );
    drawStandaloneOverlays();
    if ( drawsLiveRenderTargetPreview )
    {
        m_cache.Reset();
    }
    else
    {
        m_cache.StoreFrame( cacheKey );
    }
}
