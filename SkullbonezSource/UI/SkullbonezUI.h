/*
File: SkullbonezSource/UI/SkullbonezUI.h
Purpose:
  Implements SkullbonezUI widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/SkullbonezUI.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../SkullbonezCommon.h"
#include "../SkullbonezConfig.h"
#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UICache.h"
#include "UIBackdropBlur.h"
#include "UIScrollBar.h"
#include "UISlider.h"
#include "UIState.h"
#include "UITabBar.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

enum class InGameUITab
{
    Profiler,
    Scene,
    Editor,
    Physics,
    Options,
    Render,
    Keys,
    Cinematic,
    Count
};

// Snapshot of engine state needed to draw the UI for one frame.  The UI reads
// this structure but does not mutate engine objects directly; that keeps render
// code, input hit-testing, and runtime state changes separated.
struct InGameUIFrameData
{
    int screenW = 1;
    int screenH = 1;
    const char* rendererName = "";
    const char* sceneName = "";
    const char* const* sceneOptions = nullptr;
    int sceneOptionCount = 0;
    int selectedSceneOption = -1;
    int selectedCineModeSceneOption = -1;
    int drawCallsBeforeUI = 0;
    int UIDrawCalls = 0;
    float fps = 0.0f;
    float renderMs = 0.0f;
    float physicsMs = 0.0f;
    float cpuFrameMs = 0.0f;
    float gpuFrameMs = 0.0f;
    int modelCount = 0;
    int modelCapacity = DEFAULT_GAME_MODEL_CAPACITY;
    int workerThreadCount = 0;
    int maxWorkerThreadCount = 1;
    int currentFrame = 0;
    int targetFrameCount = -1;
    unsigned int rngSeed = 0;
    int solverBallCount = 0;
    int solverBoxCount = 0;
    int currentSceneIndex = -1;
    int sceneCount = 0;
    double now = 0.0;
    bool sceneMode = false;
    bool scenePhysicsEnabled = true;
    bool sceneTextEnabled = true;
    bool textOnly = false;
    bool fixedStep = false;
    bool exitOnComplete = false;
    bool testComplete = false;
    bool vsyncEnabled = false;
    bool pipelineSyncEnabled = false;
    float sceneEnergy = 0.0f;
    float timeScale = 1.0f;
    float trackHeight = 0.0f;
    float autoCycleInterval = 0.0f;
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    uint32_t physicsDebugFlags = 0;
    const char* physicsPipelineStageName = "";
    int physicsPipelineStageIndex = 0;
    int physicsPipelineStageCount = 0;
    float physicsDebugAlpha = 0.0f;
    float physicsDebugContactLinger = 0.0f;
    bool physicsSleepEnabled = true;
    bool collisionVisualizer = false;
    bool physicsDebugTransparent = false;
    bool broadphaseOverlay = false;
    bool tornadoEnabled = false;
    bool tornadoFieldVectors = false;
    float tornadoRadius = 0.0f;
    float tornadoHeight = 0.0f;
    float tornadoInwardAcceleration = 0.0f;
    float tornadoSwirlAcceleration = 0.0f;
    float tornadoLiftAcceleration = 0.0f;
    bool waterFreezeDebug = false;
    bool waterFlatDebug = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterNoReflect = false;
    bool waterRTReflect = false;
    bool cameraMouseActive = false;
    bool nativeCursorVisible = false;
    bool editorFixedPlacementEnabled = false;
    bool editorViewportLookActive = false;
    bool canSaveSceneDefaults = false;
    bool cinematicRendering = false;
    Basics::OrdinaryRenderConfig ordinaryRender;
    Basics::CinematicRenderConfig cinematic;
};

class InGameUI
{
  public:
    bool IsVisible() const;
    void SetVisible( bool visible, double now = 0.0 );
    void ToggleVisible( double now );
    void SetMinimized( bool minimized, double now = 0.0 );
    void ToggleMaximizeMinimize( int screenW, int screenH, double now );
    void SetActiveTab( InGameUITab tab );
    InGameUITab GetActiveTab() const;
    bool BlocksCameraMouse() const;
    bool BlocksKeyboard() const;
    bool WantsNativeMouseCursor() const;
    void SetWindowBounds( int x, int y, int width, int height );
    void SetBlurEnabled( bool enabled );
    void SetRendererComboOpen( bool open );
    void SetWaterComboOpen( bool open );
    void SetSceneComboOpen( bool open );
    void SetSceneFilter( const char* filter );
    void SetProfilerExpandAll( bool expandAll );
    void SetProfilerTimelineEnabled( bool enabled );
    void SetPerformanceHistogramEnabled( bool enabled );
    void SetHitboxOverlayEnabled( bool enabled );
    void SetScrollY( float scrollY );
    void SetMouseOverride( bool enabled, int x = 0, int y = 0 );
    void CancelInputCapture();
    void ResetResources();

    InGameUIInputResult UpdateInput( HWND hwnd, int screenW, int screenH, double now, const char* const* sceneOptions = nullptr, int sceneOptionCount = 0, int selectedSceneOption = -1 );
    void Draw( const InGameUIFrameData& data );

  private:
    // Persistent widget state.  Scene loads may apply SceneUIOptions, but normal
    // simulation resets preserve these values so the UI remains where the user
    // left it while the bodies/timers are rebuilt underneath.
    UIWindowState m_window;
    UIInteractionState m_interaction;
    bool m_blurPreviewEnabled = false;
    InGameUITab m_activeTab = InGameUITab::Scene;
    UITabBar m_tabBar;
    UICheckBox m_blurToggle;
    UICheckBox m_vsyncToggle;
    UICheckBox m_timelineToggle;
    UICheckBox m_histogramToggle;
    UICheckBox m_hitboxToggle;
    UIButton m_resetSceneButton;
    UIButton m_resetDefaultsButton;
    UIButton m_saveDefaultsButton;
    UIComboBox m_rendererCombo;
    UIComboBox m_reflectionCombo;
    UIComboBox m_sceneCombo;
    UIComboBox m_cineSceneCombo;
    UICheckBox m_cinematicMasterToggle;
    UICheckBox m_renderShadowToggle;
    UIButton m_saveRenderDefaultsButton;
    UISlider m_renderSliders[static_cast<int>( UIRenderParam::Count )];
    UICheckBox m_cinematicFeatureToggles[static_cast<int>( UICinematicFeature::Count )];
    UISlider m_cinematicSliders[static_cast<int>( UICinematicParam::Count )];
    UIBackdropBlur m_backdropBlur;
    UICacheState m_cache;
    UIScrollBar m_scrollBar;
    int m_mouseX = 0;
    int m_mouseY = 0;
    int m_lastScreenW = 1;
    int m_lastScreenH = 1;
    int m_lastModelCapacity = DEFAULT_GAME_MODEL_CAPACITY;
    int m_lastSolverBallCount = 0;
    int m_lastSolverBoxCount = 0;
    int m_lastWorkerThreadCount = 0;
    int m_lastMaxWorkerThreadCount = 1;
    bool m_hasMouseOverride = false;
    int m_mouseOverrideX = 0;
    int m_mouseOverrideY = 0;
    ControlsTab::UIControlsTabState m_controlsTab;
    EditorTab::UIEditorTabState m_editorTab;
    OptionsTab::UIOptionsTabState m_optionsTab;
    PhysicsTab::UIPhysicsTabState m_physicsTab;
    ProfilerTab::UIProfilerTabState m_profilerTab;
    SceneTab::UISceneTabState m_sceneTab;
    float m_scrollY = 0.0f;
    double m_scrollbarVisibleUntil = 0.0;
    int m_activeSlider = 0; // 0=none; other values map to Controls/Options sliders in SkullbonezUI.cpp
    bool m_hitboxOverlayEnabled = false;

    int ContentHeight() const;
    void DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data, const UIRect& windowBounds, const UIRect& contentBounds, const UIRect& footerBounds ) const;
    void CloseSceneCombo();
    void SetMaximized( bool maximized, int screenW, int screenH, double now = 0.0 );
};

} // namespace UI
} // namespace SkullbonezCore
