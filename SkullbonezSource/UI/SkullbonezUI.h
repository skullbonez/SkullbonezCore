#pragma once

#include "../SkullbonezCommon.h"
#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UIBackdropBlur.h"
#include "UIScrollBar.h"
#include "UISlider.h"
#include "UIState.h"
#include "UITabBar.h"
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
    Physics,
    Options,
    Keys,
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
    int drawCallsBeforeUI = 0;
    int UIDrawCalls = 0;
    float fps = 0.0f;
    float renderMs = 0.0f;
    float physicsMs = 0.0f;
    float cpuFrameMs = 0.0f;
    float gpuFrameMs = 0.0f;
    int modelCount = 0;
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
    bool waterFreezeDebug = false;
    bool waterFlatDebug = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterNoReflect = false;
    bool waterRTReflect = false;
    bool cameraMouseActive = false;
    bool nativeCursorVisible = false;
    bool canSaveSceneDefaults = false;
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
    bool m_profilerTimelineEnabled = false;
    bool m_performanceHistogramEnabled = false;
    InGameUITab m_activeTab = InGameUITab::Profiler;
    UITabBar m_tabBar;
    UICheckBox m_blurToggle;
    UICheckBox m_vsyncToggle;
    UICheckBox m_timelineToggle;
    UICheckBox m_histogramToggle;
    UICheckBox m_physicsToggles[8];
    UIRect m_pipelinePrevButton;
    UIRect m_pipelineNextButton;
    UICheckBox m_optionToggles[5];
    UISlider m_timeScaleSlider;
    UISlider m_modelCountSlider;
    UISlider m_physicsAlphaSlider;
    UISlider m_contactLingerSlider;
    UISlider m_seedSlider;
    UISlider m_solverBallSlider;
    UISlider m_solverBoxSlider;
    UISlider m_worldGravitySlider;
    UISlider m_worldFluidHeightSlider;
    UISlider m_worldFluidDensitySlider;
    UIButton m_resetSceneButton;
    UIButton m_resetDefaultsButton;
    UIButton m_saveDefaultsButton;
    UIComboBox m_rendererCombo;
    UIComboBox m_reflectionCombo;
    UIComboBox m_sceneCombo;
    UIBackdropBlur m_backdropBlur;
    UIScrollBar m_scrollBar;
    int m_mouseX = 0;
    int m_mouseY = 0;
    int m_lastScreenW = 1;
    int m_lastScreenH = 1;
    int m_lastRendererIndex = 0;
    int m_lastSolverBallCount = 0;
    int m_lastSolverBoxCount = 0;
    bool m_hasMouseOverride = false;
    int m_mouseOverrideX = 0;
    int m_mouseOverrideY = 0;
    SceneTab::UISceneTabState m_sceneTab;
    float m_scrollY = 0.0f;
    double m_scrollbarVisibleUntil = 0.0;
    int m_activeSlider = 0; // 0=none; other values map to Controls/Options sliders in SkullbonezUI.cpp
    float m_previewTimeScale = -1.0f;
    int m_previewModelCount = -1;
    float m_previewPhysicsAlpha = -1.0f;
    float m_previewContactLinger = -1.0f;
    int m_previewSolverBallCount = -1;
    int m_previewSolverBoxCount = -1;
    uint32_t m_expandedProfilerHashes[64] = {};
    int m_expandedProfilerHashCount = 0;
    bool m_expandAllProfilerMarkers = false;
    bool m_profilerDefaultExpansionApplied = false;
    struct PerformanceHistogramSample
    {
        float cpuMs = 0.0f;
        float gpuMs = 0.0f;
        float spikeMs = 0.0f;
    };
    static constexpr int PERFORMANCE_HISTOGRAM_SAMPLE_COUNT = 120;
    PerformanceHistogramSample m_performanceHistogramSamples[PERFORMANCE_HISTOGRAM_SAMPLE_COUNT] = {};
    int m_performanceHistogramHead = 0;
    int m_performanceHistogramCount = 0;
    float m_performanceHistogramAxisMs = 16.67f;

    struct ProfilerTimelineSegment
    {
        float startMs = 0.0f;
        float durationMs = 0.0f;
        bool isFilled = false;
    };

    int ContentHeight() const;
    int BuildVisibleProfilerRows( int* rows, int maxRows ) const;
    void BuildProfilerTimelineSegments( const int* rows, int rowCount, ProfilerTimelineSegment* segments ) const;
    bool IsProfilerMarkerExpanded( uint32_t hash ) const;
    void ToggleProfilerMarker( uint32_t hash );
    void ApplyProfilerDefaultExpansion();
    void ApplyProfilerExpandAll();
    void PushPerformanceHistogramSample( float cpuMs, float gpuMs );
    void DrawPerformanceHistogram( const UIDrawContext& draw, const InGameUIFrameData& data ) const;
    void DrawCursor( const UIDrawContext& draw ) const;
    void CloseSceneCombo();
    void SetMaximized( bool maximized, int screenW, int screenH, double now = 0.0 );
};

} // namespace UI
} // namespace SkullbonezCore
