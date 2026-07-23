/*
File: SkullbonezSource/Runtime/UiTextPass.cpp
Purpose:
  Implements the cohesive UI/Text render pass owner.

Summary:
  World rendering can be skipped, redirected, or post-processed, but UI/text is
  a late pass over the final window. This owner holds font/text-batch lifetime,
  profiler/timing and ray-tracing presentation capabilities, text-only output,
  HUD overlays, and the in-game UI draw payload. RuntimeRenderer schedules it
  through one frame input record.

Glossary:
  HUD (Heads-Up Display): Lightweight text diagnostics drawn over the scene.
  Runtime mode badge: Compact top-right label that names the current camera/input
    workspace, such as Inspect or Manipulator.
  Scene pause badge: Compact top-right scene-flow indicator for frame progress,
    completion, and the cross-scene pause lock.
  Text-only mode: Validation mode that skips world rendering and renders glyphs
    on a solid background to isolate text output.
  UI frame data: Borrowed per-frame snapshot passed to the immediate-mode UI.
  Shared editor view: Domain-grouped values copied once and consumed by both
    operator front ends during the same presentation frame.
  Profiler connection snapshot: Three fixed booleans copied from the Tracy
    owner without a process scan, socket probe, string construction, or growth.

Invariants:
  - Font resources are created once through EnsureGpuResources and released
    before backend teardown.
  - Render flushes Text2d before returning, so callers do not inherit queued UI
    glyphs into later frame work.
  - Development-tool status is copied into the UI snapshot; draw code never
    reaches back into the live Tracy owner.
  - UiTextPassInputs is borrowed only during the synchronous graph callback;
    no replay, diagnostics, model, or operator-UI reference is retained.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "Render/RuntimeRenderPasses.h"
#include "Render/RuntimeRenderFrameValues.h"
#include "InputController.h"
#include "CameraControlState.h"
#include "OverlayDebugState.h"
#include "RuntimeFrameViews.h"
#include "RuntimeViewModel.h"
#include "RunTimerState.h"
#include "Scene/SceneControllerState.h"
#include "Scene/SceneRuntime.h"
#include "Scene/SceneWorld.h"
#include "Tools/RuntimeTools.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Core/TracyClientOwner.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/DiagnosticsPhysicsUI.h"
#include "Replay/ReplayOverlayRenderer.h"
#include "../Core/WorkerPool.h"
#include "../Physics/PhysicsDebugData.h"
#include "../Core/Profiler.h"
#include "../Rendering/ProfilerOverlayPresenter.h"
#include "../Rendering/DX12/Dx12Diagnostics.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "../Rendering/Text.h"
#include "../UI/UI.h"
#include "../UI/UIDraw.h"
#include "../UI/UIStyle.h"

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Text::Text2d;
using SkullbonezCore::UI::InGameUIFrameData;
using SkullbonezCore::UI::InGameUITab;

namespace
{
void DrawUiTestPattern( SkullbonezCore::Text::TextBatch& textBatch,
                        SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures,
                        SkullbonezCore::Rendering::Dx12GeometryOwner& renderCommands,
                        int screenW,
                        int screenH )
{
    const SkullbonezCore::UI::UIDrawContext draw( screenW,
                                                  screenH,
                                                  nullptr,
                                                  &renderTextures,
                                                  &renderCommands,
                                                  &textBatch );
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
    Text2d::FlushQuads( textBatch, renderCommands );
}


SkullbonezCore::Core::MainMemoryStats
BuildMainMemoryOverlayStats( const DiagnosticsRuntime& diagnosticsRuntime,
                             const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects )
{
    // Concept: F6 is an allocator-growth overlay, not a memory profiler sample.
    // It can show the last cached replay totals and current model-store capacity,
    // but process reconciliation belongs to explicit diagnostics refreshes.
    SkullbonezCore::Core::MainMemoryStats stats = diagnosticsRuntime.MainMemoryStatsSnapshot();
    stats.process = SkullbonezCore::Core::MainMemoryProcessStats{};
    stats.gameObjects = gameObjects;
    stats.trackedEngineBytes = stats.replay.totalBytes + stats.gameObjects.totalBytes + stats.otherTrackedBytes;
    stats.unattributedProcessBytes = 0;
    stats.trackedOvershootBytes = 0;
    stats.reconciledTotalBytes = stats.trackedEngineBytes;
    stats.reconciliationDeltaBytes = 0;
    return stats;
}

void RenderReplayScrubberOverlayFromInputs( SkullbonezCore::Text::TextBatch& textBatch, const UiTextPassInputs& inputs )
{
    ReplayOverlay::RenderReplayScrubberOverlay( textBatch, inputs.replayOverlayContext );
}

void RenderReplayDivergenceCounter( SkullbonezCore::Text::TextBatch& textBatch, const UiTextPassInputs& inputs )
{
    if ( !inputs.state.debug.isTopTextHidden || !inputs.replayHud.divergenceValid )
    {
        return;
    }

    const int divergence = (std::max)( 0, static_cast<int>( inputs.replayHud.divergenceUnits + 0.5f ) );
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
                                                               const Assets::AssetSystem& assets,
                                                               int screenW,
                                                               int screenH )
{
    return Text2d::BuildFont( m_textBatch,
                              renderResources,
                              renderTextures,
                              renderGeometry,
                              assets,
                              screenW,
                              screenH,
                              "Verdana" );
}


void UiTextPass::ReleaseGpuResources( Rendering::Dx12TextureOwner* renderTextures,
                                      Rendering::Dx12GeometryOwner* renderGeometry )
{
    Text2d::DeleteFont( m_textBatch, renderTextures, renderGeometry );
    m_renderRayTracing = nullptr;
}


bool UiTextPass::ShouldRender( const UiTextPassState& state, const UI::InGameUI& ui ) const
{
    return state.debug.isTextOnly || !state.scene.isSceneMode || state.scene.isSceneText ||
           state.debug.overlayMode != OverlayMode::None || ui.NeedsUiTextPass() ||
           ( state.crossScenePauseLocked && !state.debug.isTopTextHidden ) ||
           ( state.scene.isTestComplete && !state.debug.isTopTextHidden ) || state.replayScrubberVisible ||
           state.replayPathVisualizerHasTarget ||
           ( state.camera.mode != RunCameraMode::Demo && state.camera.mode != RunCameraMode::Scene &&
             state.camera.mode != RunCameraMode::Director );
}


void UiTextPass::SetRayTracingCapability( Rendering::Dx12RaytracingOwner* renderRayTracing )
{
    m_renderRayTracing = renderRayTracing;
}


void UiTextPass::Render( const UiTextPassInputs& inputs )
{
    const UiTextPassState& state = inputs.state;
    Text::TextBatch& textBatch = m_textBatch;
    Text2d::RebuildProjection( textBatch, (std::max)( 1, state.screenW ), (std::max)( 1, state.screenH ) );
    const int uiPassDrawCallStart = inputs.renderDiagnostics.GetFrameDrawCallCount();
    assert( inputs.uiRender.IsReady() );
    Rendering::Dx12TextureOwner& renderTextures = *inputs.uiRender.textures;
    Rendering::Dx12GeometryOwner& renderCommands = *inputs.uiRender.geometry;
    UI::UIRenderContext uiRender = inputs.uiRender;
    uiRender.gpuTiming = m_gpuTiming;
    uiRender.textBatch = &textBatch;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    assert( m_profiler && "UiTextPass requires a startup-bound profiler in profile builds." );
    const SkullbonezCore::Core::Profiler& profiler = *m_profiler;
    const Rendering::ProfilerOverlayPresenter profilerOverlay;
#endif

    // Invariant: rolling diagnostics update before any overlay early return so
    // FPS, physics time, render time, and scene energy age at the same cadence.
    inputs.timers.updateTimer.StopTimer();
    inputs.timers.timeSinceLastRender += static_cast<float>( inputs.timers.updateTimer.GetElapsedTime() );
    inputs.timers.updateTimer.StartTimer();

    const double currentSceneEnergy = inputs.models.sceneKineticEnergy;
    inputs.timers.sceneEnergyAccumulator += currentSceneEnergy;
    ++inputs.timers.sceneEnergySampleCount;

    if ( inputs.timers.timeSinceLastRender > 0.5f )
    {
        if ( inputs.secondsPerFrame )
        {
            inputs.timers.rollingFpsTime = 1.0f / static_cast<float>( inputs.secondsPerFrame );
            inputs.timers.rollingPhysicsTime = inputs.timers.physicsTime;
            inputs.timers.rollingRenderTime = inputs.timers.renderTime;
        }
        if ( inputs.timers.sceneEnergySampleCount > 0 )
        {
            inputs.timers.rollingSceneEnergy = static_cast<float>(
                inputs.timers.sceneEnergyAccumulator / static_cast<double>( inputs.timers.sceneEnergySampleCount ) );
            inputs.timers.sceneEnergyAccumulator = 0.0;
            inputs.timers.sceneEnergySampleCount = 0;
        }
        inputs.timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = inputs.timers.rollingSceneEnergy;
    if ( inputs.timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( inputs.timers.sceneEnergyAccumulator /
                                                    static_cast<double>( inputs.timers.sceneEnergySampleCount ) );
    }

    const char* rendererName = inputs.renderDiagnostics.GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler
    if ( state.debug.isTextOnly )
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
        Text2d::Render2dTextColor( textBatch,
                                   -0.46f,
                                   -0.38f,
                                   0.015f,
                                   0.60f,
                                   0.60f,
                                   0.60f,
                                   "renderer: %s",
                                   rendererName );

        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "TextOnly" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        return;
    }

    const float hw = Text2d::HalfW( textBatch );
    const float hh = Text2d::HalfH( textBatch );
    const float mX = 0.022f; // horizontal inset from left/right edge
    const float mY = 0.015f; // vertical inset from top/bottom edge
    constexpr float TOP_RIGHT_BADGE_MARGIN = 12.0f;
    constexpr float TOP_RIGHT_BADGE_GAP = 6.0f;
    float topRightBadgeY = TOP_RIGHT_BADGE_MARGIN;

    const auto renderScenePauseBadge = [&]()
    {
        if ( state.debug.isTopTextHidden ||
             ( !state.scene.isSceneMode && !state.crossScenePauseLocked && !state.scene.isTestComplete ) )
        {
            return;
        }

        const int screenW = (std::max)( 1, state.screenW );
        const int screenH = (std::max)( 1, state.screenH );
        const SkullbonezCore::UI::UIDrawContext draw( screenW,
                                                      screenH,
                                                      nullptr,
                                                      &renderTextures,
                                                      &renderCommands,
                                                      &textBatch );
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
        const SkullbonezCore::UI::Style::UIRadii& radii = SkullbonezCore::UI::Style::Radii();

        char sceneLine[64] = {};
        if ( !state.scene.isSceneMode )
        {
            sprintf_s( sceneLine, sizeof( sceneLine ), "Demo  Frame %d", state.scene.currentFrame );
        }
        else if ( state.scene.targetFrameCount > 0 )
        {
            const int frame = state.scene.isTestComplete && state.scene.currentFrame > state.scene.targetFrameCount
                                  ? state.scene.targetFrameCount
                                  : state.scene.currentFrame;
            sprintf_s( sceneLine,
                       sizeof( sceneLine ),
                       "Scene %d/%d  Frame %d/%d",
                       state.scene.currentSceneIndex + 1,
                       state.sceneQueueSize,
                       frame,
                       state.scene.targetFrameCount );
        }
        else
        {
            sprintf_s( sceneLine,
                       sizeof( sceneLine ),
                       "Scene %d/%d  Frame %d",
                       state.scene.currentSceneIndex + 1,
                       state.sceneQueueSize,
                       state.scene.currentFrame );
        }

        const char* stateLine = state.crossScenePauseLocked
                                    ? "P Pause Lock   Space advances"
                                    : ( state.scene.isTestComplete ? "Scene complete" : "P pause lock" );
        const float titlePx = 11.5f;
        const float valuePx = 10.0f;
        const float padX = 10.0f;
        const float padY = 8.0f;
        const float lineGap = 15.0f;
        const float contentW =
            (std::max)( Text2d::MeasureText( titlePx, sceneLine ), Text2d::MeasureText( valuePx, stateLine ) );
        const float availableW = (std::max)( 80.0f, static_cast<float>( screenW ) - 24.0f );
        const float panelW = (std::min)( availableW, contentW + padX * 2.0f + 4.0f );
        const float panelH = 38.0f;
        const float x = static_cast<float>( screenW ) - TOP_RIGHT_BADGE_MARGIN - panelW;
        const float y = topRightBadgeY;

        SkullbonezCore::UI::Style::UIColor fill = palette.windowSubtle;
        fill.a = 0.88f;
        draw.RoundedPanel( { x, y, panelW, panelH }, radii.control, fill, palette.innerBorder );
        draw.RoundedRect( x + 1.0f,
                          y + 1.0f,
                          4.0f,
                          panelH - 2.0f,
                          radii.smallButton,
                          state.crossScenePauseLocked ? palette.warningAccent.r : palette.accent.r,
                          state.crossScenePauseLocked ? palette.warningAccent.g : palette.accent.g,
                          state.crossScenePauseLocked ? palette.warningAccent.b : palette.accent.b,
                          0.90f );
        Text2d::FlushQuads( textBatch, renderCommands );
        draw.Text( x + padX,
                   y + padY,
                   titlePx,
                   palette.textPrimary.r,
                   palette.textPrimary.g,
                   palette.textPrimary.b,
                   sceneLine );
        draw.Text( x + padX,
                   y + padY + lineGap,
                   valuePx,
                   state.crossScenePauseLocked ? palette.warningAccent.r : palette.accent.r,
                   state.crossScenePauseLocked ? palette.warningAccent.g : palette.accent.g,
                   state.crossScenePauseLocked ? palette.warningAccent.b : palette.accent.b,
                   stateLine );
        topRightBadgeY = y + panelH + TOP_RIGHT_BADGE_GAP;
    };

    const auto renderRuntimeModeBadge = [&]()
    {
        // Why: clean validation/look-dev captures use --hide-top-text to remove
        // top-left chrome without changing scene simulation or camera state.
        if ( state.debug.isTopTextHidden )
        {
            return;
        }
        if ( state.camera.mode == RunCameraMode::Demo || state.camera.mode == RunCameraMode::Scene ||
             state.camera.mode == RunCameraMode::Director )
        {
            return;
        }

        const int screenW = (std::max)( 1, state.screenW );
        const int screenH = (std::max)( 1, state.screenH );
        const SkullbonezCore::UI::UIDrawContext draw( screenW,
                                                      screenH,
                                                      nullptr,
                                                      &renderTextures,
                                                      &renderCommands,
                                                      &textBatch );
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
        const SkullbonezCore::UI::Style::UIRadii& radii = SkullbonezCore::UI::Style::Radii();

        const char* modeLine = state.cameraModeLabel;

        const char* detail = "RMB look  WASD  Space";
        SkullbonezCore::UI::Style::UIColor accent = palette.accent;
        if ( state.camera.mode == RunCameraMode::Attach )
        {
            detail = "LMB target  RMB orbit  F1  Enter";
            accent = palette.accentStrong;
        }
        else if ( state.camera.mode == RunCameraMode::Manipulator )
        {
            detail = "LMB drag  Space";
            accent = palette.accentStrong;
        }
        else if ( state.camera.mode == RunCameraMode::Launcher )
        {
            detail = "LMB fire  M mode";
        }
        else if ( state.camera.mode == RunCameraMode::Inspect )
        {
            detail = "RMB look  WASD  Space";
        }

        const float titlePx = 11.5f;
        const float detailPx = 9.5f;
        const float padX = 9.0f;
        const float padY = 7.0f;
        const float lineGap = 14.0f;
        const float textW =
            (std::max)( Text2d::MeasureText( titlePx, modeLine ), Text2d::MeasureText( detailPx, detail ) );
        const float availableW = (std::max)( 80.0f, static_cast<float>( screenW ) - 24.0f );
        const float panelW = (std::min)( availableW, textW + padX * 2.0f + 4.0f );
        const float panelH = 34.0f;
        const float x = static_cast<float>( screenW ) - TOP_RIGHT_BADGE_MARGIN - panelW;
        const float y = topRightBadgeY;

        SkullbonezCore::UI::Style::UIColor fill = palette.windowSubtle;
        fill.a = 0.86f;
        draw.RoundedPanel( { x, y, panelW, panelH }, radii.control, fill, palette.innerBorder );
        draw.RoundedRect( x + 1.0f,
                          y + 1.0f,
                          4.0f,
                          panelH - 2.0f,
                          radii.smallButton,
                          accent.r,
                          accent.g,
                          accent.b,
                          0.88f );
        Text2d::FlushQuads( textBatch, renderCommands );
        draw.Text( x + padX, y + padY, titlePx, accent.r, accent.g, accent.b, modeLine );
        draw.Text( x + padX,
                   y + padY + lineGap,
                   detailPx,
                   palette.textSecondary.r,
                   palette.textSecondary.g,
                   palette.textSecondary.b,
                   detail );
    };

    renderScenePauseBadge();
    renderRuntimeModeBadge();
    RenderReplayDivergenceCounter( textBatch, inputs );

    // Crosshair - always visible when launcher mode is active, regardless of overlay state.
    // A tiny center gap keeps the target visible instead of covering it.
    if ( state.launcherCameraMode )
    {
        const float cArm = 0.020f;
        const float cGap = 0.004f;
        const float cHalf = 0.00045f;
        const float cShadowHalf = 0.00080f;
        Text2d::Render2dQuad( textBatch,
                              renderCommands,
                              -cArm,
                              -cShadowHalf,
                              -cGap,
                              cShadowHalf,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.40f );
        Text2d::Render2dQuad( textBatch,
                              renderCommands,
                              cGap,
                              -cShadowHalf,
                              cArm,
                              cShadowHalf,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.40f );
        Text2d::Render2dQuad( textBatch,
                              renderCommands,
                              -cShadowHalf,
                              -cArm,
                              cShadowHalf,
                              -cGap,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.40f );
        Text2d::Render2dQuad( textBatch,
                              renderCommands,
                              -cShadowHalf,
                              cGap,
                              cShadowHalf,
                              cArm,
                              0.0f,
                              0.0f,
                              0.0f,
                              0.40f );
        Text2d::Render2dQuad( textBatch, renderCommands, -cArm, -cHalf, -cGap, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( textBatch, renderCommands, cGap, -cHalf, cArm, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( textBatch, renderCommands, -cHalf, -cArm, cHalf, -cGap, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( textBatch, renderCommands, -cHalf, cGap, cHalf, cArm, 0.80f, 0.96f, 1.0f, 0.88f );
        const char* fireModeLabel = state.launcherFireModeLabel;
        const float modeSz = 0.011f;
        const float modeW = Text2d::MeasureText( modeSz, fireModeLabel );
        Text2d::Render2dTextColor( textBatch, -modeW * 0.5f, -0.048f, modeSz, 0.72f, 0.94f, 1.0f, "%s", fireModeLabel );
#ifdef _DEBUG
        if ( state.debug.reproSnapshotMessage[0] != '\0' &&
             inputs.timers.simulationTimer.GetTimeSinceLastStart() <= state.debug.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, state.debug.reproSnapshotMessage );
            Text2d::Render2dTextColor( textBatch,
                                       -msgW * 0.5f,
                                       -0.065f,
                                       msgSz,
                                       0.65f,
                                       0.92f,
                                       1.0f,
                                       "%s",
                                       state.debug.reproSnapshotMessage );
        }
#endif
    }

    const RuntimeViewModel& view = state.runtimeViewModel;

    const char* sceneName = "";
    if ( view.sceneMode && state.sceneHasCurrentEntry && state.currentScenePath )
    {
        sceneName = SceneFileNameFromPath( state.currentScenePath );
    }

    if ( inputs.ui.NeedsUiTextPass() )
    {
        PROFILE_BEGIN( m_profiler, "Frame/UI/BuildData" );
        InGameUIFrameData UIData;
        UIData.screenW = state.screenW;
        UIData.screenH = state.screenH;
        if ( state.debug.isUITestPattern )
        {
            DrawUiTestPattern( textBatch, renderTextures, renderCommands, UIData.screenW, UIData.screenH );
        }
        UIData.rendererName = rendererName;
        UIData.sceneName = sceneName;
        UIData.sceneOptions = state.sceneBrowser.namePtrs.empty() ? nullptr : state.sceneBrowser.namePtrs.data();
        UIData.sceneOptionCount = static_cast<int>( state.sceneBrowser.namePtrs.size() );
        UIData.selectedSceneOption = state.currentSceneBrowserIndex;
        UIData.selectedCineModeSceneOption = state.sceneBrowser.selectedCineModeSceneIndex;
        UIData.UIDrawCalls = inputs.timers.lastUIDrawCalls;
        UIData.visibility = inputs.renderDiagnostics.GetFrameVisibilityStats();
        UIData.fps =
            inputs.timers.rollingFpsTime > 0.0f
                ? inputs.timers.rollingFpsTime
                : ( inputs.secondsPerFrame > 0.0 ? 1.0f / static_cast<float>( inputs.secondsPerFrame ) : 0.0f );
        UIData.renderMs =
            ( inputs.timers.rollingRenderTime > 0.0f ? inputs.timers.rollingRenderTime : inputs.timers.renderTime ) *
            1000.0f;
        UIData.physicsMs =
            ( inputs.timers.rollingPhysicsTime > 0.0f ? inputs.timers.rollingPhysicsTime : inputs.timers.physicsTime ) *
            1000.0f;
        UIData.cpuFrameMs = inputs.timers.cpuFrameWorkMs;
        UIData.gpuFrameMs = inputs.timers.gpuFrameWorkMs;
        {
            // Concept: render draw attribution is copied through UIData while
            // the render diagnostics capability is already borrowed by Run. The
            // profiler tab never needs the wide renderer facade to explain draw
            // calls.
            const auto drawTrace = inputs.renderDiagnostics.GetFrameDrawCallTrace();
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
            profilerFrame.markerCount =
                (std::min)( profiler.MarkerCount(), SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
            for ( int markerIndex = 0; markerIndex < profilerFrame.markerCount; ++markerIndex )
            {
                const SkullbonezCore::Core::Profiler::Marker& source = profiler.GetMarker( markerIndex );
                const int paletteIndex =
                    source.colorIndex >= 0 ? source.colorIndex % SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE : 0;
                const SkullbonezCore::Core::Profiler::BarColor& color =
                    SkullbonezCore::Core::Profiler::BAR_PALETTE[paletteIndex];
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
                target.p50Ms = source.p50Ms;
                target.p99Ms = source.p99Ms;
                target.colorR = color.r;
                target.colorG = color.g;
                target.colorB = color.b;
            }

            profilerFrame.workerCoreSampleCount =
                (std::min)( profiler.WorkerCoreSampleCount(),
                            SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );
            for ( int sampleIndex = 0; sampleIndex < profilerFrame.workerCoreSampleCount; ++sampleIndex )
            {
                const SkullbonezCore::Core::Profiler::WorkerCoreSample& source =
                    profiler.GetWorkerCoreSample( sampleIndex );
                SkullbonezCore::UI::ProfilerTab::WorkerCoreSampleSnapshot& target =
                    profilerFrame.workerCoreSamples[sampleIndex];
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
            const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus tracyStatus =
                SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();
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

                SkullbonezCore::UI::UIProfilerMarkerOption& option =
                    UIData.profilerMarkerOptions[UIData.profilerMarkerOptionCount++];
                option = input;
                option.name = input.name ? input.name : "";
                option.leafName = input.leafName ? input.leafName : option.name;
                option.cpuMs = (std::max)( 0.0f, input.cpuMs );
                option.cpuAverageMs = (std::max)( 0.0f, input.cpuAverageMs );
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
            addMarkerOption(
                SkullbonezCore::UI::UIProfilerMarkerOption{ .name = "Frame Total",
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
                const SkullbonezCore::Core::Profiler::BarColor& color =
                    SkullbonezCore::Core::Profiler::BAR_PALETTE[marker.colorIndex %
                                                                SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE];
                addMarkerOption( SkullbonezCore::UI::UIProfilerMarkerOption{
                    .name = marker.name,
                    .leafName = marker.leafName,
                    .hash = marker.hash,
                    .cpuMs = marker.lastFrameMs,
                    .cpuAverageMs = marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs,
                    .gpuMs = marker.hasGpu ? marker.gpuLastFrameMs : 0.0f,
                    .colorR = color.r,
                    .colorG = color.g,
                    .colorB = color.b,
                    .hasGpu = marker.hasGpu,
                    .sampleValid = true,
                    .isFrameTotal = false } );
            };

            static constexpr uint32_t kPinnedMarkerHashes[] = {
                ::HashStr( "Frame/Physics" ),
                ::HashStr( "Frame/Physics/Step" ),
                ::HashStr( "Frame/Physics/Narrowphase/PersistentContacts/"
                           "SolveRows" ),
                ::HashStr( "Frame/Render" ),
                ::HashStr( "Frame/UI" ) };
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
        UIData.modelCount = view.modelCount;
        UIData.modelCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( state.config );
        UIData.workerThreadCount = state.workerPool ? state.workerPool->GetThreadCount() : 0;
        UIData.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        UIData.currentFrame = view.frame;
        UIData.targetFrameCount = view.targetFrameCount;
        UIData.rngSeed = state.scene.rngSeed;
        UIData.solverBallCount = state.scene.solverBallCount;
        UIData.solverBoxCount = state.scene.solverBoxCount;
        UIData.currentSceneIndex = view.sceneIndex;
        UIData.sceneCount = view.sceneCount;
        UIData.now = inputs.timers.simulationTimer.GetTotalTime();
        UIData.replayMemoryPreset = inputs.replayHud.memoryPreset;
        UIData.replayMemoryRequestedRetentionSeconds = inputs.replayHud.requestedRetentionSeconds;
        UIData.replayMemoryRequestedBudgetMiB = inputs.replayHud.requestedBudgetMiB;
        UIData.replayMemoryPresentationRetentionSeconds = inputs.replayHud.presentationRetentionSeconds;
        UIData.replayMemorySolverRetentionSeconds = inputs.replayHud.solverRetentionSeconds;
        UIData.replayMemoryBudgetClamped = inputs.replayHud.memoryBudgetClamped;
        UIData.replayMemorySolverWindowReduced = inputs.replayHud.solverWindowReduced;
        const bool memoryTabActive =
            inputs.ui.IsVisible() && !inputs.ui.IsMinimized() && inputs.ui.GetActiveTab() == InGameUITab::Memory;
        const bool memoryOverlayEnabled = inputs.ui.IsMemoryOverlayEnabled();
        if ( memoryTabActive )
        {
            // Why: memory sampling belongs to DiagnosticsRuntime; the render host
            // only decides whether the UI pass needs to draw.
            assert( inputs.replayHud.memoryStatsValid );
            UIData.mainMemory = inputs.diagnosticsRuntime.RefreshMainMemoryStats( inputs.replayHud.memoryStats,
                                                                                  inputs.models.gameObjectMemory,
                                                                                  UIData.now,
                                                                                  false,
                                                                                  false );
        }
        else if ( memoryOverlayEnabled )
        {
            // Why: F6 stays event/counter driven. Merely leaving the overlay up
            // must not start a process-memory or replay-memory sampling heartbeat.
            UIData.mainMemory =
                BuildMainMemoryOverlayStats( inputs.diagnosticsRuntime, inputs.models.gameObjectMemory );
        }
        if ( memoryTabActive || memoryOverlayEnabled )
        {
            // The render snapshot is cheap owner-maintained accounting; unlike
            // process memory sampling, it is safe to refresh for the F6 overlay.
            UIData.renderMemory = inputs.renderDiagnostics.GetRenderMemoryStats();
            UIData.reserveGrowthEventTotalCount =
                SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::GrowthEventCount();
            UIData.reserveGrowthEventDroppedCount =
                SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::GrowthEventDroppedCount();
            UIData.reserveGrowthEventCount =
                SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CopyRecentGrowthEvents(
                    UIData.reserveGrowthEvents,
                    SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );
        }
        UIData.sceneMode = view.sceneMode;
        UIData.scenePhysicsEnabled = view.scenePhysics;
        UIData.sceneTextEnabled = view.sceneText;
        UIData.textOnly = state.debug.isTextOnly;
        UIData.fixedStep = view.fixedStep;
        UIData.exitOnComplete = state.scene.isExitOnComplete;
        UIData.testComplete = state.scene.isTestComplete;
        UIData.vsyncEnabled = state.renderPresentation.vsyncEnabled;
        UIData.pipelineSyncEnabled = state.renderPresentation.pipelineSyncEnabled;
        UIData.sceneEnergy = sceneEnergyForDisplay;
        UIData.timeScale = view.timeScale;
        UIData.presentationInterpolation = view.presentationInterpolation;
        UIData.presentationPinned = view.presentationPinned;
        UIData.presentationAlpha = view.presentationAlpha;
        UIData.trackHeight = state.camera.trackBallRow.IsValid() ? state.camera.trackHeight : 0.0f;
        UIData.autoCycleInterval = state.camera.autoCycleInterval > 0.0f ? state.camera.autoCycleInterval : 0.0f;
        UIData.worldGravity = state.world.Environment().GetGravity();
        UIData.worldFluidHeight = state.world.Environment().GetFluidSurfaceHeight();
        UIData.worldFluidDensity = state.world.Environment().GetFluidDensity();
        UIData.physicsDebug = BuildDiagnosticsPhysicsUIStatus( state.debug );
        UIData.physicsSleepEnabled = state.world.Physics().IsSleepEnabled();
        const Gameplay::TornadoFieldConfig& tornadoField = state.world.Tornado().GetFieldConfig();
        UIData.tornadoEnabled = tornadoField.enabled;
        UIData.tornadoVisualShell = state.world.Tornado().VisualSettings().enabled && tornadoField.enabled;
        UIData.tornadoFieldVectors = tornadoField.visualizeVelocityField;
        UIData.tornadoRadius = tornadoField.radius;
        UIData.tornadoHeight = tornadoField.height;
        UIData.tornadoInwardAcceleration = tornadoField.inwardAcceleration;
        UIData.tornadoSwirlAcceleration = tornadoField.swirlAcceleration;
        UIData.tornadoLiftAcceleration = tornadoField.liftAcceleration;
        const SkullbonezCore::Core::EngineConfig& liveConfig = state.config;
        UIData.rayCastVisualization = state.rayCastTest.visualizeRays;
        UIData.rayCastImpulseStrength = state.rayCastTest.impulseStrength;
        UIData.launcherProjectileSpeed = state.rayCastTest.projectileSpeed;
        UIData.terrainFrictionCoeff = liveConfig.physicsMaterial.frictionCoeff;
        UIData.objectFrictionCoeff = liveConfig.physicsMaterial.objectFrictionCoeff;
        UIData.rollingFrictionCoeff = liveConfig.physicsMaterial.rollingFrictionCoeff;
        UIData.waterFreezeDebug = state.debug.isWaterFreezeDebug;
        UIData.waterFlatDebug = state.debug.isWaterFlatDebug;
        UIData.terrainHidden = state.debug.isTerrainHidden;
        UIData.waterHidden = state.debug.isWaterHidden;
        UIData.waterNoReflect = state.debug.isWaterNoReflect;
        UIData.waterRTReflect = state.debug.isWaterRTReflect;
        const RuntimeInputMode runtimeInputMode = state.runtimeInput.CurrentMode();
        UIData.cameraModeIndex = static_cast<int>( state.camera.mode );
        UIData.cameraModeEnabledMask = state.cameraModeEnabledMask;
        UIData.runtimeInputModeLabel = state.cameraModeLabel;
        UIData.cameraMouseActive =
            ( runtimeInputMode == RuntimeInputMode::FlyCamera || runtimeInputMode == RuntimeInputMode::Launcher ||
              runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
            !inputs.ui.BlocksCameraMouse();
        UIData.nativeCursorVisible = !UIData.cameraMouseActive;
        UIData.editorModeEnabled = state.editor.editorModeEnabled;
        UIData.editorPlacementMode = state.editor.placementModeEnabled;
        UIData.editorPlaceStatic = state.editor.placeStaticObject;
        UIData.editorTerrainAlign = state.editor.autoTerrainAlign;
        UIData.editorViewportLookActive = state.editor.viewportLookActive;
        UIData.editorObjectType = state.editor.objectType;
        UIData.editorUndoDepth = static_cast<int>( state.editor.history.UndoDepth() );
        UIData.editorRedoDepth = static_cast<int>( state.editor.history.RedoDepth() );
        UIData.canSaveSceneDefaults =
            view.sceneMode && state.sceneHasCurrentEntry && state.currentScenePath && state.currentScenePath[0] != '\0';
        UIData.cinematicRendering = inputs.cinematicRendering;
        UIData.ordinaryRender = liveConfig.ordinaryRender;
        UIData.cinematic = inputs.cinematic;
        // Invariant: representative legacy controls display the same immutable
        // values supplied to the secondary editor for this frame.
        UIData.operatorEditor = state.operatorEditorView;
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
        {
            auto addPreview = [&]( const char* label,
                                   uint32_t textureHandle,
                                   int width,
                                   int height,
                                   bool available,
                                   bool depth,
                                   bool hdr )
            {
                if ( UIData.renderTargetPreviewCount >= SkullbonezCore::UI::UI_RENDER_TARGET_PREVIEW_MAX )
                {
                    return;
                }

                SkullbonezCore::UI::UIRenderTargetPreviewResource& preview =
                    UIData.renderTargetPreviews[UIData.renderTargetPreviewCount++];
                preview.label = label;
                preview.textureHandle = textureHandle;
                preview.width = width;
                preview.height = height;
                preview.available = available && textureHandle != 0 && width > 0 && height > 0;
                preview.depth = depth;
                preview.hdr = hdr;
            };

            for ( int index = 0; index < state.renderTargetPreviews.count; ++index )
            {
                const RuntimeRenderTargetPreview& source =
                    state.renderTargetPreviews.targets[static_cast<size_t>( index )];
                addPreview( source.label,
                            source.textureHandle,
                            source.width,
                            source.height,
                            source.available,
                            source.depth,
                            source.hdr );
            }

            const uint32_t dxrReflection = m_renderRayTracing ? m_renderRayTracing->GetReflectionUAVTexture() : 0;
            addPreview( "DXR Reflection",
                        dxrReflection,
                        state.screenW * 2,
                        state.screenH * 2,
                        UIData.waterRTReflect && !UIData.waterNoReflect,
                        false,
                        false );
        }
        PROFILE_END( m_profiler, "Frame/UI/BuildData" );

        PROFILE_BEGIN( m_profiler, "Frame/UI/PreFlushText" );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "PreFlushText" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        PROFILE_END( m_profiler, "Frame/UI/PreFlushText" );
        UIData.drawCallsBeforeUI = uiPassDrawCallStart;
        inputs.ui.Draw( UIData, uiRender );
        PROFILE_BEGIN( m_profiler, "Frame/UI/PostFlushText" );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/UI/PostFlushText" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        PROFILE_END( m_profiler, "Frame/UI/PostFlushText" );
        if ( inputs.ui.IsVisible() )
        {
            RenderReplayScrubberOverlayFromInputs( textBatch, inputs );
            return;
        }
    }

    // --- Overlay: None ---
    if ( state.debug.overlayMode == OverlayMode::None )
    {
        RenderReplayScrubberOverlayFromInputs( textBatch, inputs );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "HUD" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        return;
    }

    // --- Overlay: Scene telemetry ---
    if ( state.debug.overlayMode == OverlayMode::SceneStats )
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
        Text2d::Render2dTextColor( textBatch,
                                   panX0 + panPad,
                                   panY1 - panPad - titleSz,
                                   titleSz,
                                   1.0f,
                                   0.85f,
                                   0.35f,
                                   "SCENE TELEMETRY" );
        Text2d::Render2dTextColor( textBatch,
                                   panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Model Count: %d",
                                   state.scene.modelCount );
        Text2d::Render2dTextColor( textBatch,
                                   panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH * 2.0f,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Scene Energy: %.6f",
                                   sceneEnergyForDisplay );
        RenderReplayScrubberOverlayFromInputs( textBatch, inputs );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "SceneStats" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        return;
    }

    // --- Overlay: Visual profiler bars (normalized or absolute) ---
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( state.debug.overlayMode == OverlayMode::BarsNormalized ||
         state.debug.overlayMode == OverlayMode::BarsAbsolute )
    {
        // Panel anchored bottom-left, filling most of the width. Height kept modest - leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height
        const float panX = -( hw - mX ) + mX * 0.5f;   // slight left margin
        const float panY = -( hh - mY ) + mY * 0.5f;   // slight bottom margin
        const bool absolute = ( state.debug.overlayMode == OverlayMode::BarsAbsolute );
        profilerOverlay
            .RenderBarOverlay( profiler.FrameView(), textBatch, renderCommands, panX, panY, panW, panH, absolute );
        RenderReplayScrubberOverlayFromInputs( textBatch, inputs );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "ProfilerBars" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
        }
        return;
    }
#endif

    // --- Overlay: Keys reference screen (compact, bottom-left) ---
    if ( state.debug.overlayMode == OverlayMode::Keys )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.011f;
        const float lineH = 0.020f;
        const int nRows = 15;
        const float panPad = 0.012f;
        const float titleGap = 0.016f; // space between title baseline and first entry
        const float keyW = 0.058f;     // key-name column width
        const float descW = 0.120f;    // description column width
        const float colGap = 0.012f;   // gap between the two content columns

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
        Text2d::Render2dTextColor( textBatch,
                                   panX0 + panPad,
                                   titleY,
                                   titleSz,
                                   1.0f,
                                   0.85f,
                                   0.35f,
                                   "CONTROL REFERENCE" );

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
            { "Tab", "Camera mode" },
            { "N", "Launcher mode" },
            { "M", "Launcher fire mode" },
            { "F1", "Attach follow mode" },
            { "Enter", "Attach pin / repro" },
            { "F", "Fly mode" },
            { "WASD", "Move camera" },
            { "RMB", "Look" },
            { "Shift", "Sprint (3x speed)" },
            { "LMB", "Pick / drag / fire" },
            { "V", "Collision visual" },
            { "Space", "Play paused scene" },
            { "R/Bksp", "Reset scene" },
            { "F3", "Screenshot" },
            { "F5", "CPU histogram" },
        };
        static const KeyEntry kRight[nRows] = {
            { "Esc", "Min/expand UI" },
            { "Esc Esc", "Quit" },
            { "P", "Pause lock" },
            { "1", "Freeze water" },
            { "2", "Reflection mode" },
            { "3", "Toggle water flat" },
            { "4", "Toggle terrain" },
            { "5", "Toggle water" },
            { "6", "Debug body alpha" },
            { "G", "Broadphase overlay" },
            { "C", "Physics debug" },
            { "O", "Terrain probe" },
            { "PgUp/Dn", "Water height" },
            { "F7/F8", "Pipeline stage" },
            { "F6", "Memory waterline" },
        };

        for ( int i = 0; i < nRows; ++i )
        {
            float y = firstY - static_cast<float>( i ) * lineH;
            Text2d::Render2dTextColor( textBatch, col1Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kLeft[i].key );
            Text2d::Render2dTextColor( textBatch, col1Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kLeft[i].desc );
            Text2d::Render2dTextColor( textBatch, col2Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kRight[i].key );
            Text2d::Render2dTextColor( textBatch, col2Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kRight[i].desc );
        }

        RenderReplayScrubberOverlayFromInputs( textBatch, inputs );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Keys" );
            Text2d::FlushText( textBatch, renderTextures, renderCommands );
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
        profilerOverlay.RenderOverlay( profiler.FrameView(),
                                       textBatch,
                                       renderCommands,
                                       -( hw - mX ),
                                       -( hh - mY ) - padY,
                                       lineH,
                                       profFSz,
                                       inputs.timers.rollingFpsTime );
    }
#endif

    RenderReplayScrubberOverlayFromInputs( textBatch, inputs );
    {
        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "ProfilerOverlay" );
        Text2d::FlushText( textBatch, renderTextures, renderCommands );
    }
}
