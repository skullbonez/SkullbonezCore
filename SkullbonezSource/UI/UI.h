/*
File: SkullbonezSource/UI/UI.h
Purpose:
  Implements SkullbonezUI widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  UI.h implements SkullbonezUI widgets, layout, drawing, or UI state for the
  in-engine controls. As a public header, keep edits anchored on UI request,
  layout, hit-test, and draw-command flow and on the glossary/invariants
  below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.
  Sound sample path: Borrowed contact-audio asset path displayed by the Sound
    tab for immediate-frame preview and selection.
  Contact-audio flash mode: Frame-data label and value for the Sound tab's
    render-only decision flash selector.
  Contact-audio reducer counter: Runtime count showing how raw contact facts
    were merged, silenced, budgeted, or submitted by the Sound tab model.
  Simple contact audio: Optional linear-energy path for thuds, exposed as an
    obvious Sound-tab section so the old contact-row classifier can be bypassed.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UI.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Core/MainMemoryStats.h"
#include "../Runtime/Allocation/RuntimeReserveAllocator.h"
#include "../Rendering/IShader.h"
#include "../Rendering/IRenderDiagnostics.h"
#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UICache.h"
#include "UIBackdropBlur.h"
#include "UIScrollBar.h"
#include "UISlider.h"
#include "UIState.h"
#include "UIDrawList.h"
#include "UITabBar.h"
#include "UITabCinematic.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabMemory.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UITabSound.h"
#include "UITabSky.h"
#include <cstdint>
#include <memory>

namespace SkullbonezCore
{
namespace Basics
{
struct DeviceInputFrame;
struct RuntimeMouseEdges;
} // namespace Basics

namespace Assets
{
class AssetSystem;
}

namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderResourceFactory;
} // namespace Rendering

namespace UI
{

enum class InGameUITab
{
    Profiler,
    Scene,
    Editor,
    Physics,
    Sound,
    Options,
    Render,
    Targets,
    Keys,
    Sky,
    Cinematic,
    Memory,
    Count
};

constexpr int UI_RENDER_TARGET_PREVIEW_MAX = 12;
constexpr int UI_PROFILER_MARKER_OPTION_MAX = ProfilerTab::MAX_MARKERS + 1;
constexpr uint32_t UI_PROFILER_FRAME_TOTAL_HASH = 0u;
constexpr int UI_SOUND_SET_MAX = 16;
constexpr int UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX = 64;

struct UIRenderContext
{
    // Lifetime: each pointer is borrowed for the current UI/text pass only.
    // Resource creation, draw commands, and draw tracing stay split so the UI
    // never needs the wide renderer facade.
    Assets::AssetSystem* assets = nullptr;
    Rendering::IRenderResourceFactory* resources = nullptr;
    Rendering::IRenderCommandContext* commands = nullptr;
    Rendering::IRenderDiagnostics* diagnostics = nullptr;

    bool IsReady() const
    {
        return assets != nullptr && resources != nullptr && commands != nullptr && diagnostics != nullptr;
    }
};
constexpr int UI_SOUND_BAND_MAX = 4;
constexpr int UI_SOUND_SAMPLE_MAX = 64;

struct UIRenderTargetPreviewResource
{
    const char* label = "";
    uint32_t textureHandle = 0;
    int width = 0;
    int height = 0;
    bool available = false;
    bool depth = false;
    bool hdr = false;
};

struct UIProfilerMarkerOption
{
    const char* name = "";
    const char* leafName = "";
    uint32_t hash = UI_PROFILER_FRAME_TOTAL_HASH;
    float cpuMs = 0.0f;
    float cpuAverageMs = 0.0f;                     // Same 500 ms moving average used by the profiler table.
    float gpuMs = 0.0f;
    float colorR = 0.0f;                           // RGB borrowed from the profiler row palette for chart overlays.
    float colorG = 0.0f;
    float colorB = 0.0f;
    bool hasGpu = false;
    bool sampleValid = false;
    bool isFrameTotal = false;
};

struct UISoundBandFrameData
{
    const char* name = "";
    float minImpulse = 0.0f;
    float impulseRange = 0.0f;
    float baseGain = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    uint32_t sampleCount = 0;
};

struct UISoundSetFrameData
{
    const char* name = "";
    uint32_t materialA = 0;
    uint32_t materialB = 0;
    float minImpulse = 0.0f;
    float impulseRange = 0.0f;
    float cooldownMs = 0.0f;
    float overrideCooldownMs = 0.0f;
    float maxDistance = 0.0f;
    float baseGain = 0.0f;
    float pitchMin = 1.0f;
    float pitchMax = 1.0f;
    uint32_t maxVoices = 0;
    uint32_t sampleCount = 0;
    uint32_t bandCount = 0;
    UISoundBandFrameData bands[UI_SOUND_BAND_MAX];
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
    Rendering::RenderVisibilityStats visibility;
    float fps = 0.0f;
    float renderMs = 0.0f;
    float physicsMs = 0.0f;
    float cpuFrameMs = 0.0f;
    float gpuFrameMs = 0.0f;
    float workerCoreTotalMs = 0.0f;                // Sum of worker-pool CPU chunk time from the last committed frame, in ms.
    // Lifetime: profiler and draw-trace names are borrowed for this immediate UI
    // pass. The profiler tab caches only bounded values needed for next-frame
    // input/layout; drawing gets refreshed from this snapshot every frame.
    ProfilerTab::FrameSnapshot profiler;
    UIProfilerMarkerOption profilerMarkerOptions[UI_PROFILER_MARKER_OPTION_MAX];
    int profilerMarkerOptionCount = 0;
    UISoundSetFrameData soundSets[UI_SOUND_SET_MAX];
    int soundSetCount = 0;
    const char* soundSamplePaths[UI_SOUND_SAMPLE_MAX] = {};
    int soundSampleCount = 0;
    Basics::MainMemoryStats mainMemory;
    Runtime::Allocation::RuntimeReserveGrowthEventView reserveGrowthEvents[UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX];
    int reserveGrowthEventCount = 0;
    uint64_t reserveGrowthEventTotalCount = 0;
    uint64_t reserveGrowthEventDroppedCount = 0;
    int replayMemoryPreset = 0;
    int replayMemoryRequestedRetentionSeconds = 0;
    int replayMemoryRequestedBudgetMiB = 0;
    int replayMemoryPresentationRetentionSeconds = 0;
    int replayMemorySolverRetentionSeconds = 0;
    bool replayMemoryBudgetClamped = false;
    bool replayMemorySolverWindowReduced = false;
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
    bool contactAudioEnabled = false;
    bool contactAudioAvailable = false;
    bool contactAudioDebugCounters = false;
    int contactAudioFlashMode = 1;
    const char* contactAudioFlashModeLabel = "Flash: Emitted";
    float contactAudioMasterGain = 0.0f;
    float contactAudioMaxDistanceScale = 1.0f;
    float contactAudioMinClosingSpeed = 0.0f;
    float contactAudioMinImpactScore = 0.0f;
    float contactAudioImpactScoreRangeSeconds = 1.0f;
    bool contactAudioSimpleMode = true;
    float contactAudioSimpleMinLinearEnergy = 270.0f;
    float contactAudioSimpleMinLinearDeltaSpeed = 2.0f;
    float contactAudioSimpleLinearEnergyRange = 320.0f;
    uint32_t contactAudioBurstVoicesPerWindow = 0; // Max submitted sounds per 100 ms burst.
    float contactAudioRollingLevelDb = -24.0f;
    float contactAudioRollingMaxDistance = 24.0f;
    float contactAudioRollingMinSlipSpeed = 0.65f;
    uint32_t contactAudioRollingVoicesPerWindow = 4;
    uint32_t contactAudioEventsSeen = 0;
    uint32_t contactAudioPatchCandidates = 0;
    uint32_t contactAudioMergedCandidates = 0;
    uint32_t contactAudioCandidateOverflows = 0;
    uint32_t contactAudioBurstWindowSkippedCandidates = 0;
    uint32_t contactAudioBudgetRejectedCandidates = 0;
    uint32_t contactAudioRejectedByThreshold = 0;
    uint32_t contactAudioRejectedByCooldown = 0;
    uint32_t contactAudioSubmittedVoices = 0;
    uint32_t contactAudioDroppedVoices = 0;
    uint32_t contactAudioRollingCandidates = 0;
    uint32_t contactAudioRollingSubmittedVoices = 0;
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
    bool tornadoVisualShell = false;
    bool tornadoFieldVectors = false;
    bool rayCastVisualization = false;
    float tornadoRadius = 0.0f;
    float tornadoHeight = 0.0f;
    float tornadoInwardAcceleration = 0.0f;
    float tornadoSwirlAcceleration = 0.0f;
    float tornadoLiftAcceleration = 0.0f;
    float rayCastImpulseStrength = 0.0f;
    float launcherProjectileSpeed = 0.0f;
    float terrainFrictionCoeff = 0.0f;
    float objectFrictionCoeff = 0.0f;
    float rollingFrictionCoeff = 0.0f;
    bool waterFreezeDebug = false;
    bool waterFlatDebug = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterNoReflect = false;
    bool waterRTReflect = false;
    bool cameraMouseActive = false;
    bool nativeCursorVisible = false;
    const char* runtimeInputModeLabel = "";
    int cameraModeIndex = 0;
    uint32_t cameraModeEnabledMask = 0x7Fu;
    bool editorModeEnabled = false;
    bool editorPlacementMode = false;
    bool editorPlaceStatic = true;
    bool editorTerrainAlign = false;
    bool editorViewportLookActive = false;
    int editorObjectType = 0;
    bool canSaveSceneDefaults = false;
    bool cinematicRendering = false;
    Basics::OrdinaryRenderConfig ordinaryRender;
    Basics::CinematicRenderConfig cinematic;
    UIRenderTargetPreviewResource renderTargetPreviews[UI_RENDER_TARGET_PREVIEW_MAX];
    int renderTargetPreviewCount = 0;
};

class InGameUI
{
  public:
    bool IsVisible() const;
    bool IsMinimized() const;
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
    bool IsPerformanceHistogramEnabled() const;
    void TogglePerformanceHistogramEnabled();
    void SetMemoryOverlayEnabled( bool enabled );
    bool IsMemoryOverlayEnabled() const;
    void ToggleMemoryOverlayEnabled();
    bool NeedsUiTextPass() const;
    void SetHitboxOverlayEnabled( bool enabled );
    void SetScrollY( float scrollY );
    void SetMouseOverride( bool enabled, int x = 0, int y = 0 );
    void CancelInputCapture();
    void ResetResources( Rendering::IRenderResourceFactory* resources );

    InGameUIInputResult UpdateInput( const Basics::DeviceInputFrame& deviceFrame,
                                     const Basics::RuntimeMouseEdges& mouse,
                                     int screenW,
                                     int screenH,
                                     double now,
                                     bool editorModeEnabled = false,
                                     bool editorPlacementMode = false,
                                     bool editorPlaceStatic = true,
                                     bool editorTerrainAlign = false,
                                     int editorObjectType = EditorTab::OBJECT_BOX,
                                     int cameraModeIndex = 0,
                                     uint32_t cameraModeEnabledMask = 0x7Fu,
                                     const char* const* sceneOptions = nullptr,
                                     int sceneOptionCount = 0,
                                     int selectedSceneOption = -1 );
    void Draw( const InGameUIFrameData& data, const UIRenderContext& render );

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
    UIComboBox m_renderTargetCombo;
    UIComboBox m_cameraModeCombo;
    UICheckBox m_cinematicMasterToggle;
    UICheckBox m_renderShadowToggle;
    UIButton m_saveRenderDefaultsButton;
    UISlider m_renderSliders[static_cast<int>( UIRenderParam::Count )];
    UIBackdropBlur m_backdropBlur;
    UICacheState m_cache;
    UIDrawList m_histogramDrawList;
    UIDrawList m_memoryOverlayDrawList;
    std::unique_ptr<Rendering::IShader> m_renderTargetPreviewShader;
    uint32_t m_renderTargetPreviewVB = 0;
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
    int m_lastRenderTargetPreviewCount = 0;
    uint32_t m_lastRenderTargetDisabledMask = 0;
    int m_selectedRenderTargetPreview = 0;
    bool m_hasMouseOverride = false;
    int m_mouseOverrideX = 0;
    int m_mouseOverrideY = 0;
    ControlsTab::UIControlsTabState m_controlsTab;
    EditorTab::UIEditorTabState m_editorTab;
    OptionsTab::UIOptionsTabState m_optionsTab;
    PhysicsTab::UIPhysicsTabState m_physicsTab;
    ProfilerTab::UIProfilerTabState m_profilerTab;
    MemoryTab::UIMemoryOverlayState m_memoryOverlay;
    SceneTab::UISceneTabState m_sceneTab;
    SoundTab::UISoundTabState m_soundTab;
    SkyTab::UISkyTabState m_skyTab;
    CinematicTab::UICinematicTabState m_cinematicTab;
    float m_scrollY = 0.0f;
    double m_scrollbarVisibleUntil = 0.0;
    int m_activeSlider = 0;                        // 0=none; other values map to Controls/Options sliders in UI.cpp
    bool m_hitboxOverlayEnabled = false;
    bool m_editorMiniPalettePressActive = false;
    bool m_editorMiniPaletteFlyoutOpen = false;
    int m_editorMiniPalettePressedEntry = -1;
    int m_editorMiniPalettePressedObjectType = -1;
    int m_editorMiniPalettePressedTreePlacement = -1;
    int m_editorMiniPalettePressedHoldMode = 0;
    double m_editorMiniPalettePressStart = 0.0;

    int ContentHeight() const;
    void DrawHitboxOverlay( const UIDrawContext& draw,
                            const InGameUIFrameData& data,
                            const UIRect& windowBounds,
                            const UIRect& contentBounds,
                            const UIRect& footerBounds ) const;
    void CloseSceneCombo();
    void CancelEditorMiniPaletteInteraction();
    void SetMaximized( bool maximized, int screenW, int screenH, double now = 0.0 );
};

} // namespace UI
} // namespace SkullbonezCore
