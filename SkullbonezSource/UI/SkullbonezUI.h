#pragma once

#include "../SkullbonezCommon.h"
#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UIBackdropBlur.h"
#include "UIScrollBar.h"
#include "UISlider.h"
#include "UITabBar.h"
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
    bool legacyPhysics = false;
    bool fixedStep = false;
    bool exitOnComplete = false;
    bool testComplete = false;
    bool vsyncEnabled = false;
    bool pipelineSyncEnabled = false;
    bool rollAlignEnabled = true;
    float sceneEnergy = 0.0f;
    float timeScale = 1.0f;
    float trackHeight = 0.0f;
    float autoCycleInterval = 0.0f;
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    uint32_t physicsDebugFlags = 0;
    float physicsDebugAlpha = 0.0f;
    float physicsDebugContactLinger = 0.0f;
    bool collisionVisualizer = false;
    bool physicsDebugTransparent = false;
    bool debugVectors = false;
    bool broadphaseOverlay = false;
    bool waterFreezeDebug = false;
    bool waterFlatDebug = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterNoReflect = false;
    bool waterRTReflect = false;
    bool canSaveSceneDefaults = false;
};

struct InGameUIInputResult
{
    bool userInteracted = false;
    bool toggleVsync = false;
    bool toggleCollisionVisualizer = false;
    bool togglePhysicsDebugTransparent = false;
    bool toggleDebugVectors = false;
    bool toggleBroadphaseOverlay = false;
    bool toggleScenePhysics = false;
    bool toggleSceneText = false;
    bool toggleTextOnly = false;
    bool toggleFixedStep = false;
    bool toggleExitOnComplete = false;
    bool toggleRollAlign = false;
    bool toggleTerrainHidden = false;
    bool toggleWaterHidden = false;
    bool toggleWaterFreeze = false;
    bool toggleWaterFlat = false;
    bool toggleWaterReflection = false;
    bool resetScene = false;
    bool resetSceneDefaults = false; // Scene-tab command: reload current scene/config defaults instead of preserving live run controls
    bool requestDemoScene = false;
    bool saveSceneDefaults = false;
    float requestedTimeScale = -1.0f;
    float requestedPhysicsDebugAlpha = -1.0f;
    float requestedPhysicsDebugContactLinger = -1.0f;
    float requestedTrackHeight = -1.0f;
    float requestedAutoCycleInterval = -1.0f;
    int requestedModelCount = -1;
    int requestedFrameCount = -1;
    int requestedSeed = -1;
    int requestedSolverBallCount = -1;
    int requestedSolverBoxCount = -1;
    bool requestWorldGravity = false;
    bool requestWorldFluidHeight = false;
    bool requestWorldFluidDensity = false;
    float requestedWorldGravity = 0.0f;
    float requestedWorldFluidHeight = 0.0f;
    float requestedWorldFluidDensity = 0.0f;
    uint32_t togglePhysicsDebugFlags = 0;
    int requestedRendererIndex = -1; // 0=GL, 1=DX11, 2=DX12, -1=no request
    int requestedWaterReflectionMode = -1; // 0=FBO, 1=DXR, 2=None, -1=no request
    int requestedPhysicsMode = -1;         // 0=legacy, 1=solver, -1=no request
    int requestedSceneIndex = -1;          // index into sceneOptions, -1=no request
};

class InGameUI
{
  public:
    bool IsVisible() const;
    void SetVisible( bool visible, double now = 0.0 );
    void ToggleVisible( double now );
    void SetMinimized( bool minimized, double now = 0.0 );
    void SetActiveTab( InGameUITab tab );
    InGameUITab GetActiveTab() const;
    bool BlocksCameraMouse() const;
    bool BlocksKeyboard() const;
    void SetWindowBounds( int x, int y, int width, int height );
    void SetBlurEnabled( bool enabled );
    void SetRendererComboOpen( bool open );
    void SetSceneComboOpen( bool open );
    void SetSceneFilter( const char* filter );
    void SetProfilerExpandAll( bool expandAll );
    void SetProfilerTimelineEnabled( bool enabled );
    void SetPerformanceHistogramEnabled( bool enabled );
    void SetScrollY( float scrollY );
    void SetMouseOverride( bool enabled, int x = 0, int y = 0 );
    void ResetResources();

    InGameUIInputResult UpdateInput( HWND hwnd, int screenW, int screenH, double now, const char* const* sceneOptions = nullptr, int sceneOptionCount = 0, int selectedSceneOption = -1 );
    void Draw( const InGameUIFrameData& data );

  private:
    bool m_isVisible = true;
    bool m_isMinimized = true;
    bool m_isMaximized = false;
    bool m_leftWasDown = false;
    bool m_isDragging = false;
    bool m_isResizing = false;
    bool m_blocksCameraMouse = false;
    bool m_blurPreviewEnabled = false;
    bool m_profilerTimelineEnabled = false;
    bool m_performanceHistogramEnabled = false;
    InGameUITab m_activeTab = InGameUITab::Profiler;
    UITabBar m_tabBar;
    UICheckBox m_blurToggle;
    UICheckBox m_vsyncToggle;
    UICheckBox m_timelineToggle;
    UICheckBox m_histogramToggle;
    UICheckBox m_physicsToggles[7];
    UICheckBox m_optionToggles[9];
    UICheckBox m_controlToggles[17];
    UISlider m_timeScaleSlider;
    UISlider m_modelCountSlider;
    UISlider m_physicsAlphaSlider;
    UISlider m_contactLingerSlider;
    UISlider m_frameCountSlider;
    UISlider m_seedSlider;
    UISlider m_solverBallSlider;
    UISlider m_solverBoxSlider;
    UISlider m_trackHeightSlider;
    UISlider m_autoCycleSlider;
    UISlider m_worldGravitySlider;
    UISlider m_worldFluidHeightSlider;
    UISlider m_worldFluidDensitySlider;
    UIButton m_resetSceneButton;
    UIButton m_resetDefaultsButton;
    UIButton m_demoSceneButton;
    UIButton m_saveDefaultsButton;
    UIComboBox m_rendererCombo;
    UIComboBox m_reflectionCombo;
    UIComboBox m_physicsModeCombo;
    UIComboBox m_sceneCombo;
    UIBackdropBlur m_backdropBlur;
    UIScrollBar m_scrollBar;
    int m_x = 34;
    int m_y = 56;
    int m_width = 760;
    int m_height = 540;
    float m_minimizedWidth = 176.0f;
    int m_restoreX = 34;
    int m_restoreY = 56;
    int m_restoreW = 760;
    int m_restoreH = 540;
    int m_dragOffsetX = 0;
    int m_dragOffsetY = 0;
    int m_resizeStartMouseX = 0;
    int m_resizeStartMouseY = 0;
    int m_resizeStartW = 0;
    int m_resizeStartH = 0;
    int m_mouseX = 0;
    int m_mouseY = 0;
    bool m_hasMouseOverride = false;
    int m_mouseOverrideX = 0;
    int m_mouseOverrideY = 0;
    char m_sceneFilter[64] = {};
    bool m_sceneFilterKeyWasDown[256] = {};
    int m_sceneComboScroll = 0;
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
    void ClearSceneFilter();
    void CloseSceneCombo();
    void CaptureSceneFilterKeyState();
    bool SceneFilterKeyPressed( int virtualKey );
    void AppendSceneFilterChar( char value );
    void BackspaceSceneFilter();
    void UpdateSceneFilterTyping( InGameUIInputResult& result, const char* const* sceneOptions, int sceneOptionCount );
    void SetMaximized( bool maximized, int screenW, int screenH );
};

} // namespace UI
} // namespace SkullbonezCore
