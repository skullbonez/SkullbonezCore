/*
File: SkullbonezSource/Runtime/Render/UiTextPass.cpp
Purpose:
  Implements the cohesive UI/Text render pass owner.

Summary:
  World rendering can be skipped, redirected, or post-processed, but UI/text is
  a late pass over the final window. This owner holds font/text-batch lifetime,
  profiler/timing and ray-tracing presentation capabilities, text-only output,
  HUD overlays, and the in-game UI frame publication. RuntimeRenderer schedules
  focused metrics, chrome, operator projection/submission, Replay, and overlay
  operations.

Glossary:
  Runtime mode badge: Compact top-right label that names the current camera/input
    workspace, such as Inspect or Manipulator.
  Scene pause badge: Compact top-right scene-flow indicator for frame progress,
    completion, and the cross-scene pause lock.
  Text-only mode: Validation mode that skips world rendering and renders glyphs
    on a solid background to isolate text output.
  UI frame data: Borrowed per-frame snapshot passed to the immediate-mode UI.

Invariants:
  - Font resources are created once through EnsureGpuResources and released
    before backend teardown.
  - Every scheduled UI path flushes Text2d before the graph completes, so later
    frame work cannot inherit queued UI glyphs.
  - Development-tool status is copied into the UI snapshot; draw code never
    reaches back into the live Tracy owner.
  - Operation-specific graph ABI records borrow direct values only until their
    synchronous callback returns; no owner reference is retained between calls.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeRenderPasses.h"
#include "RuntimeRenderFrameValues.h"
#include "../Input/InputController.h"
#include "../Camera/CameraControlState.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../RuntimeFrameViews.h"
#include "../UI/RuntimeViewModel.h"
#include "../App/RunTimerState.h"
#include "../Scene/SceneControllerState.h"
#include "../Scene/SceneSessionState.h"
#include "../Scene/SceneWorld.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/TracyClientOwner.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/DiagnosticsPhysicsUI.h"
#include "../Planning/ReplayOverlayRenderer.h"
#include "../../Core/WorkerPool.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../../Core/Profiler.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/Text.h"
#include "../../UI/UI.h"
#include "../../UI/UIDraw.h"
#include "../../UI/UIDrawList.h"
#include "../../UI/UIFrameComposition.h"
#include "../../UI/UIFontMetrics.h"
#include "../../UI/UIProfilerOverlayPresenter.h"
#include "../UI/RenderDiagnosticsProjection.h"
#include "../../UI/UIStyle.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Text::Text2d;
using SkullbonezCore::UI::InGameUIFrameData;
using SkullbonezCore::UI::InGameUITab;

namespace
{
class RetainedUIDrawStatsScope
{
  public:
    RetainedUIDrawStatsScope( const SkullbonezCore::UI::UIDrawList& testPattern, const SkullbonezCore::UI::UIDrawList& badge,
                              const SkullbonezCore::UI::UIDrawList& replay, const SkullbonezCore::UI::UIDrawList& profiler )
        : m_testPattern( testPattern ), m_badge( badge ), m_replay( replay ), m_profiler( profiler )
    {
    }

    ~RetainedUIDrawStatsScope()
    {
        char drawStatsFlag[2] = {};
        size_t drawStatsFlagLength = 0;
        const bool drawStatsRequested = getenv_s( &drawStatsFlagLength, drawStatsFlag, sizeof( drawStatsFlag ),
                                                  "SKORE_UI_DRAW_STATS" ) == 0 &&
                                        drawStatsFlag[0] != '\0';

        if ( !drawStatsRequested )
        {
            return;
        }

        const SkullbonezCore::UI::UIDrawList::Stats testPatternStats = m_testPattern.GetStats();
        const SkullbonezCore::UI::UIDrawList::Stats badgeStats = m_badge.GetStats();
        const SkullbonezCore::UI::UIDrawList::Stats replayStats = m_replay.GetStats();
        const SkullbonezCore::UI::UIDrawList::Stats profilerStats = m_profiler.GetStats();
        const auto overflowed = []( const SkullbonezCore::UI::UIDrawList::Stats& stats )
        { return stats.commandOverflow || stats.textOverflow || stats.clipOverflow; };

        const bool overflow = overflowed( testPatternStats ) || overflowed( badgeStats ) || overflowed( replayStats ) ||
                              overflowed( profilerStats );

        std::fprintf( stderr,
                      "[ui-retained-draw-stats] test=%d/%d badge=%d/%d replay=%d/%d profiler=%d/%d clip=%d/%d/%d/%d "
                      "overflow=%d\n",
                      testPatternStats.commandCount, testPatternStats.textBytes, badgeStats.commandCount,
                      badgeStats.textBytes, replayStats.commandCount, replayStats.textBytes, profilerStats.commandCount,
                      profilerStats.textBytes, testPatternStats.maxClipDepth, badgeStats.maxClipDepth,
                      replayStats.maxClipDepth, profilerStats.maxClipDepth, overflow ? 1 : 0 );
    }

  private:
    const SkullbonezCore::UI::UIDrawList& m_testPattern;
    const SkullbonezCore::UI::UIDrawList& m_badge;
    const SkullbonezCore::UI::UIDrawList& m_replay;
    const SkullbonezCore::UI::UIDrawList& m_profiler;
};

void DrawUiTestPattern( UiDrawSubmission& submission, SkullbonezCore::UI::UIDrawList& drawList,
                        SkullbonezCore::Text::TextBatch& textBatch,
                        SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures,
                        SkullbonezCore::Rendering::Dx12GeometryOwner& renderCommands,
                        SkullbonezCore::Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    drawList.Clear();
    const SkullbonezCore::UI::UIDrawContext draw( screenW, screenH, drawList );
    draw.Rect( 0.0f, 0.0f, static_cast<float>( screenW ), static_cast<float>( screenH ), 0.20f, 0.31f, 0.36f, 1.0f );

    constexpr float tile = 88.0f;

    for ( float y = 0.0f; y < static_cast<float>( screenH ); y += tile )
    {

        for ( float x = 0.0f; x < static_cast<float>( screenW ); x += tile )
        {
            const bool alternate = ( ( static_cast<int>( x / tile ) + static_cast<int>( y / tile ) ) & 1 ) != 0;
            const float red = alternate ? 0.10f : 1.0f;
            const float green = alternate ? 0.78f : 0.72f;
            const float blue = alternate ? 0.96f : 0.18f;
            const float alpha = alternate ? 0.96f : 0.94f;
            draw.Rect( x, y, tile, tile, red, green, blue, alpha );
            draw.Rect( x + 12.0f, y + 12.0f, tile - 24.0f, 5.0f, 0.96f, 0.98f, 1.0f, 0.74f );
            draw.Rect( x + tile - 18.0f, y + 18.0f, 5.0f, tile - 32.0f, 0.12f, 0.20f, 0.24f, 0.54f );
        }
    }

    // Invariant: the same geometry feeds both screenshot blur/reference lanes;
    // changing it intentionally requires matching visual-baseline evidence.
    draw.Rect( 44.0f, 46.0f, 780.0f, 560.0f, 1.0f, 1.0f, 1.0f, 0.18f );
    draw.Rect( 76.0f, 116.0f, 720.0f, 8.0f, 0.98f, 0.12f, 0.46f, 0.82f );
    draw.Rect( 76.0f, 300.0f, 720.0f, 8.0f, 0.30f, 1.0f, 0.56f, 0.78f );
    draw.Rect( 76.0f, 484.0f, 720.0f, 8.0f, 0.38f, 0.54f, 1.0f, 0.82f );
    submission.Submit( drawList, textBatch, nullptr, renderTextures, renderCommands, renderDiagnostics, screenW, screenH );
}


SkullbonezCore::Core::MainMemoryStats
BuildMainMemoryOverlayStats( const DiagnosticsRuntime& diagnosticsRuntime,
                             const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects )
{

    // Concept: F6 is an allocator-growth overlay, not a memory profiler sample.
    // It can show the last cached replay totals and current model-store capacity,
    // but process reconciliation belongs to explicit diagnostics refreshes.
    SkullbonezCore::Core::MainMemoryStats stats = diagnosticsRuntime.MainMemoryStatsSnapshot();
    stats.process = SkullbonezCore::Core::MainMemoryProcessStats {};

    stats.gameObjects = gameObjects;
    stats.trackedEngineBytes = stats.replay.totalBytes + stats.gameObjects.totalBytes + stats.otherTrackedBytes;
    stats.unattributedProcessBytes = 0;
    stats.trackedOvershootBytes = 0;
    stats.reconciledTotalBytes = stats.trackedEngineBytes;
    stats.reconciliationDeltaBytes = 0;
    return stats;
}

void RenderReplayDivergenceCounter( SkullbonezCore::Text::TextBatch& textBatch, const OverlayDebugState& debug,
                                    const ReplayHudStatus& replayHud )
{

    if ( !debug.isTopTextHidden || !replayHud.divergenceValid )
    {
        return;
    }

    const int divergence = (std::max)( 0, static_cast<int>( replayHud.divergenceUnits + 0.5f ) );
    char value[32] = {};

    if ( divergence >= 1000 )
    {
        sprintf_s( value, sizeof( value ), "%d,%03d", divergence / 1000, divergence % 1000 );
    }
    else
    {
        sprintf_s( value, sizeof( value ), "%d", divergence );
    }

    char label[64] = {};
    sprintf_s( label, sizeof( label ), "DIVERGENCE %s u", value );
    const float size = 0.034f;
    const float textWidth = Text2d::MeasureText( size, label );
    const float x = -textWidth * 0.5f;
    const float y = Text2d::HalfH( textBatch ) - 0.115f;

    // Why: clean demo captures hide ordinary HUD chrome, so this single number
    // becomes the on-screen measure of how far the old and nudged futures split.
    Text2d::Render2dTextColor( textBatch, x + 0.002f, y - 0.002f, size, 0.0f, 0.0f, 0.0f, "%s", label );
    Text2d::Render2dTextColor( textBatch, x, y, size, 0.58f, 0.94f, 1.0f, "%s", label );
}
} // namespace

SkullbonezCore::Core::SbResult UiTextPass::EnsureGpuResources( Rendering::Dx12ResourceBuilder& renderResources,
                                                               Rendering::Dx12TextureOwner& renderTextures,
                                                               Rendering::Dx12GeometryOwner& renderGeometry,
                                                               const Assets::AssetSystem& assets, int screenW, int screenH )
{
    const SkullbonezCore::Core::SbResult fontResult = Text2d::BuildFont( m_resultDiagnostics, m_textBatch, renderResources,
                                                                         renderTextures, renderGeometry, assets, screenW,
                                                                         screenH, "Verdana" );

    if ( !fontResult.Ok() )
    {
        return fontResult;
    }

    // Invariant: the renderer atlas and UI layout consume the same 96 values
    // loaded from the baked font header. A device rebuild may confirm them but
    // cannot silently change hit geometry during the process lifetime.

    if ( !SkullbonezCore::UI::UIFontMetrics::Install( Text2d::charAdvance, 96 ) )
    {
        return m_resultDiagnostics.Failure( "Runtime/Render/UiTextPass",
                                            "Baked font metrics changed after UI layout publication." );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


void UiTextPass::ReleaseGpuResources( Rendering::Dx12TextureOwner* renderTextures,
                                      Rendering::Dx12GeometryOwner* renderGeometry )
{
    m_uiDrawSubmission.ReleaseGpuResources( renderGeometry );
    Text2d::DeleteFont( m_textBatch, renderTextures, renderGeometry );
    m_dxrReflectionPreviewTexture = 0;
}


bool UiTextPass::ShouldRender( const OverlayDebugState& debug, const SceneSessionState& scene, bool crossScenePauseLocked,
                               const CameraControlState& camera, const UI::InGameUI& ui, bool replayScrubberVisible,
                               bool replayPathVisualizerHasTarget ) const
{
    return debug.isTextOnly || !scene.isSceneMode || scene.isSceneText || debug.overlayMode != OverlayMode::None ||
           ui.NeedsUiTextPass() || ( crossScenePauseLocked && !debug.isTopTextHidden ) ||
           ( scene.isTestComplete && !debug.isTopTextHidden ) || replayScrubberVisible || replayPathVisualizerHasTarget ||
           ( camera.mode != RunCameraMode::Demo && camera.mode != RunCameraMode::Scene &&
             camera.mode != RunCameraMode::Director );
}


void UiTextPass::SetDxrReflectionPreviewTexture( uint32_t textureHandle )
{
    m_dxrReflectionPreviewTexture = textureHandle;
}


float UiTextPass::BeginFrame( RunTimerState& timers, const RuntimeRenderModelFrameView& models, double secondsPerFrame,
                              int screenW, int screenH )
{
    m_testPatternDrawList.Clear();
    m_badgeDrawList.Clear();
    m_replayDrawList.Clear();
    m_profilerDrawList.Clear();
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    assert( m_profiler && "UiTextPass requires a startup-bound profiler in profile builds." );
#endif
    Text2d::RebuildProjection( m_textBatch, (std::max)( 1, screenW ), (std::max)( 1, screenH ) );
    return UpdateFrameMetrics( timers, models, secondsPerFrame );
}


float UiTextPass::UpdateFrameMetrics( RunTimerState& timers, const RuntimeRenderModelFrameView& models,
                                      double secondsPerFrame )
{

    // Invariant: rolling diagnostics update before any overlay early return so
    // FPS, physics time, render time, and scene energy age at the same cadence.
    timers.updateTimer.StopTimer();
    timers.timeSinceLastRender += static_cast<float>( timers.updateTimer.GetElapsedTime() );
    timers.updateTimer.StartTimer();

    const double currentSceneEnergy = models.sceneKineticEnergy;
    timers.sceneEnergyAccumulator += currentSceneEnergy;
    ++timers.sceneEnergySampleCount;

    if ( timers.timeSinceLastRender > 0.5f )
    {

        if ( secondsPerFrame )
        {
            timers.rollingFpsTime = 1.0f / static_cast<float>( secondsPerFrame );
            timers.rollingPhysicsTime = timers.physicsTime;
            timers.rollingRenderTime = timers.renderTime;
        }

        if ( timers.sceneEnergySampleCount > 0 )
        {
            timers.rollingSceneEnergy = static_cast<float>( timers.sceneEnergyAccumulator /
                                                            static_cast<double>( timers.sceneEnergySampleCount ) );

            timers.sceneEnergyAccumulator = 0.0;
            timers.sceneEnergySampleCount = 0;
        }

        timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = timers.rollingSceneEnergy;

    if ( timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( timers.sceneEnergyAccumulator /
                                                    static_cast<double>( timers.sceneEnergySampleCount ) );
    }

    return sceneEnergyForDisplay;
}


void UiTextPass::RenderChromeStatus( const UiTextViewport& viewport, const OverlayDebugState& debug,
                                     bool crossScenePauseLocked, const SceneSessionState& scene,
                                     const CameraControlState& camera, int sceneQueueSize, const char* cameraModeLabel,
                                     Rendering::Dx12TextureOwner& renderTextures,
                                     Rendering::Dx12GeometryOwner& renderCommands,
                                     Rendering::Dx12Diagnostics& renderDiagnostics )
{
    Text::TextBatch& textBatch = m_textBatch;
    const char* rendererName = renderDiagnostics.GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler

    if ( debug.isTextOnly )
    {

        // Dark background covering the full viewport
        Text2d::Render2dQuad( textBatch, renderCommands, -0.55f, -0.45f, 0.55f, 0.45f, 0.08f, 0.08f, 0.12f, 1.0f );

        // Three rows of the pangram - each line uses a slightly different color
        // so hue/brightness fringing artifacts are visible on all channel combinations
        const float sz = 0.09f;
        Text2d::Render2dTextColor( textBatch, -0.46f, 0.22f, sz, 1.00f, 1.00f, 1.00f, "The quick brown fox" );
        Text2d::Render2dTextColor( textBatch, -0.46f, 0.07f, sz, 1.00f, 0.90f, 0.20f, "jumps over the" );
        Text2d::Render2dTextColor( textBatch, -0.46f, -0.08f, sz, 0.40f, 0.90f, 1.00f, "lazy dog" );

        // Renderer name in small text at bottom so we know which backend we're looking at
        Text2d::Render2dTextColor( textBatch, -0.46f, -0.38f, 0.015f, 0.60f, 0.60f, 0.60f, "renderer: %s", rendererName );

        {
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "TextOnly" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        return;
    }

    constexpr float TOP_RIGHT_BADGE_MARGIN = 12.0f;

    constexpr float TOP_RIGHT_BADGE_GAP = 6.0f;
    float topRightBadgeY = TOP_RIGHT_BADGE_MARGIN;
    m_badgeDrawList.Clear();
    const SkullbonezCore::UI::UIDrawContext badgeDraw( (std::max)( 1, viewport.screenW ), (std::max)( 1, viewport.screenH ),
                                                       m_badgeDrawList );

    const auto renderScenePauseBadge = [&]()
    {

        if ( debug.isTopTextHidden || ( !scene.isSceneMode && !crossScenePauseLocked && !scene.isTestComplete ) )
        {
            return;
        }

        const int screenW = (std::max)( 1, viewport.screenW );
        const SkullbonezCore::UI::UIDrawContext& draw = badgeDraw;
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
        const SkullbonezCore::UI::Style::UIRadii& radii = SkullbonezCore::UI::Style::Radii();

        char sceneLine[64] = {};

        if ( !scene.isSceneMode )
        {
            sprintf_s( sceneLine, sizeof( sceneLine ), "Demo  Frame %d", scene.currentFrame );
        }
        else if ( scene.targetFrameCount > 0 )
        {
            const int sceneFrame = scene.isTestComplete && scene.currentFrame > scene.targetFrameCount
                                       ? scene.targetFrameCount
                                       : scene.currentFrame;

            sprintf_s( sceneLine, sizeof( sceneLine ), "Scene %d/%d  Frame %d/%d", scene.currentSceneIndex + 1,
                       sceneQueueSize, sceneFrame, scene.targetFrameCount );
        }
        else
        {
            sprintf_s( sceneLine, sizeof( sceneLine ), "Scene %d/%d  Frame %d", scene.currentSceneIndex + 1, sceneQueueSize,
                       scene.currentFrame );
        }

        const char* stateLine = crossScenePauseLocked ? "P Pause Lock   Space advances"
                                                      : ( scene.isTestComplete ? "Scene complete" : "P pause lock" );

        const float titlePx = 11.5f;
        const float valuePx = 10.0f;
        const float padX = 10.0f;
        const float padY = 8.0f;
        const float lineGap = 15.0f;
        const float contentW = (std::max)( Text2d::MeasureText( titlePx, sceneLine ),
                                           Text2d::MeasureText( valuePx, stateLine ) );

        const float availableW = (std::max)( 80.0f, static_cast<float>( screenW ) - 24.0f );
        const float panelW = (std::min)( availableW, contentW + padX * 2.0f + 4.0f );
        const float panelH = 38.0f;
        const float x = static_cast<float>( screenW ) - TOP_RIGHT_BADGE_MARGIN - panelW;
        const float y = topRightBadgeY;

        SkullbonezCore::UI::Style::UIColor fill = palette.windowSubtle;
        fill.a = 0.88f;
        draw.RoundedPanel( { x, y, panelW, panelH }, radii.control, fill, palette.innerBorder );
        draw.RoundedRect( x + 1.0f, y + 1.0f, 4.0f, panelH - 2.0f, radii.smallButton,
                          crossScenePauseLocked ? palette.warningAccent.r : palette.accent.r,
                          crossScenePauseLocked ? palette.warningAccent.g : palette.accent.g,
                          crossScenePauseLocked ? palette.warningAccent.b : palette.accent.b, 0.90f );

        draw.Text( x + padX, y + padY, titlePx, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
                   sceneLine );

        draw.Text( x + padX, y + padY + lineGap, valuePx, crossScenePauseLocked ? palette.warningAccent.r : palette.accent.r,
                   crossScenePauseLocked ? palette.warningAccent.g : palette.accent.g,
                   crossScenePauseLocked ? palette.warningAccent.b : palette.accent.b, stateLine );

        topRightBadgeY = y + panelH + TOP_RIGHT_BADGE_GAP;
    };

    const auto renderRuntimeModeBadge = [&]()
    {

        // Why: clean validation/look-dev captures use --hide-top-text to remove
        // top-left chrome without changing scene simulation or camera state.

        if ( debug.isTopTextHidden )
        {
            return;
        }

        if ( camera.mode == RunCameraMode::Demo || camera.mode == RunCameraMode::Scene ||
             camera.mode == RunCameraMode::Director )
        {
            return;
        }

        const int screenW = (std::max)( 1, viewport.screenW );
        const SkullbonezCore::UI::UIDrawContext& draw = badgeDraw;
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
        const SkullbonezCore::UI::Style::UIRadii& radii = SkullbonezCore::UI::Style::Radii();

        const char* modeLine = cameraModeLabel;

        const char* detail = "RMB look  WASD  Space";
        SkullbonezCore::UI::Style::UIColor accent = palette.accent;

        if ( camera.mode == RunCameraMode::Attach )
        {
            detail = "LMB target  RMB orbit  F1  Enter";
            accent = palette.accentStrong;
        }
        else if ( camera.mode == RunCameraMode::Manipulator )
        {
            detail = "LMB drag  Space";
            accent = palette.accentStrong;
        }
        else if ( camera.mode == RunCameraMode::Launcher )
        {
            detail = "LMB fire  M mode";
        }
        else if ( camera.mode == RunCameraMode::Inspect )
        {
            detail = "RMB look  WASD  Space";
        }

        const float titlePx = 11.5f;
        const float detailPx = 9.5f;
        const float padX = 9.0f;
        const float padY = 7.0f;
        const float lineGap = 14.0f;
        const float textW = (std::max)( Text2d::MeasureText( titlePx, modeLine ), Text2d::MeasureText( detailPx, detail ) );

        const float availableW = (std::max)( 80.0f, static_cast<float>( screenW ) - 24.0f );
        const float panelW = (std::min)( availableW, textW + padX * 2.0f + 4.0f );
        const float panelH = 34.0f;
        const float x = static_cast<float>( screenW ) - TOP_RIGHT_BADGE_MARGIN - panelW;
        const float y = topRightBadgeY;

        SkullbonezCore::UI::Style::UIColor fill = palette.windowSubtle;
        fill.a = 0.86f;
        draw.RoundedPanel( { x, y, panelW, panelH }, radii.control, fill, palette.innerBorder );
        draw.RoundedRect( x + 1.0f, y + 1.0f, 4.0f, panelH - 2.0f, radii.smallButton, accent.r, accent.g, accent.b, 0.88f );

        draw.Text( x + padX, y + padY, titlePx, accent.r, accent.g, accent.b, modeLine );
        draw.Text( x + padX, y + padY + lineGap, detailPx, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, detail );
    };

    renderScenePauseBadge();
    renderRuntimeModeBadge();

    if ( !m_badgeDrawList.Empty() )
    {
        m_uiDrawSubmission.Submit( m_badgeDrawList, textBatch, m_gpuTiming, renderTextures, renderCommands,
                                   renderDiagnostics, (std::max)( 1, viewport.screenW ), (std::max)( 1, viewport.screenH ) );
    }
}


void UiTextPass::RenderChromeTail( const OverlayDebugState& debug, const ReplayHudStatus& replayHud, bool launcherCameraMode,
                                   const char* launcherFireModeLabel, double reproMessageAgeSeconds,
                                   Rendering::Dx12GeometryOwner& renderCommands )
{
    Text::TextBatch& textBatch = m_textBatch;
    (void)reproMessageAgeSeconds; // Development-only repro banner clock.
    RenderReplayDivergenceCounter( textBatch, debug, replayHud );

    // Crosshair - always visible when launcher mode is active, regardless of overlay state.
    // A tiny center gap keeps the target visible instead of covering it.

    if ( launcherCameraMode )
    {
        const float cArm = 0.020f;
        const float cGap = 0.004f;
        const float cHalf = 0.00045f;
        const float cShadowHalf = 0.00080f;
        Text2d::Render2dQuad( textBatch, renderCommands, -cArm, -cShadowHalf, -cGap, cShadowHalf, 0.0f, 0.0f, 0.0f, 0.40f );

        Text2d::Render2dQuad( textBatch, renderCommands, cGap, -cShadowHalf, cArm, cShadowHalf, 0.0f, 0.0f, 0.0f, 0.40f );

        Text2d::Render2dQuad( textBatch, renderCommands, -cShadowHalf, -cArm, cShadowHalf, -cGap, 0.0f, 0.0f, 0.0f, 0.40f );

        Text2d::Render2dQuad( textBatch, renderCommands, -cShadowHalf, cGap, cShadowHalf, cArm, 0.0f, 0.0f, 0.0f, 0.40f );

        Text2d::Render2dQuad( textBatch, renderCommands, -cArm, -cHalf, -cGap, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( textBatch, renderCommands, cGap, -cHalf, cArm, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( textBatch, renderCommands, -cHalf, -cArm, cHalf, -cGap, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( textBatch, renderCommands, -cHalf, cGap, cHalf, cArm, 0.80f, 0.96f, 1.0f, 0.88f );
        const char* fireModeLabel = launcherFireModeLabel;
        const float modeSz = 0.011f;
        const float modeW = Text2d::MeasureText( modeSz, fireModeLabel );
        Text2d::Render2dTextColor( textBatch, -modeW * 0.5f, -0.048f, modeSz, 0.72f, 0.94f, 1.0f, "%s", fireModeLabel );
#ifdef _DEBUG

        if ( debug.reproSnapshotMessage[0] != '\0' && reproMessageAgeSeconds <= debug.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, debug.reproSnapshotMessage );
            Text2d::Render2dTextColor( textBatch, -msgW * 0.5f, -0.065f, msgSz, 0.65f, 0.92f, 1.0f, "%s",
                                       debug.reproSnapshotMessage );
        }
#endif
    }
}


void UiTextPass::PrepareOperatorFrame( UI::InGameUIFrameData& UIData, const UiTextViewport& viewport, bool drawTestPattern,
                                       Rendering::Dx12TextureOwner& renderTextures,
                                       Rendering::Dx12GeometryOwner& renderCommands,
                                       Rendering::Dx12Diagnostics& renderDiagnostics )
{
    Text::TextBatch& textBatch = m_textBatch;
    PROFILE_BEGIN( "Frame/UI/BuildData" );
    UIData.screenW = viewport.screenW;
    UIData.screenH = viewport.screenH;
    UIData.rendererName = renderDiagnostics.GetRendererName();

    if ( drawTestPattern )
    {
        DrawUiTestPattern( m_uiDrawSubmission, m_testPatternDrawList, textBatch, renderTextures, renderCommands,
                           renderDiagnostics, UIData.screenW, UIData.screenH );
    }
}


void UiTextPass::ProjectOperatorDiagnostics( UI::InGameUIFrameData& UIData, const ReplayHudStatus& replayHud,
                                             RunTimerState& timers, const RuntimeRenderModelFrameView& models,
                                             DiagnosticsRuntime& diagnosticsRuntime, UI::InGameUI& ui,
                                             Threading::WorkerPool* workerPool, double secondsPerFrame,
                                             Rendering::Dx12Diagnostics& renderDiagnostics )
{
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    assert( m_profiler && "UiTextPass requires a startup-bound profiler in profile builds." );
    const SkullbonezCore::Core::Profiler& profiler = *m_profiler;
#endif
    UIData.UIDrawCalls = timers.lastUIDrawCalls;
    UIData.visibility = ProjectRenderVisibilityDiagnostics( renderDiagnostics.GetFrameVisibilityStats() );
    UIData.fps = timers.rollingFpsTime > 0.0f
                     ? timers.rollingFpsTime
                     : ( secondsPerFrame > 0.0 ? 1.0f / static_cast<float>( secondsPerFrame ) : 0.0f );

    UIData.renderMs = ( timers.rollingRenderTime > 0.0f ? timers.rollingRenderTime : timers.renderTime ) * 1000.0f;

    UIData.physicsMs = ( timers.rollingPhysicsTime > 0.0f ? timers.rollingPhysicsTime : timers.physicsTime ) * 1000.0f;

    UIData.cpuFrameMs = timers.cpuFrameWorkMs;
    UIData.gpuFrameMs = timers.gpuFrameWorkMs;
    {

        // Concept: render draw attribution is copied through UIData while
        // the render diagnostics capability is already borrowed by Run. The
        // profiler tab never needs the wide renderer facade to explain draw
        // calls.
        const auto drawTrace = renderDiagnostics.GetFrameDrawCallTrace();
        const int sourceNodeCount = (std::max)( 0, drawTrace.nodeCount );
        const int nodeCount = (std::min)( sourceNodeCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
        SkullbonezCore::UI::ProfilerTab::DrawTraceSnapshot& uiTrace = UIData.profiler.drawTrace;
        uiTrace.nodeCount = nodeCount;
        uiTrace.nodeOverflowCount = drawTrace.nodeOverflowCount + ( sourceNodeCount - nodeCount );
        uiTrace.eventCount = drawTrace.eventCount;
        uiTrace.eventOverflowCount = drawTrace.eventOverflowCount;
        uiTrace.scopeMismatchCount = drawTrace.scopeMismatchCount;

        if ( drawTrace.nodes )
        {

            for ( int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                const auto& source = drawTrace.nodes[nodeIndex];
                SkullbonezCore::UI::ProfilerTab::DrawTraceNodeSnapshot& target = uiTrace.nodes[nodeIndex];
                target.name = source.name ? source.name : "";
                target.leafName = source.leafName ? source.leafName : target.name;
                target.hash = source.hash;
                target.parentIndex = source.parentIndex;
                target.depth = source.depth;
                target.drawCallCount = source.drawCallCount;
                target.vertexCount = source.vertexCount;
                target.instanceCount = source.instanceCount;
            }
        }
    }
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        static_assert( SkullbonezCore::UI::ProfilerTab::MAX_MARKERS == SkullbonezCore::Core::Profiler::MAX_MARKERS,
                       "UI profiler snapshot capacity must match SkullbonezCore::Core::Profiler markers" );

        static_assert( SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES ==
                           SkullbonezCore::Core::Profiler::MAX_WORKER_CORES,
                       "UI worker sample snapshot capacity must match SkullbonezCore::Core::Profiler samples" );

        SkullbonezCore::UI::ProfilerTab::FrameSnapshot& profilerFrame = UIData.profiler;
        profilerFrame.markerCount = (std::min)( profiler.MarkerCount(), SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );

        for ( int markerIndex = 0; markerIndex < profilerFrame.markerCount; ++markerIndex )
        {
            const SkullbonezCore::Core::Profiler::Marker& source = profiler.GetMarker( markerIndex );
            const int paletteIndex = source.colorIndex >= 0
                                         ? source.colorIndex % SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE
                                         : 0;

            const SkullbonezCore::Core::Profiler::BarColor&
                color = SkullbonezCore::Core::Profiler::BAR_PALETTE[paletteIndex];

            SkullbonezCore::UI::ProfilerTab::MarkerSnapshot& target = profilerFrame.markers[markerIndex];
            target.name = source.name ? source.name : "";
            target.leafName = source.leafName ? source.leafName : target.name;
            target.hash = source.hash;
            target.parentIndex = source.parentIndex;
            target.depth = source.depth;
            target.lastFrameMs = source.lastFrameMs;
            target.lastSelfMs = source.lastSelfMs;
            target.avgMs = source.avgMs;
            target.selfAvgMs = source.selfAvgMs;
            target.lastFrameWorkerMs = source.lastFrameWorkerMs;
            target.workerAvgMs = source.workerAvgMs;
            target.p50Ms = source.p50Ms;
            target.p99Ms = source.p99Ms;
            target.colorR = color.r;
            target.colorG = color.g;
            target.colorB = color.b;
        }

        profilerFrame.workerCoreSampleCount = (std::min)( profiler.WorkerCoreSampleCount(),
                                                          SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );

        for ( int sampleIndex = 0; sampleIndex < profilerFrame.workerCoreSampleCount; ++sampleIndex )
        {
            const SkullbonezCore::Core::Profiler::WorkerCoreSample& source = profiler.GetWorkerCoreSample( sampleIndex );

            SkullbonezCore::UI::ProfilerTab::WorkerCoreSampleSnapshot& target = profilerFrame.workerCoreSamples[sampleIndex];

            target.workerIndex = source.workerIndex;
            target.jobCount = source.jobCount;
            target.coreMs = source.coreMs;
            target.avgCoreMs = source.avgCoreMs;
            target.spanStartMs = source.spanStartMs;
            target.spanEndMs = source.spanEndMs;
            UIData.workerCoreTotalMs += (std::max)( 0.0f, target.coreMs );
        }
    }
#endif
#if defined( TRACY_ENABLE )
    {
        const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus
            tracyStatus = SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();

        UIData.profiler.tracyBuildEnabled = tracyStatus.buildEnabled;
        UIData.profiler.tracyInitialized = tracyStatus.initialized;
        UIData.profiler.tracyViewerConnected = tracyStatus.viewerConnected;
    }
#endif
    {

        // Concept: marker enumeration stays in the runtime pass that owns
        // profiler access. The UI receives a bounded frame snapshot so
        // drawing and hit testing do not reach into profiler globals.
        auto markerOptionExists = [&]( uint32_t hash, bool isFrameTotal ) -> bool
        {

            for ( int i = 0; i < UIData.profilerMarkerOptionCount; ++i )
            {
                const SkullbonezCore::UI::UIProfilerMarkerOption& option = UIData.profilerMarkerOptions[i];

                if ( option.isFrameTotal == isFrameTotal && ( isFrameTotal || option.hash == hash ) )
                {
                    return true;
                }
            }

            return false;
        };

        // Why: callers label one complete profiler option; this bounded
        // append only normalizes nullable names and non-negative timings.
        auto addMarkerOption = [&]( const SkullbonezCore::UI::UIProfilerMarkerOption& input )
        {

            if ( UIData.profilerMarkerOptionCount >= SkullbonezCore::UI::UI_PROFILER_MARKER_OPTION_MAX ||
                 markerOptionExists( input.hash, input.isFrameTotal ) )
            {
                return;
            }

            SkullbonezCore::UI::UIProfilerMarkerOption&
                option = UIData.profilerMarkerOptions[UIData.profilerMarkerOptionCount++];

            option = input;
            option.name = input.name ? input.name : "";
            option.leafName = input.leafName ? input.leafName : option.name;
            option.cpuMs = (std::max)( 0.0f, input.cpuMs );
            option.cpuAverageMs = (std::max)( 0.0f, input.cpuAverageMs );
            option.workerMs = (std::max)( 0.0f, input.workerMs );
            option.workerAverageMs = (std::max)( 0.0f, input.workerAverageMs );
            option.gpuMs = (std::max)( 0.0f, input.gpuMs );
        };

        float frameAverageMs = UIData.cpuFrameMs;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        {
            static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );

            for ( int markerIndex = 0; markerIndex < profiler.MarkerCount(); ++markerIndex )
            {
                const SkullbonezCore::Core::Profiler::Marker& marker = profiler.GetMarker( markerIndex );

                if ( marker.hash == kFrameHash )
                {
                    frameAverageMs = marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
                    break;
                }
            }
        }
#endif
        const SkullbonezCore::UI::Style::UIColor& mainColor = SkullbonezCore::UI::Style::Palette().accent;
        addMarkerOption( SkullbonezCore::UI::UIProfilerMarkerOption { .name = "Frame Total",
                                                                      .leafName = "Frame Total",
                                                                      .hash = SkullbonezCore::UI::UI_PROFILER_FRAME_TOTAL_HASH,
                                                                      .cpuMs = UIData.cpuFrameMs,
                                                                      .cpuAverageMs = frameAverageMs,
                                                                      .gpuMs = UIData.gpuFrameMs,
                                                                      .colorR = mainColor.r,
                                                                      .colorG = mainColor.g,
                                                                      .colorB = mainColor.b,
                                                                      .hasGpu = true,
                                                                      .sampleValid = true,
                                                                      .isFrameTotal = true } );

#if defined( SKULLBONEZ_PROFILE_ENABLED )
        auto addProfilerMarker = [&]( const SkullbonezCore::Core::Profiler::Marker& marker )
        {
            const SkullbonezCore::Core::Profiler::BarColor&
                color = SkullbonezCore::Core::Profiler::BAR_PALETTE[marker.colorIndex %
                                                                    SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE];

            addMarkerOption( SkullbonezCore::UI::UIProfilerMarkerOption { .name = marker.name,
                                                                          .leafName = marker.leafName,
                                                                          .hash = marker.hash,
                                                                          .cpuMs = marker.lastFrameMs,
                                                                          .cpuAverageMs = marker.avgMs > 0.0f ? marker.avgMs
                                                                                                              : marker.lastFrameMs,
                                                                          .workerMs = marker.lastFrameWorkerMs,
                                                                          .workerAverageMs = marker.workerAvgMs > 0.0f
                                                                                                 ? marker.workerAvgMs
                                                                                                 : marker.lastFrameWorkerMs,
                                                                          .gpuMs = marker.hasGpu ? marker.gpuLastFrameMs : 0.0f,
                                                                          .colorR = color.r,
                                                                          .colorG = color.g,
                                                                          .colorB = color.b,
                                                                          .hasGpu = marker.hasGpu,
                                                                          .sampleValid = true,
                                                                          .isFrameTotal = false } );
        };

        static constexpr uint32_t kPinnedMarkerHashes[] = { ::HashStr( "Frame/Physics" ), ::HashStr( "Frame/Physics/Step" ),
                                                            ::HashStr( "Frame/Physics/Narrowphase/PersistentContacts/"
                                                                       "SolveRows" ),
                                                            ::HashStr( "Frame/Render" ), ::HashStr( "Frame/UI" ) };

        for ( uint32_t pinnedHash : kPinnedMarkerHashes )
        {

            for ( int markerIndex = 0; markerIndex < profiler.MarkerCount(); ++markerIndex )
            {
                const SkullbonezCore::Core::Profiler::Marker& marker = profiler.GetMarker( markerIndex );

                if ( marker.hash == pinnedHash )
                {
                    addProfilerMarker( marker );
                    break;
                }
            }
        }

        for ( int markerIndex = 0; markerIndex < profiler.MarkerCount(); ++markerIndex )
        {
            addProfilerMarker( profiler.GetMarker( markerIndex ) );
        }
#endif
    }
    UIData.workerThreadCount = workerPool ? workerPool->GetThreadCount() : 0;
    UIData.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
    UIData.now = timers.simulationTimer.GetTotalTime();
    UIData.replayMemoryPreset = replayHud.memoryPreset;
    UIData.replayMemoryRequestedRetentionSeconds = replayHud.requestedRetentionSeconds;
    UIData.replayMemoryRequestedBudgetMiB = replayHud.requestedBudgetMiB;
    UIData.replayMemoryPresentationRetentionSeconds = replayHud.presentationRetentionSeconds;
    UIData.replayMemorySolverRetentionSeconds = replayHud.solverRetentionSeconds;
    UIData.replayMemoryBudgetClamped = replayHud.memoryBudgetClamped;
    UIData.replayMemorySolverWindowReduced = replayHud.solverWindowReduced;
    UIData.predictionRevealRate = replayHud.predictionRevealRate;
    const bool memoryTabActive = ui.IsVisible() && !ui.IsMinimized() && ui.GetActiveTab() == InGameUITab::Memory;
    const bool memoryOverlayEnabled = ui.IsMemoryOverlayEnabled();
    UIData.reserveCapacityRows = nullptr;
    UIData.reserveCapacityRowCount = 0;

    if ( memoryTabActive )
    {

        // Why: memory sampling belongs to DiagnosticsRuntime; the render host
        // only decides whether the UI pass needs to draw.
        assert( replayHud.memoryStatsValid );
        UIData.mainMemory = diagnosticsRuntime.RefreshMainMemoryStats( replayHud.memoryStats, models.gameObjectMemory,
                                                                       UIData.now, false, false );
    }
    else if ( memoryOverlayEnabled )
    {

        // Why: F6 stays event/counter driven. Merely leaving the overlay up
        // must not start a process-memory or replay-memory sampling heartbeat.
        UIData.mainMemory = BuildMainMemoryOverlayStats( diagnosticsRuntime, models.gameObjectMemory );
    }

    if ( memoryTabActive || memoryOverlayEnabled )
    {

        // The render snapshot is cheap owner-maintained accounting; unlike
        // process memory sampling, it is safe to refresh for the F6 overlay.
        UIData.renderMemory = ProjectRenderMemoryDiagnostics( renderDiagnostics.GetRenderMemoryStats() );
        UIData.reserveGrowthEventTotalCount = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::GrowthEventCount();

        UIData.reserveGrowthEventDroppedCount = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::
            GrowthEventDroppedCount();

        UIData.reserveGrowthEventCount = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::
            CopyRecentGrowthEvents( UIData.reserveGrowthEvents, SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );
    }

    if ( memoryTabActive )
    {
        const std::span<const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView>
            capacityRows = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CapacityRows();

        UIData.reserveCapacityRowCount = (std::min)( static_cast<int>( capacityRows.size() ),
                                                     SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX );

        for ( int index = 0; index < UIData.reserveCapacityRowCount; ++index )
        {
            const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView&
                source = capacityRows[static_cast<std::size_t>( index )];
            SkullbonezCore::UI::UIRuntimeReserveCapacityRow& destination = m_reserveCapacityRows[index];
            strncpy_s( destination.ownerName, sizeof( destination.ownerName ), source.ownerName ? source.ownerName : "",
                       _TRUNCATE );

            strncpy_s( destination.capacityReason, sizeof( destination.capacityReason ),
                       source.capacityReason ? source.capacityReason : "", _TRUNCATE );

            strncpy_s( destination.subsystemName, sizeof( destination.subsystemName ),
                       SkullbonezCore::Core::Allocation::RuntimeReserveSubsystemName( source.subsystem ), _TRUNCATE );

            destination.elementSizeBytes = source.elementSizeBytes;
            destination.currentCapacity = source.currentCapacity;
            destination.liveCount = source.liveCount;
            destination.sessionHighWater = source.sessionHighWater;
            destination.residentBytes = source.residentBytes;
        }

        UIData.reserveCapacityRows = m_reserveCapacityRows;
    }
}


void UiTextPass::ProjectOperatorPresentation( UI::InGameUIFrameData& UIData, const SceneSessionState& scene,
                                              const RuntimeViewModel& view,
                                              const SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                              const UI::OperatorEditorFrameView& operatorEditorView,
                                              bool sceneHasCurrentEntry, const char* currentScenePath,
                                              int currentSceneBrowserIndex, float sceneEnergyForDisplay )
{
    const char* sceneName = "";

    if ( view.sceneMode && sceneHasCurrentEntry && currentScenePath )
    {
        sceneName = SceneFileNameFromPath( currentScenePath );
    }

    UIData.sceneName = sceneName;
    UIData.sceneOptions = sceneBrowser.namePtrs.empty() ? nullptr : sceneBrowser.namePtrs.data();
    UIData.sceneOptionCount = static_cast<int>( sceneBrowser.namePtrs.size() );
    UIData.selectedSceneOption = currentSceneBrowserIndex;
    UIData.selectedCineModeSceneOption = sceneBrowser.selectedCineModeSceneIndex;
    UIData.modelCount = view.modelCount;
    UIData.currentFrame = view.frame;
    UIData.targetFrameCount = view.targetFrameCount;
    UIData.rngSeed = scene.rngSeed;
    UIData.solverBallCount = scene.solverBallCount;
    UIData.solverBoxCount = scene.solverBoxCount;
    UIData.currentSceneIndex = view.sceneIndex;
    UIData.sceneCount = view.sceneCount;
    UIData.sceneMode = view.sceneMode;
    UIData.scenePhysicsEnabled = view.scenePhysics;
    UIData.sceneTextEnabled = view.sceneText;
    UIData.fixedStep = view.fixedStep;
    UIData.exitOnComplete = scene.isExitOnComplete;
    UIData.testComplete = scene.isTestComplete;
    UIData.sceneEnergy = sceneEnergyForDisplay;
    UIData.timeScale = view.timeScale;
    UIData.presentationInterpolation = view.presentationInterpolation;
    UIData.presentationPinned = view.presentationPinned;
    UIData.presentationAlpha = view.presentationAlpha;
    UIData.canSaveSceneDefaults = view.sceneMode && sceneHasCurrentEntry && currentScenePath && currentScenePath[0] != '\0';

    // Invariant: representative legacy controls display the same immutable
    // values supplied to the secondary editor for this frame.
    UIData.operatorEditor = operatorEditorView;
    UIData.sceneName = UIData.operatorEditor.scene.sceneName;
    UIData.modelCount = UIData.operatorEditor.scene.modelCount;
    UIData.currentFrame = UIData.operatorEditor.scene.currentFrame;
    UIData.currentSceneIndex = UIData.operatorEditor.scene.currentSceneIndex;
    UIData.sceneCount = UIData.operatorEditor.scene.sceneCount;
    UIData.timeScale = UIData.operatorEditor.scene.timeScale;
    UIData.worldGravity = UIData.operatorEditor.property.worldGravity;
    UIData.worldFluidHeight = UIData.operatorEditor.property.worldFluidHeight;
    UIData.worldFluidDensity = UIData.operatorEditor.property.worldFluidDensity;
    UIData.vsyncEnabled = UIData.operatorEditor.rendering.vsyncEnabled;
    UIData.presentationInterpolation = UIData.operatorEditor.rendering.presentationInterpolation;
    UIData.presentationAlpha = UIData.operatorEditor.rendering.presentationAlpha;
    UIData.cinematicRendering = UIData.operatorEditor.rendering.cinematicRendering;
    UIData.replayMemoryPreset = UIData.operatorEditor.replay.memoryPreset;
    UIData.replayMemoryRequestedRetentionSeconds = UIData.operatorEditor.replay.requestedRetentionSeconds;
    UIData.replayMemoryRequestedBudgetMiB = UIData.operatorEditor.replay.requestedBudgetMiB;
    UIData.replayMemoryPresentationRetentionSeconds = UIData.operatorEditor.replay.presentationRetentionSeconds;
    UIData.replayMemorySolverRetentionSeconds = UIData.operatorEditor.replay.solverRetentionSeconds;
    UIData.replayMemoryBudgetClamped = UIData.operatorEditor.replay.memoryBudgetClamped;
    UIData.replayMemorySolverWindowReduced = UIData.operatorEditor.replay.solverWindowReduced;
}


void UiTextPass::ProjectOperatorSettings( UI::InGameUIFrameData& UIData, const OverlayDebugState& debug,
                                          const RenderPresentationSettings& renderPresentation, const SceneWorld& world,
                                          const SkullbonezCore::Core::EngineConfig& config,
                                          const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                          bool cinematicRendering )
{
    UIData.modelCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( config );
    UIData.textOnly = debug.isTextOnly;
    UIData.vsyncEnabled = renderPresentation.vsyncEnabled;
    UIData.pipelineSyncEnabled = renderPresentation.pipelineSyncEnabled;
    UIData.worldGravity = world.Environment().GetGravity();
    UIData.worldFluidHeight = world.Environment().GetFluidSurfaceHeight();
    UIData.worldFluidDensity = world.Environment().GetFluidDensity();
    UIData.physicsDebug = BuildDiagnosticsPhysicsUIStatus( debug );
    UIData.physicsSleepEnabled = world.Physics().IsSleepEnabled();
    const Gameplay::TornadoFieldConfig& tornadoField = world.Tornado().GetFieldConfig();
    UIData.tornadoEnabled = tornadoField.enabled;
    UIData.tornadoVisualShell = world.Tornado().VisualSettings().enabled && tornadoField.enabled;
    UIData.tornadoFieldVectors = tornadoField.visualizeVelocityField;
    UIData.tornadoRadius = tornadoField.radius;
    UIData.tornadoHeight = tornadoField.height;
    UIData.tornadoInwardAcceleration = tornadoField.inwardAcceleration;
    UIData.tornadoSwirlAcceleration = tornadoField.swirlAcceleration;
    UIData.tornadoLiftAcceleration = tornadoField.liftAcceleration;
    UIData.terrainFrictionCoeff = config.physicsMaterial.frictionCoeff;
    UIData.objectFrictionCoeff = config.physicsMaterial.objectFrictionCoeff;
    UIData.rollingFrictionCoeff = config.physicsMaterial.rollingFrictionCoeff;
    UIData.waterFreezeDebug = debug.isWaterFreezeDebug;
    UIData.waterFlatDebug = debug.isWaterFlatDebug;
    UIData.terrainHidden = debug.isTerrainHidden;
    UIData.waterHidden = debug.isWaterHidden;
    UIData.waterNoReflect = debug.isWaterNoReflect;
    UIData.waterRTReflect = debug.isWaterRTReflect;
    UIData.cinematicRendering = cinematicRendering;
    UIData.ordinaryRender = config.ordinaryRender;
    UIData.cinematic = cinematic;
}


void UiTextPass::ProjectOperatorInteraction( UI::InGameUIFrameData& UIData, const RunRayCastTestState& rayCastTest,
                                             const RunEditorPlacementState& editor, const RuntimeInputContext& runtimeInput,
                                             const CameraControlState& camera, const UI::InGameUI& ui,
                                             uint32_t cameraModeEnabledMask, const char* cameraModeLabel )
{
    UIData.trackHeight = camera.trackBallRow.IsValid() ? camera.trackHeight : 0.0f;
    UIData.autoCycleInterval = camera.autoCycleInterval > 0.0f ? camera.autoCycleInterval : 0.0f;
    UIData.rayCastVisualization = rayCastTest.visualizeRays;
    UIData.rayCastImpulseStrength = rayCastTest.impulseStrength;
    UIData.launcherProjectileSpeed = rayCastTest.projectileSpeed;
    const RuntimeInputMode runtimeInputMode = runtimeInput.CurrentMode();
    UIData.cameraModeIndex = static_cast<int>( camera.mode );
    UIData.cameraModeEnabledMask = cameraModeEnabledMask;
    UIData.runtimeInputModeLabel = cameraModeLabel;
    UIData.cameraMouseActive = ( runtimeInputMode == RuntimeInputMode::FlyCamera ||
                                 runtimeInputMode == RuntimeInputMode::Launcher ||
                                 runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
                               !ui.BlocksCameraMouse();

    UIData.nativeCursorVisible = !UIData.cameraMouseActive;
    UIData.editorModeEnabled = editor.editorModeEnabled;
    UIData.editorPlacementMode = editor.placementModeEnabled;
    UIData.editorPlaceStatic = editor.placeStaticObject;
    UIData.editorTerrainAlign = editor.autoTerrainAlign;
    UIData.editorViewportLookActive = editor.viewportLookActive;
    UIData.editorObjectType = editor.objectType;
    UIData.editorUndoDepth = static_cast<int>( editor.history.UndoDepth() );
    UIData.editorRedoDepth = static_cast<int>( editor.history.RedoDepth() );
}


void UiTextPass::SubmitOperatorFrame( UI::InGameUIFrameData& UIData, UI::InGameUI& ui,
                                      const RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews,
                                      Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                                      Rendering::Dx12TextureOwner& renderTextures,
                                      Rendering::Dx12GeometryOwner& renderCommands,
                                      Rendering::Dx12Diagnostics& renderDiagnostics, int uiPassDrawCallStart )
{
    Text::TextBatch& textBatch = m_textBatch;

    // Invariant: the UI projection below omits backend handles. This
    // renderer-owned copy retains them only until submission resolves the
    // recorded catalog index for this same frame.
    RuntimeRenderTargetPreviewSnapshot resolvedPreviews = renderTargetPreviews;
    {
        const uint32_t dxrReflection = m_dxrReflectionPreviewTexture;

        if ( resolvedPreviews.count < static_cast<int>( resolvedPreviews.targets.size() ) )
        {
            RuntimeRenderTargetPreview& preview = resolvedPreviews.targets[static_cast<size_t>( resolvedPreviews.count++ )];

            preview.label = "DXR Reflection";
            preview.textureHandle = dxrReflection;
            preview.width = UIData.screenW * 2;
            preview.height = UIData.screenH * 2;
            preview.available = UIData.waterRTReflect && !UIData.waterNoReflect && dxrReflection != 0;
            preview.depth = false;
            preview.hdr = false;
        }

        for ( int index = 0; index < resolvedPreviews.count; ++index )
        {
            const RuntimeRenderTargetPreview& source = resolvedPreviews.targets[static_cast<size_t>( index )];
            SkullbonezCore::UI::UIRenderTargetPreviewResource&
                preview = UIData.renderTargetPreviews[UIData.renderTargetPreviewCount++];

            preview.label = source.label;
            preview.width = source.width;
            preview.height = source.height;
            preview.available = source.available && source.width > 0 && source.height > 0;
            preview.depth = source.depth;
            preview.hdr = source.hdr;
        }
    }
    PROFILE_END( "Frame/UI/BuildData" );

    PROFILE_BEGIN( "Frame/UI/PreFlushText" );
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "PreFlushText" );
        Text2d::FlushText( textBatch, renderTextures, renderCommands );
    }
    PROFILE_END( "Frame/UI/PreFlushText" );
    UIData.drawCallsBeforeUI = uiPassDrawCallStart;
    const UI::UIDrawList& uiDrawList = ui.Draw( UIData );
    m_uiDrawSubmission.SubmitWithPreviews( uiDrawList, resolvedPreviews, textBatch, m_gpuTiming, assets, renderResources,
                                           renderTextures, renderCommands, renderDiagnostics, UIData.screenW,
                                           UIData.screenH );

    PROFILE_BEGIN( "Frame/UI/PostFlushText" );
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/UI/PostFlushText" );
        Text2d::FlushText( textBatch, renderTextures, renderCommands );
    }
    PROFILE_END( "Frame/UI/PostFlushText" );
}


void UiTextPass::RenderOverlayContent( const UiTextViewport& viewport, OverlayMode mode, int modelCount,
                                       float rollingFpsTime, float sceneEnergyForDisplay,
                                       Rendering::Dx12TextureOwner& renderTextures,
                                       Rendering::Dx12GeometryOwner& renderCommands,
                                       Rendering::Dx12Diagnostics& renderDiagnostics )
{
    Text::TextBatch& textBatch = m_textBatch;
    const float hw = Text2d::HalfW( textBatch );
    const float hh = Text2d::HalfH( textBatch );
    const float mX = 0.022f;
    const float mY = 0.015f;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    assert( m_profiler && "UiTextPass requires a startup-bound profiler in profile builds." );
    const SkullbonezCore::Core::Profiler& profiler = *m_profiler;
    const UI::UIProfilerOverlayPresenter profilerOverlay;
#endif

    // --- Overlay: None ---

    if ( mode == OverlayMode::None )
    {
        return;
    }

    // --- Overlay: Scene telemetry ---

    if ( mode == OverlayMode::SceneStats )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.012f;
        const float lineH = 0.025f;
        const float panPad = 0.014f;
        const float panW = 0.36f;
        const float panH = panPad * 2.0f + titleSz + lineH * 2.0f;
        const float panX0 = -( hw - mX );
        const float panY0 = -( hh - mY );
        const float panX1 = panX0 + panW;
        const float panY1 = panY0 + panH;

        Text2d::Render2dQuad( textBatch, renderCommands, panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );
        Text2d::Render2dTextColor( textBatch, panX0 + panPad, panY1 - panPad - titleSz, titleSz, 1.0f, 0.85f, 0.35f,
                                   "SCENE TELEMETRY" );

        Text2d::Render2dTextColor( textBatch, panX0 + panPad, panY1 - panPad - titleSz - lineH, entrySz, 0.85f, 0.85f, 0.85f,
                                   "Model Count: %d", modelCount );

        Text2d::Render2dTextColor( textBatch, panX0 + panPad, panY1 - panPad - titleSz - lineH * 2.0f, entrySz, 0.85f, 0.85f,
                                   0.85f, "Scene Energy: %.6f", sceneEnergyForDisplay );

        return;
    }

    // --- Overlay: Visual profiler bars (normalized or absolute) ---
#if defined( SKULLBONEZ_PROFILE_ENABLED )

    if ( mode == OverlayMode::BarsNormalized || mode == OverlayMode::BarsAbsolute )
    {

        // Panel anchored bottom-left, filling most of the width. Height kept modest - leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height

        const float panX = -( hw - mX ) + mX * 0.5f; // slight left margin

        const float panY = -( hh - mY ) + mY * 0.5f; // slight bottom margin

        const bool absolute = ( mode == OverlayMode::BarsAbsolute );

        m_profilerDrawList.Clear();
        const UI::UIDrawContext profilerDraw( viewport.screenW, viewport.screenH, m_profilerDrawList );
        profilerOverlay.RecordBarOverlay( profiler.FrameView(), profilerDraw, panX, panY, panW, panH, absolute );
        {
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "ProfilerBars" );
            m_uiDrawSubmission.Submit( m_profilerDrawList, textBatch, m_gpuTiming, renderTextures, renderCommands,
                                       renderDiagnostics, viewport.screenW, viewport.screenH );
        }
        return;
    }
#endif

    // --- Overlay: Keys reference screen (compact, bottom-left) ---

    if ( mode == OverlayMode::Keys )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.011f;
        const float lineH = 0.020f;
        const int nRows = 15;
        const float panPad = 0.012f;
        const float titleGap = 0.016f; // space between title baseline and first entry
        const float keyW = 0.058f;     // key-name column width

        const float descW = 0.120f; // description column width

        const float colGap = 0.012f; // gap between the two content columns

        // Panel dimensions - anchored to bottom-left corner
        const float panH = panPad + titleSz + titleGap + static_cast<float>( nRows ) * lineH + panPad;
        const float panW = panPad + keyW + descW + colGap + keyW + descW + panPad;
        const float panX0 = -( hw - mX );
        const float panY0 = -( hh - mY );
        const float panX1 = panX0 + panW;
        const float panY1 = panY0 + panH;

        Text2d::Render2dQuad( textBatch, renderCommands, panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );

        // Title left-aligned inside panel
        const float titleY = panY1 - panPad - titleSz;
        Text2d::Render2dTextColor( textBatch, panX0 + panPad, titleY, titleSz, 1.0f, 0.85f, 0.35f, "CONTROL REFERENCE" );

        // Column X positions
        const float col1Key = panX0 + panPad;
        const float col1Desc = col1Key + keyW;
        const float col2Key = col1Desc + descW + colGap;
        const float col2Desc = col2Key + keyW;
        const float firstY = titleY - titleGap;

        struct KeyEntry
        {
            const char* key;
            const char* desc;
        };
        static const KeyEntry kLeft[nRows] = {
            { "Tab", "Camera mode" },          { "N", "Launcher mode" },
            { "M", "Launcher fire mode" },     { "F1", "Attach follow mode" },
            { "Enter", "Attach pin / repro" }, { "F", "Fly mode" },
            { "WASD", "Move camera" },         { "RMB", "Look" },
            { "Shift", "Sprint (3x speed)" },  { "LMB", "Pick / drag / fire" },
            { "V", "Collision visual" },       { "Space", "Play paused scene" },
            { "R/Bksp", "Reset scene" },       { "F3", "Screenshot" },
            { "F5", "CPU histogram" },
        };

        static const KeyEntry kRight[nRows] = {
            { "Esc", "Min/expand UI" },    { "Esc Esc", "Quit" },         { "P", "Pause lock" },
            { "1", "Freeze water" },       { "2", "Reflection mode" },    { "3", "Toggle water flat" },
            { "4", "Toggle terrain" },     { "5", "Toggle water" },       { "6", "Debug body alpha" },
            { "G", "Broadphase overlay" }, { "C", "Physics debug" },      { "O", "Terrain probe" },
            { "PgUp/Dn", "Water height" }, { "F7/F8", "Pipeline stage" }, { "F6", "Memory waterline" },
        };

        for ( int i = 0; i < nRows; ++i )
        {
            float y = firstY - static_cast<float>( i ) * lineH;
            Text2d::Render2dTextColor( textBatch, col1Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kLeft[i].key );
            Text2d::Render2dTextColor( textBatch, col1Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kLeft[i].desc );
            Text2d::Render2dTextColor( textBatch, col2Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kRight[i].key );
            Text2d::Render2dTextColor( textBatch, col2Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kRight[i].desc );
        }

        return;
    }

    // --- Overlay: Timers / HUD (OverlayMode::Timers) ---

    // SkullbonezCore::Core::Profiler overlay - bottom-left anchored.
    // Compiled out in Release; always shown when overlay is Timers in Debug/Profile.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        const float lineH = 0.018f;
        const float profFSz = 0.012f;
        const float padY = lineH * 1.2f;
        m_profilerDrawList.Clear();
        const UI::UIDrawContext profilerDraw( viewport.screenW, viewport.screenH, m_profilerDrawList );
        profilerOverlay.RecordOverlay( profiler.FrameView(), profilerDraw, -( hw - mX ), -( hh - mY ) - padY, lineH, profFSz,
                                       rollingFpsTime );

        m_uiDrawSubmission.Submit( m_profilerDrawList, textBatch, m_gpuTiming, renderTextures, renderCommands,
                                   renderDiagnostics, viewport.screenW, viewport.screenH );
    }
#endif
}


void UiTextPass::RenderReplay( const ReplayOverlay::ReplayOverlayStateView& overlay, Core::Profiler* profiler,
                               bool legacySurfaceActive, bool scenePhysicsEnabled, RuntimeInteractionGestureKind gesture,
                               const UiTextViewport& viewport, double nowSeconds,
                               Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderCommands,
                               Rendering::Dx12Diagnostics& renderDiagnostics )
{

    if ( !legacySurfaceActive )
    {
        return;
    }

    ReplayOverlay::RenderReplayScrubberOverlay( m_uiDrawSubmission, m_textBatch, m_replayDrawList, overlay, renderTextures,
                                                renderCommands, renderDiagnostics, profiler, scenePhysicsEnabled, gesture,
                                                { viewport.screenW, viewport.screenH }, nowSeconds );
}


void UiTextPass::FinalizeOverlay( OverlayMode mode, Rendering::Dx12TextureOwner& renderTextures,
                                  Rendering::Dx12GeometryOwner& renderCommands,
                                  Rendering::Dx12Diagnostics& renderDiagnostics )
{

    if ( mode == OverlayMode::BarsNormalized || mode == OverlayMode::BarsAbsolute )
    {
        return;
    }

    const char* traceLabel = "ProfilerOverlay";

    if ( mode == OverlayMode::None )
    {
        traceLabel = "HUD";
    }
    else if ( mode == OverlayMode::SceneStats )
    {
        traceLabel = "SceneStats";
    }
    else if ( mode == OverlayMode::Keys )
    {
        traceLabel = "Keys";
    }

    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, traceLabel );
        Text2d::FlushText( m_textBatch, renderTextures, renderCommands );
    }
}


void UiTextPass::ReportRetainedDrawStats()
{

    // Lifetime: reporting runs after the complete UI graph, so every retained
    // stream reflects the same final command/text counts as the former scope.
    const RetainedUIDrawStatsScope retainedDrawStats( m_testPatternDrawList, m_badgeDrawList, m_replayDrawList,
                                                      m_profilerDrawList );
}
