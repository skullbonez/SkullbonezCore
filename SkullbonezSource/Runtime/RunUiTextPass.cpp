/*
File: SkullbonezSource/Runtime/RunUiTextPass.cpp
Purpose:
  Implements the UI/Text render pass owned by Run.

Mental model:
  World rendering can be skipped, redirected, or post-processed, but UI/text is
  a late pass over the final window. It owns font lifetime, text-only output,
  HUD overlays, and the in-game UI draw payload.

Glossary:
  HUD (Heads-Up Display): Lightweight text diagnostics drawn over the scene.
  Text-only mode: Validation mode that skips world rendering and renders glyphs
  on a solid background to isolate text output.
  UI frame data: Borrowed per-frame snapshot passed to the immediate-mode UI.

Invariants:
  - Font resources are created once through EnsureGpuResources and released
    before backend teardown.
  - Render flushes Text2d before returning, so callers do not inherit queued UI
    glyphs into later frame work.
*/
#include "RunInternal.h"
#include "../Core/WorkerPool.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Basics::RunInternal;

void Run::UiTextPass::EnsureGpuResources()
{
    Text2d::BuildFont( "Verdana" );
}


void Run::UiTextPass::ReleaseGpuResources()
{
    Text2d::DeleteFont();
}


void Run::RenderReplayScrubberOverlay()
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay" );
    RenderReplayCauseTreeOverlay();

    if ( !ShouldRenderReplayScrubber() )
    {
        return;
    }

    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    const ReplayRecorderStats solverReplayStats = m_solverReplay.GetStats();
    if ( screenW <= 0 || screenH <= 0 || solverReplayStats.sampleCount < 2 )
    {
        return;
    }

    const RunReplayTrack activeTrack = RunReplayTrack::Solver;
    const float t = std::clamp( ReplayScrubberTrackPosition( m_replayScrubber, activeTrack ), 0.0f, 1.0f );
    const ReplaySolverFrameSample* selected = m_solverReplay.SampleAtNormalized( t );
    const ReplaySolverFrameSample* latest = m_solverReplay.LatestSample();
    const double selectedSeconds = selected ? selected->simulationSeconds : 0.0;
    const double latestSeconds = latest ? latest->simulationSeconds : 0.0;
    double secondsBack = 0.0;
    if ( latestSeconds >= selectedSeconds )
    {
        secondsBack = latestSeconds - selectedSeconds;
    }

    char timeLabel[48] = {};
    if ( t >= REPLAY_SCRUBBER_LIVE_THRESHOLD && !m_replayScrubber.paused )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "LIVE" );
    }
    else
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "-%.1fs", secondsBack );
    }

    const UI::UIDrawContext draw( screenW, screenH );
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const bool live = t >= REPLAY_SCRUBBER_LIVE_THRESHOLD && !m_replayScrubber.paused;
    const double now = m_timers.simulationTimer.GetTotalTime();

    draw.RoundedRect( panel.x, panel.y, panel.w, panel.h, 8.0f, 0.015f, 0.018f, 0.024f, 0.74f );
    draw.Text( panel.x + 16.0f,
               panel.y + 19.0f,
               10.5f,
               0.54f,
               0.98f,
               0.80f,
               "SOLVER" );
    const float labelW = Text2d::MeasureText( 11.0f, timeLabel );
    draw.Text( panel.x + panel.w - labelW - 16.0f,
               panel.y + 18.0f,
               11.0f,
               live ? 0.58f : 1.0f,
               live ? 0.96f : 0.86f,
               live ? 0.70f : 0.36f,
               timeLabel );

    const UI::UIRect pauseButton = ReplayScrubberPauseButtonRect( screenW, screenH );
    const bool simulationPaused = m_replayScrubber.simulationPaused;
    const bool pauseHover = m_replayScrubber.pauseHovered;
    draw.RoundedRect( pauseButton.x,
                      pauseButton.y,
                      pauseButton.w,
                      pauseButton.h,
                      4.0f,
                      simulationPaused ? 0.18f : 0.08f,
                      simulationPaused ? 0.33f : 0.12f,
                      simulationPaused ? 0.21f : 0.15f,
                      pauseHover || simulationPaused ? 0.94f : 0.78f );
    draw.Outline( pauseButton.x,
                  pauseButton.y,
                  pauseButton.w,
                  pauseButton.h,
                  0.58f,
                  0.92f,
                  0.72f,
                  pauseHover || simulationPaused ? 0.78f : 0.36f );
    draw.Text( pauseButton.x + 9.0f,
               pauseButton.y + 5.0f,
               9.5f,
               simulationPaused ? 0.72f : 0.60f,
               simulationPaused ? 1.0f : 0.72f,
               simulationPaused ? 0.78f : 0.76f,
               simulationPaused ? "PLAY" : "PAUSE" );

    {
        PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay/VelocityEditControls" );
        const UI::UIRect velocityEdit = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
        const bool velocityEditEnabled = m_replayVelocityEdit.enabled;
        const bool velocityEditHover = m_replayVelocityEdit.toggleHovered;
        draw.RoundedRect( velocityEdit.x,
                          velocityEdit.y,
                          velocityEdit.w,
                          velocityEdit.h,
                          4.0f,
                          velocityEditEnabled ? 0.25f : 0.08f,
                          velocityEditEnabled ? 0.19f : 0.12f,
                          velocityEditEnabled ? 0.06f : 0.15f,
                          velocityEditHover || velocityEditEnabled ? 0.94f : 0.78f );
        draw.Outline( velocityEdit.x,
                      velocityEdit.y,
                      velocityEdit.w,
                      velocityEdit.h,
                      0.98f,
                      0.82f,
                      0.42f,
                      velocityEditHover || velocityEditEnabled ? 0.78f : 0.34f );
        const float checkX = velocityEdit.x + 7.0f;
        const float checkY = velocityEdit.y + 5.0f;
        draw.Outline( checkX, checkY, 10.0f, 10.0f, 0.98f, 0.86f, 0.54f, 0.82f );
        if ( velocityEditEnabled )
        {
            draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, 1.0f, 0.62f, 0.16f, 0.95f );
        }
        draw.Text( velocityEdit.x + 23.0f,
                   velocityEdit.y + 4.5f,
                   9.5f,
                   velocityEditEnabled ? 1.0f : 0.66f,
                   velocityEditEnabled ? 0.86f : 0.72f,
                   velocityEditEnabled ? 0.56f : 0.76f,
                   "ALT VEL" );
    }

    auto drawReplayRow = [&]( RunReplayTrack trackName,
                              float fillR,
                              float fillG,
                              float fillB,
                              float outlineR,
                              float outlineG,
                              float outlineB )
    {
        const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, trackName );
        const UI::UIRect saveButton = ReplayScrubberSaveButtonRect( screenW, screenH, trackName );
        const float rowT = std::clamp( ReplayScrubberTrackPosition( m_replayScrubber, trackName ), 0.0f, 1.0f );
        const float fillW = (std::max)( REPLAY_SCRUBBER_TRACK_HEIGHT, track.w * rowT );
        const float knobX = track.x + track.w * rowT;
        const bool active = activeTrack == trackName;
        const bool inactiveDuringScrub = ( m_replayScrubber.dragging || m_replayScrubber.paused ) && !active;
        const bool saveHover = m_replayScrubber.saveHovered && m_replayScrubber.saveHoveredTrack == trackName;
        const bool saveFeedback = m_replayScrubber.saveMessage[0] != '\0' && m_replayScrubber.saveMessageUntil >= now &&
                                  m_replayScrubber.saveMessageTrack == trackName;
        const bool saveFailed = saveFeedback && strstr( m_replayScrubber.saveMessage, "FAILED" ) != nullptr;
        const float saveR = saveFeedback ? ( saveFailed ? 0.48f : 0.13f ) : ( saveHover ? 0.20f : 0.09f );
        const float saveG = saveFeedback ? ( saveFailed ? 0.12f : 0.48f ) : ( saveHover ? 0.42f : 0.20f );
        const float saveB = saveFeedback ? ( saveFailed ? 0.12f : 0.34f ) : ( saveHover ? 0.55f : 0.28f );

        const float rowBack = inactiveDuringScrub ? 0.11f : 0.16f;
        const float rowFillR = inactiveDuringScrub ? 0.30f : fillR;
        const float rowFillG = inactiveDuringScrub ? 0.33f : fillG;
        const float rowFillB = inactiveDuringScrub ? 0.36f : fillB;
        const float rowFillA = inactiveDuringScrub ? 0.40f : ( live && active ? 0.64f : 0.94f );
        draw.RoundedRect( track.x,
                          track.y,
                          track.w,
                          track.h,
                          track.h * 0.5f,
                          rowBack,
                          rowBack + 0.02f,
                          rowBack + 0.05f,
                          inactiveDuringScrub ? 0.74f : 0.92f );
        draw.RoundedRect( track.x, track.y, fillW, track.h, track.h * 0.5f, rowFillR, rowFillG, rowFillB, rowFillA );
        draw.RoundedRect( knobX - 6.0f,
                          track.y - 5.0f,
                          12.0f,
                          18.0f,
                          5.0f,
                          active ? 0.98f : 0.52f,
                          active ? 0.98f : 0.56f,
                          active ? 1.0f : 0.60f,
                          active ? 0.98f : 0.70f );
        draw.Outline( knobX - 6.0f,
                      track.y - 5.0f,
                      12.0f,
                      18.0f,
                      outlineR,
                      outlineG,
                      outlineB,
                      active ? 0.72f : 0.22f );

        draw.RoundedRect( saveButton.x, saveButton.y, saveButton.w, saveButton.h, 4.0f, saveR, saveG, saveB, 0.96f );
        draw.Outline( saveButton.x,
                      saveButton.y,
                      saveButton.w,
                      saveButton.h,
                      outlineR,
                      outlineG,
                      outlineB,
                      saveHover || saveFeedback ? 0.74f : 0.36f );

        const float iconX = saveButton.x + 6.0f;
        const float iconY = saveButton.y + 5.0f;
        const float iconW = 10.0f;
        const float iconH = 12.0f;
        draw.Outline( iconX, iconY, iconW, iconH, 0.88f, 0.97f, 1.0f, 0.96f );
        draw.Rect( iconX + 2.0f, iconY + 2.0f, iconW - 4.0f, 3.0f, 0.88f, 0.97f, 1.0f, 0.70f );
        draw.Rect( iconX + 3.0f, iconY + 8.0f, iconW - 6.0f, 3.0f, 0.88f, 0.97f, 1.0f, 0.82f );
    };

    drawReplayRow( RunReplayTrack::Solver, 0.30f, 0.93f, 0.72f, 0.48f, 0.86f, 0.74f );

    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
    const UI::UIRect predict = ReplayScrubberPredictControlRect( screenW, screenH );
    const UI::UIRect predictHorizon = ReplayScrubberPredictHorizonRect( screenW, screenH );
    const bool predictHover = m_replayPrediction.horizonHovered || m_replayPrediction.horizonDragging;
    const bool predictEnabled = m_replayPrediction.enabled;
    const float predictSeconds =
        std::clamp( m_replayPrediction.horizonSeconds, REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );
    const float predictBackR = predictEnabled ? 0.08f : 0.055f;
    const float predictBackG = predictEnabled ? 0.24f : 0.08f;
    const float predictBackB = predictEnabled ? 0.16f : 0.105f;
    draw.RoundedRect( predictToggle.x,
                      predictToggle.y,
                      predictToggle.w,
                      predictToggle.h,
                      4.0f,
                      predictBackR + ( m_replayPrediction.checkboxHovered ? 0.07f : 0.0f ),
                      predictBackG + ( m_replayPrediction.checkboxHovered ? 0.07f : 0.0f ),
                      predictBackB + ( m_replayPrediction.checkboxHovered ? 0.07f : 0.0f ),
                      0.88f );
    draw.Outline( predictToggle.x,
                  predictToggle.y,
                  predictToggle.w,
                  predictToggle.h,
                  0.62f,
                  0.86f,
                  0.78f,
                  m_replayPrediction.checkboxHovered || predictEnabled ? 0.72f : 0.34f );
    const float checkX = predictToggle.x + 7.0f;
    const float checkY = predictToggle.y + 5.0f;
    draw.Outline( checkX, checkY, 10.0f, 10.0f, 0.82f, 0.94f, 0.90f, 0.82f );
    if ( predictEnabled )
    {
        draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, 0.38f, 1.0f, 0.58f, 0.95f );
    }
    draw.Text( predictToggle.x + 23.0f,
               predictToggle.y + 4.5f,
               9.5f,
               predictEnabled ? 0.70f : 0.60f,
               predictEnabled ? 1.0f : 0.72f,
               predictEnabled ? 0.78f : 0.76f,
               "PREDICT" );

    draw.RoundedRect( predict.x,
                      predict.y,
                      predict.w,
                      predict.h,
                      4.0f,
                      predictBackR + ( predictHover ? 0.07f : 0.0f ),
                      predictBackG + ( predictHover ? 0.07f : 0.0f ),
                      predictBackB + ( predictHover ? 0.07f : 0.0f ),
                      0.88f );
    draw.Outline( predict.x,
                  predict.y,
                  predict.w,
                  predict.h,
                  0.62f,
                  0.86f,
                  0.78f,
                  predictHover || predictEnabled ? 0.72f : 0.34f );

    char predictSecondsLabel[16] = {};
    sprintf_s( predictSecondsLabel, sizeof( predictSecondsLabel ), "%.0fs", static_cast<double>( predictSeconds ) );
    const float horizonT = ReplayPredictionHorizonT( predictSeconds );
    const float horizonFillW = (std::max)( 4.0f, predictHorizon.w * horizonT );
    const float horizonKnobX = predictHorizon.x + predictHorizon.w * horizonT;
    draw.RoundedRect( predictHorizon.x,
                      predictHorizon.y,
                      predictHorizon.w,
                      predictHorizon.h,
                      4.0f,
                      0.10f,
                      0.14f,
                      0.15f,
                      0.86f );
    draw.RoundedRect( predictHorizon.x,
                      predictHorizon.y,
                      horizonFillW,
                      predictHorizon.h,
                      4.0f,
                      0.34f,
                      0.95f,
                      0.62f,
                      predictEnabled ? 0.86f : 0.48f );
    draw.RoundedRect( horizonKnobX - 4.0f,
                      predictHorizon.y - 3.0f,
                      8.0f,
                      14.0f,
                      3.0f,
                      predictEnabled ? 0.88f : 0.56f,
                      predictEnabled ? 1.0f : 0.62f,
                      predictEnabled ? 0.82f : 0.64f,
                      m_replayPrediction.horizonHovered || m_replayPrediction.horizonDragging ? 0.98f : 0.86f );
    draw.Text( predictHorizon.x + predictHorizon.w + 8.0f,
               predict.y + 4.5f,
               8.5f,
               predictEnabled ? 0.90f : 0.64f,
               predictEnabled ? 1.0f : 0.74f,
               predictEnabled ? 0.88f : 0.76f,
               predictSecondsLabel );

    Text2d::FlushQuads();
    Text2d::FlushText();
}


void Run::RenderReplayCauseTreeOverlay()
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Overlay" );
    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    if ( screenW <= 0 || screenH <= 0 || !BuildReplayCauseTreeRows() )
    {
        return;
    }

    EnsureReplayCauseWindowPlacement( m_replayCauseTree, screenW, screenH );
    const UI::UIRect panel = ReplayCauseWindowRect( m_replayCauseTree );
    const UI::UIRect title = ReplayCauseWindowTitleRect( m_replayCauseTree );
    const UI::UIRect content = ReplayCauseWindowContentRect( m_replayCauseTree );
    const UI::UIRect resize = ReplayCauseWindowResizeRect( m_replayCauseTree );

    const UI::UIDrawContext draw( screenW, screenH );
    draw.RoundedRect( panel.x, panel.y, panel.w, panel.h, 7.0f, 0.014f, 0.018f, 0.024f, 0.88f );
    draw.Outline( panel.x, panel.y, panel.w, panel.h, 0.36f, 0.54f, 0.62f, 0.50f );
    draw.Rect( title.x, title.y + title.h - 1.0f, title.w, 1.0f, 0.36f, 0.54f, 0.62f, 0.35f );
    draw.Text( panel.x + 12.0f, panel.y + 10.0f, 13.5f, 0.82f, 0.94f, 1.0f, "REPLAY CAMERA" );
    draw.Text( panel.x + 136.0f, panel.y + 12.0f, 11.0f, 0.58f, 0.70f, 0.78f, "CAUSE" );

    const bool predictionRows = !m_replayCauseTree.rows.empty() && m_replayCauseTree.rows.front().prediction;
    const char* sourceLabel = predictionRows ? "PREDICT" : "REPLAY";
    const float sourceW = Text2d::MeasureText( 9.5f, sourceLabel );
    draw.RoundedRect( panel.x + panel.w - sourceW - 26.0f,
                      panel.y + 9.0f,
                      sourceW + 14.0f,
                      18.0f,
                      4.0f,
                      predictionRows ? 0.08f : 0.08f,
                      predictionRows ? 0.30f : 0.18f,
                      predictionRows ? 0.17f : 0.27f,
                      0.70f );
    draw.Text( panel.x + panel.w - sourceW - 19.0f,
               panel.y + 13.0f,
               9.5f,
               predictionRows ? 0.62f : 0.62f,
               predictionRows ? 1.0f : 0.86f,
               predictionRows ? 0.72f : 1.0f,
               sourceLabel );

    draw.Rect( content.x, content.y, content.w, content.h, 0.02f, 0.026f, 0.034f, 0.36f );

    const ReplaySolverFrameSample* scrubSample = CurrentReplaySolverScrubSample();
    const ReplayFrameIndex presentFrame = scrubSample ? scrubSample->frameIndex : 0;

    auto truncateText = []( const char* src, char* dst, std::size_t dstSize, int maxChars ) -> void
    {
        if ( dstSize == 0 )
        {
            return;
        }
        dst[0] = '\0';
        if ( !src )
        {
            return;
        }
        maxChars = (std::max)( 4, maxChars );
        strncpy_s( dst, dstSize, src, _TRUNCATE );
        if ( static_cast<int>( strlen( dst ) ) > maxChars )
        {
            const int end = (std::min)( maxChars, static_cast<int>( dstSize ) - 1 );
            if ( end >= 4 )
            {
                dst[end - 3] = '.';
                dst[end - 2] = '.';
                dst[end - 1] = '.';
                dst[end] = '\0';
            }
        }
    };

    const float rowAreaW = content.w - 12.0f;
    const int firstRow =
        (std::max)( 0, static_cast<int>( floorf( m_replayCauseTree.scrollY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) ) );
    const int rowCount = static_cast<int>( m_replayCauseTree.rows.size() );
    for ( int rowIndex = firstRow; rowIndex < rowCount; ++rowIndex )
    {
        const RunReplayCauseTreeRow& row = m_replayCauseTree.rows[static_cast<std::size_t>( rowIndex )];
        const float rowY =
            content.y + static_cast<float>( rowIndex ) * REPLAY_CAUSE_WINDOW_ROW_HEIGHT - m_replayCauseTree.scrollY;
        if ( rowY + REPLAY_CAUSE_WINDOW_ROW_HEIGHT < content.y )
        {
            continue;
        }
        if ( rowY + REPLAY_CAUSE_WINDOW_ROW_HEIGHT > content.y + content.h )
        {
            break;
        }

        const UI::UIRect rowRect = { content.x + 2.0f, rowY, rowAreaW, REPLAY_CAUSE_WINDOW_ROW_HEIGHT - 2.0f };
        const bool hovered = rowIndex == m_replayCauseTree.hoveredRow;
        const bool selected = rowIndex == m_replayCauseTree.selectedRow;
        if ( hovered || selected )
        {
            draw.RoundedRect( rowRect.x,
                              rowRect.y,
                              rowRect.w,
                              rowRect.h,
                              4.0f,
                              selected ? 0.12f : 0.08f,
                              selected ? 0.30f : 0.18f,
                              selected ? 0.22f : 0.24f,
                              hovered ? 0.82f : 0.56f );
        }

        const float indent = (std::min)( rowRect.w * 0.40f, static_cast<float>( row.depth ) * 16.0f );
        if ( row.depth > 0 )
        {
            const float lineX = rowRect.x + 8.0f + indent - 9.0f;
            draw.Rect( lineX, rowRect.y + 4.0f, 1.0f, rowRect.h - 8.0f, 0.62f, 0.68f, 0.72f, 0.32f );
            draw.Rect( lineX, rowRect.y + rowRect.h * 0.5f, 8.0f, 1.0f, 0.62f, 0.68f, 0.72f, 0.32f );
        }

        char prefix[32] = {};
        switch ( row.kind )
        {
        case RunReplayCauseTreeRowKind::Body:
            if ( row.depth == 0 )
            {
                strncpy_s( prefix, sizeof( prefix ), "ROOT", _TRUNCATE );
            }
            else
            {
                double secondsToHit = 0.0;
                if ( row.prediction )
                {
                    secondsToHit = static_cast<double>( row.firstFrame ) * PHYSICS_FIXED_DT;
                }
                else if ( row.firstFrame > presentFrame )
                {
                    secondsToHit = static_cast<double>( row.firstFrame - presentFrame ) * PHYSICS_FIXED_DT;
                }
                sprintf_s( prefix, sizeof( prefix ), "+%.2fs", secondsToHit );
            }
            break;
        case RunReplayCauseTreeRowKind::Manifold:
            strncpy_s( prefix, sizeof( prefix ), "MANIFOLD", _TRUNCATE );
            break;
        case RunReplayCauseTreeRowKind::SolverRow:
            strncpy_s( prefix, sizeof( prefix ), "ROW", _TRUNCATE );
            break;
        case RunReplayCauseTreeRowKind::PredictionContact:
            strncpy_s( prefix, sizeof( prefix ), "CONTACT", _TRUNCATE );
            break;
        }

        char label[144] = {};
        sprintf_s( label, sizeof( label ), "%s  %s", prefix, row.name );
        char clippedLabel[144] = {};
        const int labelChars = static_cast<int>( ( rowRect.w - indent - 18.0f ) / 8.4f );
        truncateText( label, clippedLabel, sizeof( clippedLabel ), labelChars );
        char clippedDetail[160] = {};
        const int detailChars = static_cast<int>( ( rowRect.w - indent - 18.0f ) / 7.2f );
        truncateText( row.detail, clippedDetail, sizeof( clippedDetail ), detailChars );

        float markerR = 0.94f;
        float markerG = 1.0f;
        float markerB = 0.74f;
        if ( row.kind == RunReplayCauseTreeRowKind::Manifold )
        {
            markerR = 0.20f;
            markerG = 0.90f;
            markerB = 1.0f;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::SolverRow )
        {
            markerR = 1.0f;
            markerG = 0.42f;
            markerB = 0.18f;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact )
        {
            markerR = 0.38f;
            markerG = 1.0f;
            markerB = 0.58f;
        }

        const float markerX = rowRect.x + 8.0f + indent;
        const float markerY = rowRect.y + 8.0f;
        draw.Rect( markerX, markerY, 6.0f, 6.0f, markerR, markerG, markerB, 0.92f );
        draw.Text( markerX + 11.0f,
                   rowRect.y + 4.0f,
                   12.4f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? 0.88f : 0.78f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? 1.0f : 0.86f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? 0.86f : 0.82f,
                   clippedLabel );
        if ( clippedDetail[0] != '\0' )
        {
            draw.Text( markerX + 11.0f, rowRect.y + 22.0f, 10.4f, 0.56f, 0.66f, 0.72f, clippedDetail );
        }
    }

    const float maxScroll = ReplayCauseWindowMaxScroll( m_replayCauseTree );
    if ( maxScroll > 0.0f )
    {
        const float trackX = content.x + content.w - 5.0f;
        draw.Rect( trackX, content.y + 3.0f, 3.0f, content.h - 6.0f, 0.16f, 0.22f, 0.28f, 0.72f );
        const float contentHeight = ReplayCauseWindowContentHeight( m_replayCauseTree );
        const float knobH = (std::max)( 24.0f, ( content.h / contentHeight ) * ( content.h - 6.0f ) );
        const float knobY = content.y + 3.0f + ( m_replayCauseTree.scrollY / maxScroll ) * ( content.h - 6.0f - knobH );
        draw.RoundedRect( trackX - 1.0f, knobY, 5.0f, knobH, 2.0f, 0.42f, 0.60f, 0.68f, 0.78f );
    }

    draw.Rect( resize.x + 4.0f, resize.y + resize.h - 5.0f, resize.w - 7.0f, 1.0f, 0.56f, 0.70f, 0.76f, 0.68f );
    draw.Rect( resize.x + resize.w - 5.0f, resize.y + 4.0f, 1.0f, resize.h - 7.0f, 0.56f, 0.70f, 0.76f, 0.68f );

    Text2d::FlushQuads();
    Text2d::FlushText();
}


bool Run::UiTextPass::ShouldRender() const
{
    return m_run.m_debug.isTextOnly || !m_run.SceneState().isSceneMode || m_run.SceneState().isSceneText ||
           m_run.m_debug.overlayMode != OverlayMode::None || m_run.m_UI.IsVisible() ||
           m_run.ShouldRenderReplayScrubber() || m_run.m_replayPathVisualizer.hasTarget;
}


void Run::UiTextPass::Render( double dSecondsPerFrame )
{
    const int uiPassDrawCallStart = Gfx().GetFrameDrawCallCount();

    // Invariant: rolling diagnostics update before any overlay early return so
    // FPS, physics time, render time, and scene energy age at the same cadence.
    m_run.m_timers.updateTimer.StopTimer();
    m_run.m_timers.timeSinceLastRender += static_cast<float>( m_run.m_timers.updateTimer.GetElapsedTime() );
    m_run.m_timers.updateTimer.StartTimer();

    const double currentSceneEnergy = m_run.m_cGameModelCollection.GetSceneKineticEnergy();
    m_run.m_timers.sceneEnergyAccumulator += currentSceneEnergy;
    ++m_run.m_timers.sceneEnergySampleCount;

    if ( m_run.m_timers.timeSinceLastRender > 0.5f )
    {
        if ( dSecondsPerFrame )
        {
            m_run.m_timers.rollingFpsTime = 1.0f / static_cast<float>( dSecondsPerFrame );
            m_run.m_timers.rollingPhysicsTime = m_run.m_timers.physicsTime;
            m_run.m_timers.rollingRenderTime = m_run.m_timers.renderTime;
        }
        if ( m_run.m_timers.sceneEnergySampleCount > 0 )
        {
            m_run.m_timers.rollingSceneEnergy = static_cast<float>(
                m_run.m_timers.sceneEnergyAccumulator / static_cast<double>( m_run.m_timers.sceneEnergySampleCount ) );
            m_run.m_timers.sceneEnergyAccumulator = 0.0;
            m_run.m_timers.sceneEnergySampleCount = 0;
        }
        m_run.m_timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = m_run.m_timers.rollingSceneEnergy;
    if ( m_run.m_timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( m_run.m_timers.sceneEnergyAccumulator /
                                                    static_cast<double>( m_run.m_timers.sceneEnergySampleCount ) );
    }

    const char* rendererName = Gfx().GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler
    if ( m_run.m_debug.isTextOnly )
    {
        // Dark background covering the full viewport
        Text2d::Render2dQuad( -0.55f, -0.45f, 0.55f, 0.45f, 0.08f, 0.08f, 0.12f, 1.0f );

        // Three rows of the pangram - each line uses a slightly different color
        // so hue/brightness fringing artifacts are visible on all channel combinations
        const float sz = 0.09f;
        Text2d::Render2dTextColor( -0.46f, 0.22f, sz, 1.00f, 1.00f, 1.00f, "The quick brown fox" );
        Text2d::Render2dTextColor( -0.46f, 0.07f, sz, 1.00f, 0.90f, 0.20f, "jumps over the" );
        Text2d::Render2dTextColor( -0.46f, -0.08f, sz, 0.40f, 0.90f, 1.00f, "lazy dog" );

        // Renderer name in small text at bottom so we know which backend we're looking at
        Text2d::Render2dTextColor( -0.46f, -0.38f, 0.015f, 0.60f, 0.60f, 0.60f, "renderer: %s", rendererName );

        {
            DRAW_CALL_TRACE_SCOPE( "TextOnly" );
            Text2d::FlushText();
        }
        return;
    }

    const float hw = Text2d::HalfW();
    const float hh = Text2d::HalfH();
    const float mX = 0.022f; // horizontal inset from left/right edge
    const float mY = 0.015f; // vertical inset from top/bottom edge

    // Crosshair - always visible when launcher mode is active, regardless of overlay state.
    // A tiny center gap keeps the target visible instead of covering it.
    if ( m_run.m_camera.isLauncherMode )
    {
        const float cArm = 0.020f;
        const float cGap = 0.004f;
        const float cHalf = 0.00045f;
        const float cShadowHalf = 0.00080f;
        Text2d::Render2dQuad( -cArm, -cShadowHalf, -cGap, cShadowHalf, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( cGap, -cShadowHalf, cArm, cShadowHalf, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( -cShadowHalf, -cArm, cShadowHalf, -cGap, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( -cShadowHalf, cGap, cShadowHalf, cArm, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( -cArm, -cHalf, -cGap, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( cGap, -cHalf, cArm, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( -cHalf, -cArm, cHalf, -cGap, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( -cHalf, cGap, cHalf, cArm, 0.80f, 0.96f, 1.0f, 0.88f );
        const char* fireModeLabel =
            m_run.m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? "PROJECTILE" : "LASER";
        const float modeSz = 0.011f;
        const float modeW = Text2d::MeasureText( modeSz, fireModeLabel );
        Text2d::Render2dTextColor( -modeW * 0.5f, -0.048f, modeSz, 0.72f, 0.94f, 1.0f, "%s", fireModeLabel );
#ifdef _DEBUG
        if ( m_run.m_debug.reproSnapshotMessage[0] != '\0' &&
             m_run.m_timers.simulationTimer.GetTimeSinceLastStart() <= m_run.m_debug.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, m_run.m_debug.reproSnapshotMessage );
            Text2d::Render2dTextColor( -msgW * 0.5f,
                                       -0.065f,
                                       msgSz,
                                       0.65f,
                                       0.92f,
                                       1.0f,
                                       "%s",
                                       m_run.m_debug.reproSnapshotMessage );
        }
#endif
    }

    m_run.RefreshRuntimeViewModel();
    const RuntimeViewModel& view = m_run.m_runtimeViewModel;

    const char* sceneName = "";
    if ( view.sceneMode && m_run.m_sceneController.HasCurrentEntry() )
    {
        sceneName = FileNameFromPath( m_run.m_sceneController.CurrentPath()->c_str() );
    }

    if ( m_run.m_UI.IsVisible() )
    {
        PROFILE_BEGIN( "Frame/UI/BuildData" );
        InGameUIFrameData UIData;
        UIData.screenW = m_run.WindowScreenWidth();
        UIData.screenH = m_run.WindowScreenHeight();
        if ( m_run.m_debug.isUITestPattern )
        {
            DrawUITestPattern( UIData.screenW, UIData.screenH );
        }
        UIData.rendererName = rendererName;
        UIData.sceneName = sceneName;
        UIData.sceneOptions = m_run.m_sceneBrowserNamePtrs.empty() ? nullptr : m_run.m_sceneBrowserNamePtrs.data();
        UIData.sceneOptionCount = static_cast<int>( m_run.m_sceneBrowserNamePtrs.size() );
        UIData.selectedSceneOption = m_run.CurrentSceneBrowserIndex();
        UIData.selectedCineModeSceneOption = m_run.m_selectedCineModeSceneIndex;
        UIData.UIDrawCalls = m_run.m_timers.lastUIDrawCalls;
        UIData.fps = m_run.m_timers.rollingFpsTime > 0.0f
                         ? m_run.m_timers.rollingFpsTime
                         : ( dSecondsPerFrame > 0.0 ? 1.0f / static_cast<float>( dSecondsPerFrame ) : 0.0f );
        UIData.renderMs =
            ( m_run.m_timers.rollingRenderTime > 0.0f ? m_run.m_timers.rollingRenderTime : m_run.m_timers.renderTime ) *
            1000.0f;
        UIData.physicsMs = ( m_run.m_timers.rollingPhysicsTime > 0.0f ? m_run.m_timers.rollingPhysicsTime
                                                                      : m_run.m_timers.physicsTime ) *
                           1000.0f;
        UIData.cpuFrameMs = m_run.m_timers.cpuFrameWorkMs;
        UIData.gpuFrameMs = m_run.m_timers.gpuFrameWorkMs;
        UIData.modelCount = view.modelCount;
        UIData.modelCapacity = ActiveGameModelCapacity();
        UIData.workerThreadCount = SkullbonezCore::Threading::WorkerPool::Instance().GetThreadCount();
        UIData.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        UIData.currentFrame = view.frame;
        UIData.targetFrameCount = view.targetFrameCount;
        UIData.rngSeed = m_run.SceneState().rngSeed;
        UIData.solverBallCount = m_run.SceneState().solverBallCount;
        UIData.solverBoxCount = m_run.SceneState().solverBoxCount;
        UIData.currentSceneIndex = view.sceneIndex;
        UIData.sceneCount = view.sceneCount;
        UIData.now = m_run.m_timers.simulationTimer.GetTotalTime();
        UIData.sceneMode = view.sceneMode;
        UIData.scenePhysicsEnabled = view.scenePhysics;
        UIData.sceneTextEnabled = view.sceneText;
        UIData.textOnly = m_run.m_debug.isTextOnly;
        UIData.fixedStep = view.fixedStep;
        UIData.exitOnComplete = m_run.SceneState().isExitOnComplete;
        UIData.testComplete = m_run.SceneState().isTestComplete;
        UIData.vsyncEnabled = m_run.m_runtimeSettings.isVsyncEnabled;
        UIData.pipelineSyncEnabled = m_run.m_runtimeSettings.isPipelineSyncEnabled;
        UIData.sceneEnergy = sceneEnergyForDisplay;
        UIData.timeScale = view.timeScale;
        UIData.trackHeight = m_run.m_camera.trackBallIndex >= 0 ? m_run.m_camera.trackHeight : 0.0f;
        UIData.autoCycleInterval = m_run.m_camera.autoCycleInterval > 0.0f ? m_run.m_camera.autoCycleInterval : 0.0f;
        UIData.worldGravity = m_run.m_cWorldEnvironment.GetGravity();
        UIData.worldFluidHeight = m_run.m_cWorldEnvironment.GetFluidSurfaceHeight();
        UIData.worldFluidDensity = m_run.m_cWorldEnvironment.GetFluidDensity();
        UIData.physicsDebugFlags = m_run.m_debug.physicsDebugFlags;
        {
            const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
            int stageIndex = stageCount > 0 ? m_run.m_debug.physicsDebugPipelineStageCursor % stageCount : 0;
            if ( stageIndex < 0 )
            {
                stageIndex += stageCount;
            }
            UIData.physicsPipelineStageName =
                PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );
            UIData.physicsPipelineStageIndex = stageIndex;
            UIData.physicsPipelineStageCount = stageCount;
        }
        UIData.physicsDebugAlpha = m_run.m_debug.physicsDebugAlpha;
        UIData.physicsDebugContactLinger = m_run.m_debug.physicsDebugContactLinger;
        UIData.physicsSleepEnabled = m_run.m_runtimeSettings.isPhysicsSleepEnabled;
        UIData.collisionVisualizer = m_run.m_debug.isCollisionVisualizer;
        UIData.physicsDebugTransparent = m_run.m_debug.isPhysicsDebugTransparent;
        UIData.broadphaseOverlay = m_run.m_debug.isBroadphaseOverlay;
        UIData.tornadoEnabled = m_run.m_runtimeSettings.tornadoField.enabled;
        UIData.tornadoFieldVectors = m_run.m_runtimeSettings.tornadoField.visualizeVelocityField;
        UIData.tornadoRadius = m_run.m_runtimeSettings.tornadoField.radius;
        UIData.tornadoHeight = m_run.m_runtimeSettings.tornadoField.height;
        UIData.tornadoInwardAcceleration = m_run.m_runtimeSettings.tornadoField.inwardAcceleration;
        UIData.tornadoSwirlAcceleration = m_run.m_runtimeSettings.tornadoField.swirlAcceleration;
        UIData.tornadoLiftAcceleration = m_run.m_runtimeSettings.tornadoField.liftAcceleration;
        UIData.rayCastVisualization = m_run.m_rayCastTest.visualizeRays;
        UIData.rayCastImpulseStrength = m_run.m_rayCastTest.impulseStrength;
        UIData.launcherProjectileSpeed = m_run.m_rayCastTest.projectileSpeed;
        UIData.waterFreezeDebug = m_run.m_debug.isWaterFreezeDebug;
        UIData.waterFlatDebug = m_run.m_debug.isWaterFlatDebug;
        UIData.terrainHidden = m_run.m_debug.isTerrainHidden;
        UIData.waterHidden = m_run.m_debug.isWaterHidden;
        UIData.waterNoReflect = m_run.m_debug.isWaterNoReflect;
        UIData.waterRTReflect = m_run.m_debug.isWaterRTReflect;
        const RuntimeInputMode runtimeInputMode = m_run.m_runtimeInput.CurrentMode();
        UIData.cameraModeIndex = static_cast<int>( m_run.m_camera.mode );
        UIData.cameraModeEnabledMask = m_run.CameraModeEnabledMask();
        UIData.runtimeInputModeLabel = m_run.CameraModeLabel( m_run.m_camera.mode );
        UIData.cameraMouseActive =
            ( runtimeInputMode == RuntimeInputMode::FlyCamera || runtimeInputMode == RuntimeInputMode::Launcher ||
              runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
            !m_run.m_UI.BlocksCameraMouse();
        UIData.nativeCursorVisible = !UIData.cameraMouseActive;
        UIData.editorModeEnabled = m_run.m_editor.editorModeEnabled;
        UIData.editorPlacementMode = m_run.m_editor.placementModeEnabled;
        UIData.editorPlaceStatic = m_run.m_editor.placeStaticObject;
        UIData.editorTerrainAlign = m_run.m_editor.autoTerrainAlign;
        UIData.editorViewportLookActive = m_run.m_editor.viewportLookActive;
        UIData.editorObjectType = m_run.m_editor.objectType;
        UIData.canSaveSceneDefaults = view.sceneMode && m_run.m_sceneController.HasCurrentEntry() &&
                                      !m_run.m_sceneController.CurrentPath()->empty();
        UIData.cinematicRendering = m_run.IsCinematicRenderingEnabled();
        UIData.ordinaryRender = Cfg().ordinaryRender;
        UIData.cinematic = m_run.ActiveCinematicConfig();
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

            auto addFramebufferPreview = [&]( const char* label,
                                              const SkullbonezCore::Rendering::IFramebuffer* target,
                                              bool depth,
                                              bool available )
            {
                const uint32_t textureHandle =
                    target ? ( depth ? target->GetDepthTextureHandle() : target->GetColorTextureHandle() ) : 0;
                const bool hdr = target && !depth &&
                                 target->GetColorFormat() == SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;
                addPreview( label,
                            textureHandle,
                            target ? target->GetWidth() : 0,
                            target ? target->GetHeight() : 0,
                            available,
                            depth,
                            hdr );
            };

            const RunRenderPassResources& passes = m_run.m_systems.renderPasses;
            const bool shadowsAvailable =
                UIData.cinematicRendering ? UIData.cinematic.shadowsEnabled : UIData.ordinaryRender.shadowsEnabled;
            const bool cinematicTargetsAvailable = UIData.cinematicRendering;

            addFramebufferPreview( "Reflection Color",
                                   passes.reflection.target.get(),
                                   false,
                                   passes.reflection.target != nullptr );
            addFramebufferPreview( "Reflection Depth",
                                   passes.reflection.target.get(),
                                   true,
                                   passes.reflection.target != nullptr );
            addFramebufferPreview( "Terrain Shadow Depth", passes.shadows.terrainTarget.get(), true, shadowsAvailable );
            addFramebufferPreview( "Object Shadow Depth", passes.shadows.objectTarget.get(), true, shadowsAvailable );
            addFramebufferPreview( "Terrain Shadow Color",
                                   passes.shadows.terrainTarget.get(),
                                   false,
                                   shadowsAvailable );
            addFramebufferPreview( "Object Shadow Color", passes.shadows.objectTarget.get(), false, shadowsAvailable );
            addFramebufferPreview( "Cinematic Scene Color",
                                   passes.cinematicScene.hdrTarget.get(),
                                   false,
                                   cinematicTargetsAvailable );
            addFramebufferPreview( "Cinematic Scene Depth",
                                   passes.cinematicScene.hdrTarget.get(),
                                   true,
                                   cinematicTargetsAvailable );
            addFramebufferPreview( "Volumetric Color",
                                   passes.volumetricLight.target.get(),
                                   false,
                                   cinematicTargetsAvailable && UIData.cinematic.volumetricLightingEnabled );
            addFramebufferPreview( "Volumetric Depth",
                                   passes.volumetricLight.target.get(),
                                   true,
                                   cinematicTargetsAvailable && UIData.cinematic.volumetricLightingEnabled );

            const uint32_t dxrReflection = IsGfxReady() ? Gfx().GetReflectionUAVTexture() : 0;
            addPreview( "DXR Reflection",
                        dxrReflection,
                        m_run.WindowScreenWidth() * 2,
                        m_run.WindowScreenHeight() * 2,
                        UIData.waterRTReflect && !UIData.waterNoReflect,
                        false,
                        false );
        }
        PROFILE_END( "Frame/UI/BuildData" );

        PROFILE_BEGIN( "Frame/UI/PreFlushText" );
        {
            DRAW_CALL_TRACE_SCOPE( "PreFlushText" );
            Text2d::FlushText();
        }
        PROFILE_END( "Frame/UI/PreFlushText" );
        UIData.drawCallsBeforeUI = uiPassDrawCallStart;
        m_run.m_UI.Draw( UIData );
        PROFILE_BEGIN( "Frame/UI/PostFlushText" );
        {
            DRAW_CALL_TRACE_SCOPE( "Frame/UI/PostFlushText" );
            Text2d::FlushText();
        }
        PROFILE_END( "Frame/UI/PostFlushText" );
        m_run.RenderReplayScrubberOverlay();
        return;
    }

    // --- Overlay: None ---
    if ( m_run.m_debug.overlayMode == OverlayMode::None )
    {
        m_run.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "HUD" );
            Text2d::FlushText();
        }
        return;
    }

    // --- Overlay: Scene telemetry ---
    if ( m_run.m_debug.overlayMode == OverlayMode::SceneStats )
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

        Text2d::Render2dQuad( panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );
        Text2d::Render2dTextColor( panX0 + panPad,
                                   panY1 - panPad - titleSz,
                                   titleSz,
                                   1.0f,
                                   0.85f,
                                   0.35f,
                                   "SCENE TELEMETRY" );
        Text2d::Render2dTextColor( panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Model Count: %d",
                                   m_run.SceneState().modelCount );
        Text2d::Render2dTextColor( panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH * 2.0f,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Scene Energy: %.6f",
                                   sceneEnergyForDisplay );
        m_run.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "SceneStats" );
            Text2d::FlushText();
        }
        return;
    }

    // --- Overlay: Visual profiler bars (normalized or absolute) ---
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( m_run.m_debug.overlayMode == OverlayMode::BarsNormalized ||
         m_run.m_debug.overlayMode == OverlayMode::BarsAbsolute )
    {
        // Panel anchored bottom-left, filling most of the width. Height kept modest - leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height
        const float panX = -( hw - mX ) + mX * 0.5f;   // slight left margin
        const float panY = -( hh - mY ) + mY * 0.5f;   // slight bottom margin
        const bool absolute = ( m_run.m_debug.overlayMode == OverlayMode::BarsAbsolute );
        Profiler::Instance().RenderBarOverlay( panX, panY, panW, panH, absolute );
        m_run.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "ProfilerBars" );
            Text2d::FlushText();
        }
        return;
    }
#endif

    // --- Overlay: Keys reference screen (compact, bottom-left) ---
    if ( m_run.m_debug.overlayMode == OverlayMode::Keys )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.011f;
        const float lineH = 0.020f;
        const int nRows = 13;
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

        Text2d::Render2dQuad( panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );

        // Title left-aligned inside panel
        const float titleY = panY1 - panPad - titleSz;
        Text2d::Render2dTextColor( panX0 + panPad, titleY, titleSz, 1.0f, 0.85f, 0.35f, "CONTROL REFERENCE" );

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
            { "N", "Launcher mode" },
            { "M", "Launcher fire mode" },
            { "Enter", "Dump repro" },
            { "F", "Fly mode" },
            { "WASD", "Move camera" },
            { "Mouse", "Look" },
            { "Shift", "Sprint (3x speed)" },
            { "LMB", "Fire launcher" },
            { "Q", "Cycle renderer" },
            { "V", "Collision visual" },
            { "Space", "Step physics" },
            { "R/Bksp", "Reset scene" },
            { "F3", "Screenshot" },
        };
        static const KeyEntry kRight[nRows] = {
            { "Esc", "Min/expand UI" },
            { "Esc Esc", "Quit" },
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
        };

        for ( int i = 0; i < nRows; ++i )
        {
            float y = firstY - static_cast<float>( i ) * lineH;
            Text2d::Render2dTextColor( col1Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kLeft[i].key );
            Text2d::Render2dTextColor( col1Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kLeft[i].desc );
            Text2d::Render2dTextColor( col2Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kRight[i].key );
            Text2d::Render2dTextColor( col2Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kRight[i].desc );
        }

        m_run.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "Keys" );
            Text2d::FlushText();
        }
        return;
    }

    // --- Overlay: Timers / HUD (OverlayMode::Timers) ---

    // Profiler overlay - bottom-left anchored.
    // Compiled out in Release; always shown when overlay is Timers in Debug/Profile.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        const float lineH = 0.018f;
        const float profFSz = 0.012f;
        const float padY = lineH * 1.2f;
        Profiler::Instance().RenderOverlay( -( hw - mX ),
                                            -( hh - mY ) - padY,
                                            lineH,
                                            profFSz,
                                            m_run.m_timers.rollingFpsTime );
    }
#endif

    m_run.RenderReplayScrubberOverlay();
    {
        DRAW_CALL_TRACE_SCOPE( "ProfilerOverlay" );
        Text2d::FlushText();
    }
}
