#pragma once

#include "../SkullbonezCommon.h"
#include "UiButton.h"
#include "UiCheckBox.h"
#include "UiComboBox.h"
#include "UiBackdropBlur.h"
#include "UiScrollBar.h"
#include "UiSlider.h"
#include "UiTabBar.h"
#include <cstdint>

namespace SkullbonezCore
{
namespace Ui
{

enum class InGameUiTab
{
    Overview,
    Profiler,
    Scene,
    Physics,
    Options,
    Renderer,
    Keys,
    Count
};

struct InGameUiFrameData
{
    int screenW = 1;
    int screenH = 1;
    const char* rendererName = "";
    const char* sceneName = "";
    int drawCallsBeforeUi = 0;
    int uiDrawCalls = 0;
    float fps = 0.0f;
    float renderMs = 0.0f;
    float physicsMs = 0.0f;
    int modelCount = 0;
    int currentFrame = 0;
    int targetFrameCount = -1;
    int currentSceneIndex = -1;
    int sceneCount = 0;
    double now = 0.0;
    bool sceneMode = false;
    bool scenePhysicsEnabled = true;
    bool sceneTextEnabled = true;
    bool legacyPhysics = false;
    bool fixedStep = false;
    bool testComplete = false;
    bool vsyncEnabled = false;
    bool pipelineSyncEnabled = false;
    bool rollAlignEnabled = true;
    float sceneEnergy = 0.0f;
    float timeScale = 1.0f;
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

struct InGameUiInputResult
{
    bool toggleVsync = false;
    bool toggleCollisionVisualizer = false;
    bool togglePhysicsDebugTransparent = false;
    bool toggleDebugVectors = false;
    bool toggleBroadphaseOverlay = false;
    bool toggleScenePhysics = false;
    bool toggleSceneText = false;
    bool toggleFixedStep = false;
    bool togglePipelineSync = false;
    bool toggleRollAlign = false;
    bool toggleTerrainHidden = false;
    bool toggleWaterHidden = false;
    bool toggleWaterFreeze = false;
    bool toggleWaterFlat = false;
    bool toggleWaterReflection = false;
    bool resetScene = false;
    bool saveSceneDefaults = false;
    float requestedTimeScale = -1.0f;
    float requestedPhysicsDebugAlpha = -1.0f;
    float requestedPhysicsDebugContactLinger = -1.0f;
    int requestedModelCount = -1;
    uint32_t togglePhysicsDebugFlags = 0;
    int requestedRendererIndex = -1; // 0=GL, 1=DX11, 2=DX12, -1=no request
    int requestedWaterReflectionMode = -1; // 0=FBO, 1=DXR, 2=None, -1=no request
};

class InGameUi
{
  public:
    bool IsVisible() const;
    void SetVisible( bool visible, double now = 0.0 );
    void ToggleVisible( double now );
    void SetMinimized( bool minimized, double now = 0.0 );
    void SetActiveTab( InGameUiTab tab );
    InGameUiTab GetActiveTab() const;
    bool BlocksCameraMouse() const;
    void SetWindowBounds( int x, int y, int width, int height );
    void SetBlurEnabled( bool enabled );
    void SetRendererComboOpen( bool open );
    void SetProfilerExpandAll( bool expandAll );
    void SetProfilerTimelineEnabled( bool enabled );
    void SetMouseOverride( bool enabled, int x = 0, int y = 0 );
    void ResetResources();

    InGameUiInputResult UpdateInput( HWND hwnd, int screenW, int screenH, double now );
    void Draw( const InGameUiFrameData& data );

  private:
    bool m_isVisible = true;
    bool m_isMinimized = true;
    bool m_isMaximized = false;
    bool m_leftWasDown = false;
    bool m_isDragging = false;
    bool m_isResizing = false;
    bool m_blocksCameraMouse = false;
    bool m_blurPreviewEnabled = true;
    bool m_profilerTimelineEnabled = false;
    InGameUiTab m_activeTab = InGameUiTab::Profiler;
    UiTabBar m_tabBar;
    UiCheckBox m_blurToggle;
    UiCheckBox m_vsyncToggle;
    UiCheckBox m_timelineToggle;
    UiCheckBox m_physicsToggles[7];
    UiCheckBox m_optionToggles[11];
    UiCheckBox m_controlToggles[17];
    UiSlider m_timeScaleSlider;
    UiSlider m_modelCountSlider;
    UiSlider m_physicsAlphaSlider;
    UiSlider m_contactLingerSlider;
    UiButton m_resetSceneButton;
    UiButton m_saveDefaultsButton;
    UiComboBox m_rendererCombo;
    UiComboBox m_reflectionCombo;
    UiBackdropBlur m_backdropBlur;
    UiScrollBar m_scrollBar;
    int m_x = 34;
    int m_y = 56;
    int m_width = 760;
    int m_height = 540;
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
    float m_scrollY = 0.0f;
    double m_scrollbarVisibleUntil = 0.0;
    int m_activeSlider = 0; // 0=none, 1=time scale, 2=model count, 3=debug alpha, 4=contact linger
    float m_previewTimeScale = -1.0f;
    int m_previewModelCount = -1;
    float m_previewPhysicsAlpha = -1.0f;
    float m_previewContactLinger = -1.0f;
    uint32_t m_expandedProfilerHashes[64] = {};
    int m_expandedProfilerHashCount = 0;
    bool m_expandAllProfilerMarkers = false;
    bool m_profilerDefaultExpansionApplied = false;

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
    void SetMaximized( bool maximized, int screenW, int screenH );
};

} // namespace Ui
} // namespace SkullbonezCore
