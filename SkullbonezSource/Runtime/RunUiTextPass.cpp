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
  Runtime mode badge: Small top-left label that names the current camera/input
    workspace, such as Inspect or Manipulator.
  Text-only mode: Validation mode that skips world rendering and renders glyphs
    on a solid background to isolate text output.
  UI frame data: Borrowed per-frame snapshot passed to the immediate-mode UI.

Invariants:
  - Font resources are created once through EnsureGpuResources and released
    before backend teardown.
  - Render flushes Text2d before returning, so callers do not inherit queued UI
    glyphs into later frame work.

Related:
  - SkullbonezSource/Runtime/RunInternal.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "../Core/WorkerPool.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Basics::RunInternal;

void UiTextPass::EnsureGpuResources()
{
    Text2d::BuildFont( "Verdana" );
}


void UiTextPass::ReleaseGpuResources()
{
    Text2d::DeleteFont();
}


bool UiTextPass::ShouldRender() const
{
    return m_host.m_debug.isTextOnly || !m_host.SceneState().isSceneMode || m_host.SceneState().isSceneText ||
           m_host.m_debug.overlayMode != OverlayMode::None || m_host.m_UI.IsVisible() ||
           m_host.ShouldRenderReplayScrubber() || m_host.ReplayPathVisualizerHasTarget() ||
           ( m_host.m_camera.mode != RunCameraMode::Demo && m_host.m_camera.mode != RunCameraMode::Scene );
}


void UiTextPass::Render( const UiTextPassInputs& inputs )
{
    const int uiPassDrawCallStart = inputs.renderDiagnostics.GetFrameDrawCallCount();

    // Invariant: rolling diagnostics update before any overlay early return so
    // FPS, physics time, render time, and scene energy age at the same cadence.
    m_host.m_timers.updateTimer.StopTimer();
    m_host.m_timers.timeSinceLastRender += static_cast<float>( m_host.m_timers.updateTimer.GetElapsedTime() );
    m_host.m_timers.updateTimer.StartTimer();

    const double currentSceneEnergy = m_host.m_cGameModelCollection.GetSceneKineticEnergy();
    m_host.m_timers.sceneEnergyAccumulator += currentSceneEnergy;
    ++m_host.m_timers.sceneEnergySampleCount;

    if ( m_host.m_timers.timeSinceLastRender > 0.5f )
    {
        if ( inputs.secondsPerFrame )
        {
            m_host.m_timers.rollingFpsTime = 1.0f / static_cast<float>( inputs.secondsPerFrame );
            m_host.m_timers.rollingPhysicsTime = m_host.m_timers.physicsTime;
            m_host.m_timers.rollingRenderTime = m_host.m_timers.renderTime;
        }
        if ( m_host.m_timers.sceneEnergySampleCount > 0 )
        {
            m_host.m_timers.rollingSceneEnergy =
                static_cast<float>( m_host.m_timers.sceneEnergyAccumulator /
                                    static_cast<double>( m_host.m_timers.sceneEnergySampleCount ) );
            m_host.m_timers.sceneEnergyAccumulator = 0.0;
            m_host.m_timers.sceneEnergySampleCount = 0;
        }
        m_host.m_timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = m_host.m_timers.rollingSceneEnergy;
    if ( m_host.m_timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( m_host.m_timers.sceneEnergyAccumulator /
                                                    static_cast<double>( m_host.m_timers.sceneEnergySampleCount ) );
    }

    const char* rendererName = inputs.renderDiagnostics.GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler
    if ( m_host.m_debug.isTextOnly )
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

    const auto renderRuntimeModeBadge = [&]()
    {
        // Why: clean validation/look-dev captures use --hide-top-text to remove
        // top-left chrome without changing scene simulation or camera state.
        if ( m_host.m_debug.isTopTextHidden )
        {
            return;
        }
        if ( m_host.m_camera.mode == RunCameraMode::Demo || m_host.m_camera.mode == RunCameraMode::Scene )
        {
            return;
        }

        char modeLine[128] = {};
        sprintf_s( modeLine, sizeof( modeLine ), "MODE  %s", m_host.CameraModeLabel( m_host.m_camera.mode ) );

        const char* detail = "RMB look   WASD move";
        float accentR = 0.66f;
        float accentG = 0.88f;
        float accentB = 1.0f;
        if ( m_host.m_camera.mode == RunCameraMode::Attach )
        {
            detail = "LMB target   RMB orbit   Wheel distance   F1 mode   Enter pin";
            accentR = 0.16f;
            accentG = 1.0f;
            accentB = 0.92f;
        }
        else if ( m_host.m_camera.mode == RunCameraMode::Manipulator )
        {
            detail = "LMB drag object   Hold Space play";
            accentR = 0.18f;
            accentG = 0.94f;
            accentB = 1.0f;
        }
        else if ( m_host.m_camera.mode == RunCameraMode::Launcher )
        {
            detail = "LMB fire   M fire mode";
        }
        else if ( m_host.m_camera.mode == RunCameraMode::Inspect )
        {
            detail = "RMB look   WASD move   Hold Space play";
        }

        const float titleSz = 0.013f;
        const float detailSz = 0.0105f;
        const float pad = 0.010f;
        const float lineGap = 0.020f;
        const float textW =
            (std::max)( Text2d::MeasureText( titleSz, modeLine ), Text2d::MeasureText( detailSz, detail ) );
        const float panelW = textW + pad * 2.0f;
        const float panelH = pad * 2.0f + titleSz + lineGap;
        const float x0 = -( hw - mX );
        const float y1 = hh - mY;
        const float y0 = y1 - panelH;
        Text2d::Render2dQuad( x0, y0, x0 + panelW, y1, 0.018f, 0.024f, 0.032f, 0.78f );
        Text2d::Render2dQuad( x0, y0, x0 + 0.004f, y1, accentR, accentG, accentB, 0.92f );
        Text2d::Render2dTextColor( x0 + pad, y1 - pad - titleSz, titleSz, accentR, accentG, accentB, "%s", modeLine );
        Text2d::Render2dTextColor( x0 + pad,
                                   y1 - pad - titleSz - lineGap,
                                   detailSz,
                                   0.86f,
                                   0.90f,
                                   0.92f,
                                   "%s",
                                   detail );
    };

    renderRuntimeModeBadge();

    // Crosshair - always visible when launcher mode is active, regardless of overlay state.
    // A tiny center gap keeps the target visible instead of covering it.
    if ( m_host.IsLauncherCameraMode() )
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
        const char* fireModeLabel = m_host.LauncherFireModeLabel();
        const float modeSz = 0.011f;
        const float modeW = Text2d::MeasureText( modeSz, fireModeLabel );
        Text2d::Render2dTextColor( -modeW * 0.5f, -0.048f, modeSz, 0.72f, 0.94f, 1.0f, "%s", fireModeLabel );
#ifdef _DEBUG
        if ( m_host.m_debug.reproSnapshotMessage[0] != '\0' &&
             m_host.m_timers.simulationTimer.GetTimeSinceLastStart() <= m_host.m_debug.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, m_host.m_debug.reproSnapshotMessage );
            Text2d::Render2dTextColor( -msgW * 0.5f,
                                       -0.065f,
                                       msgSz,
                                       0.65f,
                                       0.92f,
                                       1.0f,
                                       "%s",
                                       m_host.m_debug.reproSnapshotMessage );
        }
#endif
    }

    m_host.RefreshRuntimeViewModel();
    const RuntimeViewModel& view = m_host.m_runtimeViewModel;

    const char* sceneName = "";
    if ( view.sceneMode && m_host.m_sceneController.HasCurrentEntry() )
    {
        sceneName = FileNameFromPath( m_host.m_sceneController.CurrentPath()->c_str() );
    }

    if ( m_host.m_UI.IsVisible() )
    {
        PROFILE_BEGIN( "Frame/UI/BuildData" );
        InGameUIFrameData UIData;
        UIData.screenW = m_host.WindowScreenWidth();
        UIData.screenH = m_host.WindowScreenHeight();
        if ( m_host.m_debug.isUITestPattern )
        {
            DrawUITestPattern( UIData.screenW, UIData.screenH );
        }
        UIData.rendererName = rendererName;
        UIData.sceneName = sceneName;
        UIData.sceneOptions = m_host.m_sceneBrowser.namePtrs.empty() ? nullptr : m_host.m_sceneBrowser.namePtrs.data();
        UIData.sceneOptionCount = static_cast<int>( m_host.m_sceneBrowser.namePtrs.size() );
        UIData.selectedSceneOption = m_host.CurrentSceneBrowserIndex();
        UIData.selectedCineModeSceneOption = m_host.m_sceneBrowser.selectedCineModeSceneIndex;
        UIData.UIDrawCalls = m_host.m_timers.lastUIDrawCalls;
        UIData.fps =
            m_host.m_timers.rollingFpsTime > 0.0f
                ? m_host.m_timers.rollingFpsTime
                : ( inputs.secondsPerFrame > 0.0 ? 1.0f / static_cast<float>( inputs.secondsPerFrame ) : 0.0f );
        UIData.renderMs = ( m_host.m_timers.rollingRenderTime > 0.0f ? m_host.m_timers.rollingRenderTime
                                                                     : m_host.m_timers.renderTime ) *
                          1000.0f;
        UIData.physicsMs = ( m_host.m_timers.rollingPhysicsTime > 0.0f ? m_host.m_timers.rollingPhysicsTime
                                                                       : m_host.m_timers.physicsTime ) *
                           1000.0f;
        UIData.cpuFrameMs = m_host.m_timers.cpuFrameWorkMs;
        UIData.gpuFrameMs = m_host.m_timers.gpuFrameWorkMs;
        UIData.modelCount = view.modelCount;
        UIData.modelCapacity = ActiveGameModelCapacity();
        UIData.workerThreadCount = SkullbonezCore::Threading::WorkerPool::Instance().GetThreadCount();
        UIData.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        UIData.currentFrame = view.frame;
        UIData.targetFrameCount = view.targetFrameCount;
        UIData.rngSeed = m_host.SceneState().rngSeed;
        UIData.solverBallCount = m_host.SceneState().solverBallCount;
        UIData.solverBoxCount = m_host.SceneState().solverBoxCount;
        UIData.currentSceneIndex = view.sceneIndex;
        UIData.sceneCount = view.sceneCount;
        UIData.now = m_host.m_timers.simulationTimer.GetTotalTime();
        if ( m_host.m_UI.GetActiveTab() == InGameUITab::Profiler )
        {
            UIData.mainMemory = m_host.RefreshMainMemoryStats( UIData.now );
        }
        UIData.sceneMode = view.sceneMode;
        UIData.scenePhysicsEnabled = view.scenePhysics;
        UIData.sceneTextEnabled = view.sceneText;
        UIData.textOnly = m_host.m_debug.isTextOnly;
        UIData.fixedStep = view.fixedStep;
        UIData.exitOnComplete = m_host.SceneState().isExitOnComplete;
        UIData.testComplete = m_host.SceneState().isTestComplete;
        UIData.vsyncEnabled = m_host.m_runtimeSettings.isVsyncEnabled;
        UIData.pipelineSyncEnabled = m_host.m_runtimeSettings.isPipelineSyncEnabled;
        UIData.sceneEnergy = sceneEnergyForDisplay;
        UIData.timeScale = view.timeScale;
        UIData.trackHeight = m_host.m_camera.trackBallIndex >= 0 ? m_host.m_camera.trackHeight : 0.0f;
        UIData.autoCycleInterval = m_host.m_camera.autoCycleInterval > 0.0f ? m_host.m_camera.autoCycleInterval : 0.0f;
        UIData.worldGravity = m_host.m_cWorldEnvironment.GetGravity();
        UIData.worldFluidHeight = m_host.m_cWorldEnvironment.GetFluidSurfaceHeight();
        UIData.worldFluidDensity = m_host.m_cWorldEnvironment.GetFluidDensity();
        UIData.physicsDebugFlags = m_host.m_debug.physicsDebugFlags;
        {
            const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
            int stageIndex = stageCount > 0 ? m_host.m_debug.physicsDebugPipelineStageCursor % stageCount : 0;
            if ( stageIndex < 0 )
            {
                stageIndex += stageCount;
            }
            UIData.physicsPipelineStageName =
                PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );
            UIData.physicsPipelineStageIndex = stageIndex;
            UIData.physicsPipelineStageCount = stageCount;
        }
        UIData.physicsDebugAlpha = m_host.m_debug.physicsDebugAlpha;
        UIData.physicsDebugContactLinger = m_host.m_debug.physicsDebugContactLinger;
        UIData.physicsSleepEnabled = m_host.m_runtimeSettings.isPhysicsSleepEnabled;
        UIData.collisionVisualizer = m_host.m_debug.isCollisionVisualizer;
        UIData.physicsDebugTransparent = m_host.m_debug.isPhysicsDebugTransparent;
        UIData.broadphaseOverlay = m_host.m_debug.isBroadphaseOverlay;
        UIData.tornadoEnabled = m_host.m_runtimeSettings.tornadoField.enabled;
        UIData.tornadoVisualShell =
            m_host.m_runtimeSettings.tornadoVisual.enabled && m_host.m_runtimeSettings.tornadoField.enabled;
        UIData.tornadoFieldVectors = m_host.m_runtimeSettings.tornadoField.visualizeVelocityField;
        UIData.tornadoRadius = m_host.m_runtimeSettings.tornadoField.radius;
        UIData.tornadoHeight = m_host.m_runtimeSettings.tornadoField.height;
        UIData.tornadoInwardAcceleration = m_host.m_runtimeSettings.tornadoField.inwardAcceleration;
        UIData.tornadoSwirlAcceleration = m_host.m_runtimeSettings.tornadoField.swirlAcceleration;
        UIData.tornadoLiftAcceleration = m_host.m_runtimeSettings.tornadoField.liftAcceleration;
        UIData.rayCastVisualization = m_host.m_rayCastTest.visualizeRays;
        UIData.rayCastImpulseStrength = m_host.m_rayCastTest.impulseStrength;
        UIData.launcherProjectileSpeed = m_host.m_rayCastTest.projectileSpeed;
        UIData.waterFreezeDebug = m_host.m_debug.isWaterFreezeDebug;
        UIData.waterFlatDebug = m_host.m_debug.isWaterFlatDebug;
        UIData.terrainHidden = m_host.m_debug.isTerrainHidden;
        UIData.waterHidden = m_host.m_debug.isWaterHidden;
        UIData.waterNoReflect = m_host.m_debug.isWaterNoReflect;
        UIData.waterRTReflect = m_host.m_debug.isWaterRTReflect;
        const RuntimeInputMode runtimeInputMode = m_host.m_runtimeInput.CurrentMode();
        UIData.cameraModeIndex = static_cast<int>( m_host.m_camera.mode );
        UIData.cameraModeEnabledMask = m_host.CameraModeEnabledMask();
        UIData.runtimeInputModeLabel = m_host.CameraModeLabel( m_host.m_camera.mode );
        UIData.cameraMouseActive =
            ( runtimeInputMode == RuntimeInputMode::FlyCamera || runtimeInputMode == RuntimeInputMode::Launcher ||
              runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
            !m_host.m_UI.BlocksCameraMouse();
        UIData.nativeCursorVisible = !UIData.cameraMouseActive;
        UIData.editorModeEnabled = m_host.m_editor.editorModeEnabled;
        UIData.editorPlacementMode = m_host.m_editor.placementModeEnabled;
        UIData.editorPlaceStatic = m_host.m_editor.placeStaticObject;
        UIData.editorTerrainAlign = m_host.m_editor.autoTerrainAlign;
        UIData.editorViewportLookActive = m_host.m_editor.viewportLookActive;
        UIData.editorObjectType = m_host.m_editor.objectType;
        UIData.canSaveSceneDefaults = view.sceneMode && m_host.m_sceneController.HasCurrentEntry() &&
                                      !m_host.m_sceneController.CurrentPath()->empty();
        UIData.cinematicRendering = m_host.IsCinematicRenderingEnabled();
        UIData.ordinaryRender = Cfg().ordinaryRender;
        UIData.cinematic = m_host.ActiveCinematicConfig();
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

            const RunRenderPassResources& passes = m_host.m_systems.renderPasses;
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

            const uint32_t dxrReflection =
                inputs.renderRayTracing ? inputs.renderRayTracing->GetReflectionUAVTexture() : 0;
            addPreview( "DXR Reflection",
                        dxrReflection,
                        m_host.WindowScreenWidth() * 2,
                        m_host.WindowScreenHeight() * 2,
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
        m_host.m_UI.Draw( UIData );
        PROFILE_BEGIN( "Frame/UI/PostFlushText" );
        {
            DRAW_CALL_TRACE_SCOPE( "Frame/UI/PostFlushText" );
            Text2d::FlushText();
        }
        PROFILE_END( "Frame/UI/PostFlushText" );
        m_host.RenderReplayScrubberOverlay();
        return;
    }

    // --- Overlay: None ---
    if ( m_host.m_debug.overlayMode == OverlayMode::None )
    {
        m_host.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "HUD" );
            Text2d::FlushText();
        }
        return;
    }

    // --- Overlay: Scene telemetry ---
    if ( m_host.m_debug.overlayMode == OverlayMode::SceneStats )
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
                                   m_host.SceneState().modelCount );
        Text2d::Render2dTextColor( panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH * 2.0f,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Scene Energy: %.6f",
                                   sceneEnergyForDisplay );
        m_host.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "SceneStats" );
            Text2d::FlushText();
        }
        return;
    }

    // --- Overlay: Visual profiler bars (normalized or absolute) ---
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( m_host.m_debug.overlayMode == OverlayMode::BarsNormalized ||
         m_host.m_debug.overlayMode == OverlayMode::BarsAbsolute )
    {
        // Panel anchored bottom-left, filling most of the width. Height kept modest - leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height
        const float panX = -( hw - mX ) + mX * 0.5f;   // slight left margin
        const float panY = -( hh - mY ) + mY * 0.5f;   // slight bottom margin
        const bool absolute = ( m_host.m_debug.overlayMode == OverlayMode::BarsAbsolute );
        Profiler::Instance().RenderBarOverlay( panX, panY, panW, panH, absolute );
        m_host.RenderReplayScrubberOverlay();
        {
            DRAW_CALL_TRACE_SCOPE( "ProfilerBars" );
            Text2d::FlushText();
        }
        return;
    }
#endif

    // --- Overlay: Keys reference screen (compact, bottom-left) ---
    if ( m_host.m_debug.overlayMode == OverlayMode::Keys )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.011f;
        const float lineH = 0.020f;
        const int nRows = 14;
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
        };
        static const KeyEntry kRight[nRows] = {
            { "Esc", "Min/expand UI" },
            { "Esc Esc", "Quit" },
            { "Q", "Renderer notice" },
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

        m_host.RenderReplayScrubberOverlay();
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
                                            m_host.m_timers.rollingFpsTime );
    }
#endif

    m_host.RenderReplayScrubberOverlay();
    {
        DRAW_CALL_TRACE_SCOPE( "ProfilerOverlay" );
        Text2d::FlushText();
    }
}
