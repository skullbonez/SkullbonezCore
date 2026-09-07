/*
Purpose:
  Implements the cohesive UI/Text render pass owner.

Invariants:
  - Font resources are created once through EnsureGpuResources and released
    before backend teardown.
  - Every scheduled UI path flushes Text2d before the graph completes, so later
    frame work cannot inherit queued UI glyphs.
  - Operation-specific graph ABI records borrow direct values only until their
    synchronous callback returns; no owner reference is retained between calls.
  - Backend preview handles stay in the Runtime snapshot, whose bounded append
    owns the optional DXR row before UI projection drops handle identity.
  - Profile-only reads require the startup-bound profiler epoch; resource
    release closes that epoch and a successful rebuild reopens it.
  - An invalid Replay memory sample publishes a cleared unavailable value and
    never reuses or refreshes stale accounting.
*/

#include "RuntimeRenderPasses.h"
#include "RuntimeRenderFrameValues.h"
#include "../../Core/Profiler.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/Text.h"
#include "../../UI/UIDraw.h"
#include "../../UI/UIDrawList.h"
#include "../../UI/UIDrawWidgets.h"
#include "../../UI/UIFontMetrics.h"
#include "UIProfilerOverlayPresenter.h"
#include "../../UI/UIStyle.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Text::Text2d;
namespace
{
class RetainedUIDrawStatsScope
{
  public:
    RetainedUIDrawStatsScope( const SkullbonezCore::UI::UIDrawList& testPattern, const SkullbonezCore::UI::UIDrawList& badge,
                              SkullbonezCore::UI::UIDrawList::Stats detached,
                              const SkullbonezCore::UI::UIDrawList& profiler )
        : m_testPattern( testPattern ), m_badge( badge ), m_detached( detached ), m_profiler( profiler )
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
        const SkullbonezCore::UI::UIDrawList::Stats detachedStats = m_detached;
        const SkullbonezCore::UI::UIDrawList::Stats profilerStats = m_profiler.GetStats();
        const auto overflowed = []( const SkullbonezCore::UI::UIDrawList::Stats& stats )
        { return stats.commandOverflow || stats.textOverflow || stats.clipOverflow; };

        const bool overflow = overflowed( testPatternStats ) || overflowed( badgeStats ) || overflowed( detachedStats ) ||
                              overflowed( profilerStats );

        std::fprintf( stderr,
                      "[ui-retained-draw-stats] test=%d/%d badge=%d/%d detached=%d/%d profiler=%d/%d clip=%d/%d/%d/%d "
                      "overflow=%d\n",
                      testPatternStats.commandCount, testPatternStats.textBytes, badgeStats.commandCount,
                      badgeStats.textBytes, detachedStats.commandCount, detachedStats.textBytes, profilerStats.commandCount,
                      profilerStats.textBytes, testPatternStats.maxClipDepth, badgeStats.maxClipDepth,
                      detachedStats.maxClipDepth, profilerStats.maxClipDepth, overflow ? 1 : 0 );
    }

  private:
    const SkullbonezCore::UI::UIDrawList& m_testPattern;
    const SkullbonezCore::UI::UIDrawList& m_badge;
    SkullbonezCore::UI::UIDrawList::Stats m_detached;
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


void RenderReplayDivergenceCounter( SkullbonezCore::Text::TextBatch& textBatch, const UiChromeTailValues& values )
{
    if ( !values.topTextHidden || !values.divergenceValid )
    {
        return;
    }

    const int divergence = (std::max)( 0, static_cast<int>( values.divergenceUnits + 0.5f ) );
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

SkullbonezCore::Core::SbResult UiTextPass::EnsureGpuResources( Rendering::Dx12TextureOwner& renderTextures,
                                                               Rendering::Dx12GeometryOwner& renderGeometry,
                                                               std::unique_ptr<Rendering::ShaderDX12> textShader,
                                                               std::unique_ptr<Rendering::ShaderDX12> solidShader,
                                                               std::unique_ptr<Rendering::ShaderDX12> solidBatchShader,
                                                               int screenW, int screenH )
{
    const SkullbonezCore::Core::SbResult fontResult = Text2d::BuildFont( m_resultDiagnostics, m_textBatch, renderTextures,
                                                                         renderGeometry, std::move( textShader ),
                                                                         std::move( solidShader ),
                                                                         std::move( solidBatchShader ), screenW, screenH,
                                                                         "Verdana" );

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

    m_profilerLifecycle.Activate();
    return SkullbonezCore::Core::SbResult::Success();
}


void UiTextPass::ReleaseGpuResources( Rendering::Dx12TextureOwner* renderTextures,
                                      Rendering::Dx12GeometryOwner* renderGeometry )
{
    m_profilerLifecycle.Close();
    m_uiDrawSubmission.ReleaseGpuResources( renderGeometry );
    Text2d::DeleteFont( m_textBatch, renderTextures, renderGeometry );
    m_dxrReflectionPreviewTexture = 0;
}


bool UiTextPass::ShouldRender( const UiTextVisibility& visibility ) const
{
    return visibility.textOnly || !visibility.sceneMode || visibility.sceneText || visibility.overlayVisible ||
           visibility.uiTextNeeded || ( visibility.crossScenePauseLocked && !visibility.topTextHidden ) ||
           ( visibility.sceneTestComplete && !visibility.topTextHidden ) || visibility.replayScrubberVisible ||
           visibility.replayPathVisualizerHasTarget || visibility.cameraBadgeVisible;
}


void UiTextPass::SetDxrReflectionPreviewTexture( uint32_t textureHandle )
{
    m_dxrReflectionPreviewTexture = textureHandle;
}


void UiTextPass::BeginFrame( int screenW, int screenH )
{
    m_testPatternDrawList.Clear();
    m_badgeDrawList.Clear();
    m_detachedDrawStats = {};
    m_profilerDrawList.Clear();
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    static_cast<void>( m_profilerLifecycle.Require( "BeginFrame" ) );
#endif
    Text2d::RebuildProjection( m_textBatch, (std::max)( 1, screenW ), (std::max)( 1, screenH ) );
}


void UiTextPass::RenderChromeStatus( const UiTextViewport& viewport, const UiChromeStatusValues& values,
                                     Rendering::Dx12TextureOwner& renderTextures,
                                     Rendering::Dx12GeometryOwner& renderCommands,
                                     Rendering::Dx12Diagnostics& renderDiagnostics )
{
    Text::TextBatch& textBatch = m_textBatch;
    const char* rendererName = renderDiagnostics.GetRendererName();
    static_cast<void>( rendererName );

    if ( values.textOnly )
    {
        Text2d::Render2dTextColor( textBatch, -0.95f, 0.90f, 0.04f, 1.0f, 1.0f, 1.0f,
                                   "The quick brown fox jumps over the lazy dog." );
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
        if ( values.topTextHidden || ( !values.sceneMode && !values.crossScenePauseLocked && !values.sceneTestComplete ) )
        {
            return;
        }

        const int screenW = (std::max)( 1, viewport.screenW );
        const SkullbonezCore::UI::UIDrawContext& draw = badgeDraw;
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
        const SkullbonezCore::UI::Style::UIRadii& radii = SkullbonezCore::UI::Style::Radii();

        char sceneLine[64] = {};

        if ( !values.sceneMode )
        {
            sprintf_s( sceneLine, sizeof( sceneLine ), "Demo  Frame %d", values.currentFrame );
        }
        else if ( values.targetFrameCount > 0 )
        {
            const int sceneFrame = values.sceneTestComplete && values.currentFrame > values.targetFrameCount
                                       ? values.targetFrameCount
                                       : values.currentFrame;

            sprintf_s( sceneLine, sizeof( sceneLine ), "Scene %d/%d  Frame %d/%d", values.currentSceneIndex + 1,
                       values.sceneQueueSize, sceneFrame, values.targetFrameCount );
        }
        else
        {
            sprintf_s( sceneLine, sizeof( sceneLine ), "Scene %d/%d  Frame %d", values.currentSceneIndex + 1,
                       values.sceneQueueSize, values.currentFrame );
        }

        const char* stateLine = values.crossScenePauseLocked
                                    ? "P Pause Lock   Space advances"
                                    : ( values.sceneTestComplete ? "Scene complete" : "Pause lock" );

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

        SkullbonezCore::UI::Widgets::DrawPanel( draw, { x, y, panelW, panelH },
                                                SkullbonezCore::UI::UIVisualState::Visible |
                                                    SkullbonezCore::UI::UIVisualState::Enabled,
                                                SkullbonezCore::UI::Widgets::ComponentAppearance::Compact, 0.88f );
        draw.RoundedRect( x + 1.0f, y + 1.0f, 4.0f, panelH - 2.0f, radii.smallButton,
                          values.crossScenePauseLocked ? palette.warningAccent.r : palette.accent.r,
                          values.crossScenePauseLocked ? palette.warningAccent.g : palette.accent.g,
                          values.crossScenePauseLocked ? palette.warningAccent.b : palette.accent.b, 0.90f );

        draw.Text( x + padX, y + padY, titlePx, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
                   sceneLine );

        draw.Text( x + padX, y + padY + lineGap, valuePx,
                   values.crossScenePauseLocked ? palette.warningAccent.r : palette.accent.r,
                   values.crossScenePauseLocked ? palette.warningAccent.g : palette.accent.g,
                   values.crossScenePauseLocked ? palette.warningAccent.b : palette.accent.b, stateLine );

        topRightBadgeY = y + panelH + TOP_RIGHT_BADGE_GAP;
    };

    const auto renderRuntimeModeBadge = [&]()
    {
        // Why: clean validation/look-dev captures use --hide-top-text to remove
        // top-left chrome without changing scene simulation or camera state.
        if ( values.topTextHidden )
        {
            return;
        }

        if ( values.cameraMode == UiCameraBadgeMode::Quiet )
        {
            return;
        }

        const int screenW = (std::max)( 1, viewport.screenW );
        const SkullbonezCore::UI::UIDrawContext& draw = badgeDraw;
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
        const SkullbonezCore::UI::Style::UIRadii& radii = SkullbonezCore::UI::Style::Radii();

        const char* modeLine = values.cameraModeLabel;

        const char* detail = "RMB look  WASD  Space";
        SkullbonezCore::UI::Style::UIColor accent = palette.accent;

        if ( values.cameraMode == UiCameraBadgeMode::Attach )
        {
            detail = "LMB target  RMB orbit  F1  Enter";
            accent = palette.accentStrong;
        }
        else if ( values.cameraMode == UiCameraBadgeMode::Manipulator )
        {
            detail = "LMB drag  Space";
            accent = palette.accentStrong;
        }
        else if ( values.cameraMode == UiCameraBadgeMode::Launcher )
        {
            detail = "LMB fire  M mode";
        }
        else if ( values.cameraMode == UiCameraBadgeMode::Inspect )
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

        SkullbonezCore::UI::Widgets::DrawPanel( draw, { x, y, panelW, panelH },
                                                SkullbonezCore::UI::UIVisualState::Visible |
                                                    SkullbonezCore::UI::UIVisualState::Enabled,
                                                SkullbonezCore::UI::Widgets::ComponentAppearance::Compact, 0.86f );
        draw.RoundedRect( x + 1.0f, y + 1.0f, 4.0f, panelH - 2.0f, radii.smallButton, accent.r, accent.g, accent.b, 0.88f );

        draw.Text( x + padX, y + padY, titlePx, accent.r, accent.g, accent.b, modeLine );
        draw.Text( x + padX, y + padY + lineGap, detailPx, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, detail );
    };

    const auto renderInteractionIndicator = [&]()
    {
        if ( !values.interactionRecording && !values.interactionPlayback && values.interactionFailure[0] == '\0' )
        {
            return;
        }

        const SkullbonezCore::UI::UIDrawContext& draw = badgeDraw;
        const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();

        constexpr float x = 16.0f;
        constexpr float y = 16.0f;
        constexpr float panelW = 286.0f;
        constexpr float panelH = 42.0f;

        SkullbonezCore::UI::Widgets::DrawPanel( draw, { x, y, panelW, panelH },
                                                SkullbonezCore::UI::UIVisualState::Visible |
                                                    SkullbonezCore::UI::UIVisualState::Enabled,
                                                SkullbonezCore::UI::Widgets::ComponentAppearance::Compact, 0.88f );

        constexpr float dotX = x + 8.0f;
        constexpr float dotY = y + 8.0f;
        constexpr float dotSize = 12.0f;
        const bool failed = values.interactionFailure[0] != '\0';
        const bool playing = values.interactionPlayback && !failed;
        const float statusR = failed ? 1.0f : ( playing ? 0.20f : 0.96f );
        const float statusG = failed ? 0.65f : ( playing ? 0.88f : 0.18f );
        const float statusB = failed ? 0.10f : ( playing ? 0.32f : 0.18f );
        draw.RoundedRect( dotX, dotY, dotSize, dotSize, dotSize * 0.5f, statusR, statusG, statusB, 1.0f );
        draw.Text( dotX + dotSize + 6.0f, y + 5.0f, 12.0f, statusR, statusG, statusB,
                   failed ? "RECORDING FAILED" : ( playing ? "PLAYBACK" : "REC F8 TO STOP" ) );

        if ( failed )
        {
            char statusLine[160] = {};
            sprintf_s( statusLine, sizeof( statusLine ), "%s", values.interactionFailure );
            draw.Text( x + 8.0f, y + 23.0f, 9.5f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                       statusLine );
        }
        else if ( playing )
        {
            char statusLine[160] = {};
            const std::size_t visibleTurn = values.interactionPlaybackTurnCount == 0u
                                                ? 0u
                                                : (std::min)( values.interactionPlaybackTurn + 1u,
                                                              values.interactionPlaybackTurnCount );
            sprintf_s( statusLine, sizeof( statusLine ), "%zu / %zu turns", visibleTurn,
                       values.interactionPlaybackTurnCount );
            draw.Text( x + 8.0f, y + 23.0f, 9.5f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                       statusLine );
        }
        else
        {
            char statusLine[160] = {};
            sprintf_s( statusLine, sizeof( statusLine ), "%.1fs / %dm   %zu / %zu turns",
                       values.interactionRecordingElapsedSeconds, values.interactionRecordingMaximumMinutes,
                       values.interactionRecordingFrameCount, values.interactionRecordingFrameCapacity );
            draw.Text( x + 8.0f, y + 23.0f, 9.5f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                       statusLine );
        }
    };

    renderScenePauseBadge();
    renderRuntimeModeBadge();
    renderInteractionIndicator();

    if ( !m_badgeDrawList.Empty() )
    {
        m_uiDrawSubmission.Submit( m_badgeDrawList, textBatch, m_gpuTiming, renderTextures, renderCommands,
                                   renderDiagnostics, (std::max)( 1, viewport.screenW ), (std::max)( 1, viewport.screenH ) );
    }
}


void UiTextPass::RenderChromeTail( const UiChromeTailValues& values, Rendering::Dx12GeometryOwner& renderCommands )
{
    Text::TextBatch& textBatch = m_textBatch;
    RenderReplayDivergenceCounter( textBatch, values );

    // Crosshair - always visible when launcher mode is active, regardless of overlay state.
    // A tiny center gap keeps the target visible instead of covering it.
    if ( values.launcherCameraMode )
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
        const char* fireModeLabel = values.launcherFireModeLabel;
        const float modeSz = 0.011f;
        const float modeW = Text2d::MeasureText( modeSz, fireModeLabel );
        Text2d::Render2dTextColor( textBatch, -modeW * 0.5f, -0.048f, modeSz, 0.72f, 0.94f, 1.0f, "%s", fireModeLabel );
#ifdef _DEBUG

        if ( values.reproSnapshotMessage[0] != '\0' && values.reproMessageAgeSeconds <= values.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, values.reproSnapshotMessage );
            Text2d::Render2dTextColor( textBatch, -msgW * 0.5f, -0.065f, msgSz, 0.65f, 0.92f, 1.0f, "%s",
                                       values.reproSnapshotMessage );
        }
#endif
    }
}


void UiTextPass::PrepareOperatorSubmission( const UiTextViewport& viewport, bool drawTestPattern,
                                            Rendering::Dx12TextureOwner& renderTextures,
                                            Rendering::Dx12GeometryOwner& renderCommands,
                                            Rendering::Dx12Diagnostics& renderDiagnostics )
{
    Text::TextBatch& textBatch = m_textBatch;
    PROFILE_BEGIN( "Frame/UI/BuildData" );

    if ( drawTestPattern )
    {
        DrawUiTestPattern( m_uiDrawSubmission, m_testPatternDrawList, textBatch, renderTextures, renderCommands,
                           renderDiagnostics, viewport.screenW, viewport.screenH );
    }
}


void UiTextPass::SubmitOperatorDrawList( const UI::UIDrawList& drawList,
                                         const RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews,
                                         Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                                         Rendering::Dx12TextureOwner& renderTextures,
                                         Rendering::Dx12GeometryOwner& renderCommands,
                                         Rendering::Dx12Diagnostics& renderDiagnostics, const UiTextViewport& viewport )
{
    Text::TextBatch& textBatch = m_textBatch;
    PROFILE_END( "Frame/UI/BuildData" );
    PROFILE_BEGIN( "Frame/UI/PreFlushText" );
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "PreFlushText" );
        Text2d::FlushText( textBatch, renderTextures, renderCommands );
    }
    PROFILE_END( "Frame/UI/PreFlushText" );
    m_uiDrawSubmission.SubmitWithPreviews( drawList, renderTargetPreviews, textBatch, m_gpuTiming, assets, renderResources,
                                           renderTextures, renderCommands, renderDiagnostics, viewport.screenW,
                                           viewport.screenH );

    PROFILE_BEGIN( "Frame/UI/PostFlushText" );
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/UI/PostFlushText" );
        Text2d::FlushText( textBatch, renderTextures, renderCommands );
    }
    PROFILE_END( "Frame/UI/PostFlushText" );
}


void UiTextPass::RenderOverlayContent( const UiTextViewport& viewport, UiOverlayMode mode, int modelCount,
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
    const SkullbonezCore::Core::Profiler& profiler = m_profilerLifecycle.Require( "RenderOverlayContent" );
    const UI::UIProfilerOverlayPresenter profilerOverlay;
#else
    (void)viewport;
    (void)rollingFpsTime;
    (void)renderTextures;
    (void)renderDiagnostics;
#endif

    // Overlay: None:
    if ( mode == UiOverlayMode::None )
    {
        return;
    }

    // Overlay: Scene telemetry:
    if ( mode == UiOverlayMode::SceneStats )
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

    // Overlay: Visual profiler bars (normalized or absolute):
#if defined( SKULLBONEZ_PROFILE_ENABLED )

    if ( mode == UiOverlayMode::BarsNormalized || mode == UiOverlayMode::BarsAbsolute )
    {
        // Panel anchored bottom-left, filling most of the width. Height kept modest - leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height

        const float panX = -( hw - mX ) + mX * 0.5f; // slight left margin

        const float panY = -( hh - mY ) + mY * 0.5f; // slight bottom margin

        const bool absolute = ( mode == UiOverlayMode::BarsAbsolute );

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

    // Overlay: Keys reference screen (compact, bottom-left):
    if ( mode == UiOverlayMode::Keys )
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
            { "Esc", "Min/expand UI" },    { "Esc Esc", "Quit" },       { "P", "Replay play/pause" },
            { "1", "Freeze water" },       { "2", "Reflection mode" },  { "3", "Toggle water flat" },
            { "4", "Toggle terrain" },     { "5", "Toggle water" },     { "6", "Debug body alpha" },
            { "G", "Broadphase overlay" }, { "C", "Physics debug" },    { "O", "Terrain probe" },
            { "PgUp/Dn", "Water height" }, { "[/]", "Pipeline stage" }, { "F8", "Record repro" },
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

    // Overlay: Timers / HUD (OverlayMode::Timers):

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


void UiTextPass::SubmitDrawList( const UI::UIDrawList& drawList, const UiTextViewport& viewport,
                                 Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderCommands,
                                 Rendering::Dx12Diagnostics& renderDiagnostics )
{
    m_detachedDrawStats = drawList.GetStats();
    m_uiDrawSubmission.Submit( drawList, m_textBatch, nullptr, renderTextures, renderCommands, renderDiagnostics,
                               viewport.screenW, viewport.screenH );
}


void UiTextPass::FinalizeOverlay( UiOverlayMode mode, Rendering::Dx12TextureOwner& renderTextures,
                                  Rendering::Dx12GeometryOwner& renderCommands,
                                  Rendering::Dx12Diagnostics& renderDiagnostics )
{
    if ( mode == UiOverlayMode::BarsNormalized || mode == UiOverlayMode::BarsAbsolute )
    {
        return;
    }

    const char* traceLabel = "ProfilerOverlay";

    if ( mode == UiOverlayMode::None )
    {
        traceLabel = "HUD";
    }
    else if ( mode == UiOverlayMode::SceneStats )
    {
        traceLabel = "SceneStats";
    }
    else if ( mode == UiOverlayMode::Keys )
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
    const RetainedUIDrawStatsScope retainedDrawStats( m_testPatternDrawList, m_badgeDrawList, m_detachedDrawStats,
                                                      m_profilerDrawList );
}
