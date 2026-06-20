/*
File: SkullbonezSource/SkullbonezRunUiTextPass.cpp
Purpose:
  Implements the UI/Text render pass owned by SkullbonezRun.

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
#include "SkullbonezRunInternal.h"
#include "SkullbonezWorkerPool.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Basics::RunInternal;

void SkullbonezRun::UiTextPass::EnsureGpuResources()
{
    Text2d::BuildFont( "Verdana" );
}


void SkullbonezRun::UiTextPass::ReleaseGpuResources()
{
    Text2d::DeleteFont();
}


void SkullbonezRun::RenderReplayScrubberOverlay()
{
    if ( !ShouldRenderReplayScrubber() )
    {
        return;
    }

    const int screenW = WindowScreenWidth();
    const int screenH = WindowScreenHeight();
    const ReplayRecorderStats replayStats = m_replay.GetStats();
    if ( screenW <= 0 || screenH <= 0 || replayStats.sampleCount < 2 )
    {
        return;
    }

    const float t = std::clamp( m_replayScrubber.position, 0.0f, 1.0f );
    const ReplayPresentationSample* selected = m_replay.SampleAtNormalized( t );
    const ReplayPresentationSample* latest = m_replay.LatestSample();
    double secondsBack = 0.0;
    if ( selected && latest && latest->simulationSeconds >= selected->simulationSeconds )
    {
        secondsBack = latest->simulationSeconds - selected->simulationSeconds;
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
    const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH );
    const float fillW = (std::max)( REPLAY_SCRUBBER_TRACK_HEIGHT, track.w * t );
    const float knobX = track.x + track.w * t;
    const bool live = t >= REPLAY_SCRUBBER_LIVE_THRESHOLD && !m_replayScrubber.paused;

    draw.RoundedRect( panel.x, panel.y, panel.w, panel.h, 8.0f, 0.015f, 0.018f, 0.024f, 0.74f );
    draw.RoundedRect( track.x, track.y, track.w, track.h, track.h * 0.5f, 0.16f, 0.18f, 0.22f, 0.92f );
    draw.RoundedRect( track.x, track.y, fillW, track.h, track.h * 0.5f, 0.20f, 0.70f, 0.96f, live ? 0.64f : 0.94f );
    draw.RoundedRect( knobX - 6.0f, track.y - 5.0f, 12.0f, 18.0f, 5.0f, 0.92f, 0.96f, 1.0f, 0.98f );
    draw.Text( panel.x + 16.0f, panel.y + 15.0f, 11.0f, 0.72f, 0.88f, 1.0f, "REPLAY" );
    const float labelW = Text2d::MeasureText( 11.0f, timeLabel );
    draw.Text( panel.x + panel.w - labelW - 16.0f, panel.y + 15.0f, 11.0f, live ? 0.58f : 1.0f, live ? 0.96f : 0.86f, live ? 0.70f : 0.36f, timeLabel );

    Text2d::FlushQuads();
    Text2d::FlushText();
}


bool SkullbonezRun::UiTextPass::ShouldRender() const
{
    return m_run.m_debug.isTextOnly ||
           !m_run.SceneState().isSceneMode ||
           m_run.SceneState().isSceneText ||
           m_run.m_debug.overlayMode != OverlayMode::None ||
           m_run.m_UI.IsVisible() ||
           m_run.ShouldRenderReplayScrubber();
}


void SkullbonezRun::UiTextPass::Render( double dSecondsPerFrame )
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
            m_run.m_timers.rollingSceneEnergy = static_cast<float>( m_run.m_timers.sceneEnergyAccumulator / static_cast<double>( m_run.m_timers.sceneEnergySampleCount ) );
            m_run.m_timers.sceneEnergyAccumulator = 0.0;
            m_run.m_timers.sceneEnergySampleCount = 0;
        }
        m_run.m_timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = m_run.m_timers.rollingSceneEnergy;
    if ( m_run.m_timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( m_run.m_timers.sceneEnergyAccumulator / static_cast<double>( m_run.m_timers.sceneEnergySampleCount ) );
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
        const char* fireModeLabel = m_run.m_rayCastTest.fireMode == RunLauncherFireMode::Projectile ? "PROJECTILE" : "LASER";
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

    const char* sceneName = "";
    if ( m_run.SceneState().isSceneMode && m_run.m_sceneRuntime.HasCurrentEntry() )
    {
        sceneName = FileNameFromPath( m_run.m_sceneRuntime.CurrentPath()->c_str() );
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
        UIData.fps = m_run.m_timers.rollingFpsTime > 0.0f ? m_run.m_timers.rollingFpsTime : ( dSecondsPerFrame > 0.0 ? 1.0f / static_cast<float>( dSecondsPerFrame ) : 0.0f );
        UIData.renderMs = ( m_run.m_timers.rollingRenderTime > 0.0f ? m_run.m_timers.rollingRenderTime : m_run.m_timers.renderTime ) * 1000.0f;
        UIData.physicsMs = ( m_run.m_timers.rollingPhysicsTime > 0.0f ? m_run.m_timers.rollingPhysicsTime : m_run.m_timers.physicsTime ) * 1000.0f;
        UIData.cpuFrameMs = m_run.m_timers.cpuFrameWorkMs;
        UIData.gpuFrameMs = m_run.m_timers.gpuFrameWorkMs;
        UIData.modelCount = m_run.SceneState().modelCount;
        UIData.modelCapacity = ActiveGameModelCapacity();
        UIData.workerThreadCount = SkullbonezCore::Threading::WorkerPool::Instance().GetThreadCount();
        UIData.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        UIData.currentFrame = m_run.SceneState().currentFrame;
        UIData.targetFrameCount = m_run.SceneState().targetFrameCount;
        UIData.rngSeed = m_run.SceneState().rngSeed;
        UIData.solverBallCount = m_run.SceneState().solverBallCount;
        UIData.solverBoxCount = m_run.SceneState().solverBoxCount;
        UIData.currentSceneIndex = m_run.SceneState().currentSceneIndex;
        UIData.sceneCount = m_run.m_sceneRuntime.QueueSize();
        UIData.now = m_run.m_timers.simulationTimer.GetTotalTime();
        UIData.sceneMode = m_run.SceneState().isSceneMode;
        UIData.scenePhysicsEnabled = m_run.SceneState().isScenePhysics;
        UIData.sceneTextEnabled = m_run.SceneState().isSceneText;
        UIData.textOnly = m_run.m_debug.isTextOnly;
        UIData.fixedStep = m_run.SceneState().isFixedStep;
        UIData.exitOnComplete = m_run.SceneState().isExitOnComplete;
        UIData.testComplete = m_run.SceneState().isTestComplete;
        UIData.vsyncEnabled = m_run.m_runtimeSettings.isVsyncEnabled;
        UIData.pipelineSyncEnabled = m_run.m_runtimeSettings.isPipelineSyncEnabled;
        UIData.sceneEnergy = sceneEnergyForDisplay;
        UIData.timeScale = m_run.SceneState().timeScale;
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
            UIData.physicsPipelineStageName = PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );
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
        UIData.runtimeInputModeLabel = InputController::DescribeMode( runtimeInputMode );
        UIData.cameraMouseActive = ( runtimeInputMode == RuntimeInputMode::FlyCamera ||
                                     runtimeInputMode == RuntimeInputMode::Launcher ||
                                     runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
                                   !m_run.m_UI.BlocksCameraMouse();
        UIData.nativeCursorVisible = !UIData.cameraMouseActive;
        UIData.editorModeEnabled = m_run.m_editor.editorModeEnabled;
        UIData.editorPlacementMode = m_run.m_editor.placementModeEnabled;
        UIData.editorPlaceStatic = m_run.m_editor.placeStaticObject;
        UIData.editorTerrainAlign = m_run.m_editor.autoTerrainAlign;
        UIData.editorViewportLookActive = m_run.m_editor.viewportLookActive;
        UIData.editorObjectType = m_run.m_editor.objectType;
        UIData.canSaveSceneDefaults = m_run.SceneState().isSceneMode &&
                                      m_run.m_sceneRuntime.HasCurrentEntry() &&
                                      !m_run.m_sceneRuntime.CurrentPath()->empty();
        UIData.cinematicRendering = m_run.IsCinematicRenderingEnabled();
        UIData.ordinaryRender = Cfg().ordinaryRender;
        UIData.cinematic = m_run.ActiveCinematicConfig();
        {
            auto addPreview = [&]( const char* label, uint32_t textureHandle, int width, int height, bool available, bool depth, bool hdr )
            {
                if ( UIData.renderTargetPreviewCount >= SkullbonezCore::UI::UI_RENDER_TARGET_PREVIEW_MAX )
                {
                    return;
                }

                SkullbonezCore::UI::UIRenderTargetPreviewResource& preview = UIData.renderTargetPreviews[UIData.renderTargetPreviewCount++];
                preview.label = label;
                preview.textureHandle = textureHandle;
                preview.width = width;
                preview.height = height;
                preview.available = available && textureHandle != 0 && width > 0 && height > 0;
                preview.depth = depth;
                preview.hdr = hdr;
            };

            auto addFramebufferPreview = [&]( const char* label, const SkullbonezCore::Rendering::IFramebuffer* target, bool depth, bool available )
            {
                const uint32_t textureHandle = target ? ( depth ? target->GetDepthTextureHandle() : target->GetColorTextureHandle() ) : 0;
                const bool hdr = target && !depth && target->GetColorFormat() == SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;
                addPreview( label, textureHandle, target ? target->GetWidth() : 0, target ? target->GetHeight() : 0, available, depth, hdr );
            };

            const RunRenderPassResources& passes = m_run.m_systems.renderPasses;
            const bool shadowsAvailable = UIData.cinematicRendering ? UIData.cinematic.shadowsEnabled : UIData.ordinaryRender.shadowsEnabled;
            const bool cinematicTargetsAvailable = UIData.cinematicRendering;

            addFramebufferPreview( "Reflection Color", passes.reflection.target.get(), false, passes.reflection.target != nullptr );
            addFramebufferPreview( "Reflection Depth", passes.reflection.target.get(), true, passes.reflection.target != nullptr );
            addFramebufferPreview( "Terrain Shadow Depth", passes.shadows.terrainTarget.get(), true, shadowsAvailable );
            addFramebufferPreview( "Object Shadow Depth", passes.shadows.objectTarget.get(), true, shadowsAvailable );
            addFramebufferPreview( "Terrain Shadow Color", passes.shadows.terrainTarget.get(), false, shadowsAvailable );
            addFramebufferPreview( "Object Shadow Color", passes.shadows.objectTarget.get(), false, shadowsAvailable );
            addFramebufferPreview( "Cinematic Scene Color", passes.cinematicScene.hdrTarget.get(), false, cinematicTargetsAvailable );
            addFramebufferPreview( "Cinematic Scene Depth", passes.cinematicScene.hdrTarget.get(), true, cinematicTargetsAvailable );
            addFramebufferPreview( "Volumetric Color", passes.volumetricLight.target.get(), false, cinematicTargetsAvailable && UIData.cinematic.volumetricLightingEnabled );
            addFramebufferPreview( "Volumetric Depth", passes.volumetricLight.target.get(), true, cinematicTargetsAvailable && UIData.cinematic.volumetricLightingEnabled );

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
        Text2d::Render2dTextColor( panX0 + panPad, panY1 - panPad - titleSz, titleSz, 1.0f, 0.85f, 0.35f, "SCENE TELEMETRY" );
        Text2d::Render2dTextColor( panX0 + panPad, panY1 - panPad - titleSz - lineH, entrySz, 0.85f, 0.85f, 0.85f, "Model Count: %d", m_run.SceneState().modelCount );
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
    if ( m_run.m_debug.overlayMode == OverlayMode::BarsNormalized || m_run.m_debug.overlayMode == OverlayMode::BarsAbsolute )
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
        Profiler::Instance().RenderOverlay( -( hw - mX ), -( hh - mY ) - padY, lineH, profFSz, m_run.m_timers.rollingFpsTime );
    }
#endif

    m_run.RenderReplayScrubberOverlay();
    {
        DRAW_CALL_TRACE_SCOPE( "ProfilerOverlay" );
        Text2d::FlushText();
    }
}
