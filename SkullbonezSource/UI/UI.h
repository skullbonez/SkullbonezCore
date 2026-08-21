/*
File: SkullbonezSource/UI/UI.h
Purpose:
  Declares the in-engine UI draw composer and its stable public command surface.

Summary:
  InGameUI composes one bounded draw frame around a concrete
  UIWindowInteractionOwner. Callers keep the stable InGameUI API while window,
  widget, tab-input, gesture, and cache authority live in that owner. Detached
  Look Lab status is republished only at authoring transitions and read from a
  UI-owned cache during idle composition.

Glossary:
  Scene navigation model: UI-owned browser rows and generated-scene overrides
    borrowed synchronously by runtime navigation and load transactions.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - Scene browser pointer views and live overrides share InGameUI's lifetime;
    runtime owners never retain a backpointer to this model.
  - The interaction owner has no InGameUI backpointer, friend edge, callback
    pack, or unrelated runtime context.
  - Draw returns backend-neutral values and never requires a renderer owner.
  - Runtime App republishes detached Look Lab status only after an authoring or
    scene transition; idle UI composition reads the cache without an upward edge.
  - Capacity-row labels live in Runtime's detached fixed snapshot; UI borrows
    them only for the synchronous draw and retains no allocator span.

Related:
  - SkullbonezSource/UI/UI.cpp
  - SkullbonezSource/UI/UIWindowInteractionOwner.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Core/MainMemoryStats.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"
#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UIRenderDiagnostics.h"
#include "UICache.h"
#include "UIBackdropBlur.h"
#include "UIScrollBar.h"
#include "UISlider.h"
#include "UISceneNavigationModel.h"
#include "UIState.h"
#include "UIDrawList.h"
#include "UITabProfiler.h"
#include "UIWindowInteractionOwner.h"
#include <cstdint>

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core

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
constexpr int UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX = 64;
constexpr int UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX = 160;
constexpr int UI_RUNTIME_RESERVE_OWNER_NAME_MAX = 64;
constexpr int UI_RUNTIME_RESERVE_REASON_MAX = 96;
constexpr int UI_RUNTIME_RESERVE_SUBSYSTEM_NAME_MAX = 16;

struct UIRuntimeReserveCapacityRow
{
    char ownerName[UI_RUNTIME_RESERVE_OWNER_NAME_MAX] = {};
    char capacityReason[UI_RUNTIME_RESERVE_REASON_MAX] = {};
    char subsystemName[UI_RUNTIME_RESERVE_SUBSYSTEM_NAME_MAX] = {};
    int elementSizeBytes = 0;
    int currentCapacity = 0;
    int liveCount = 0;
    int sessionHighWater = 0;
    uint64_t residentBytes = 0;
};

struct UIRenderTargetPreviewResource
{
    const char* label = "";
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
    float cpuAverageMs = 0.0f;        // Same 500 ms moving average used by the profiler table.

    // Worker-thread time for the same marker. The histogram plots cpu+worker so
    // selecting a worker-owned marker graphs the work that marker actually did;
    // the profiler table's CPU and Work columns own the per-thread split.
    float workerMs = 0.0f;
    float workerAverageMs = 0.0f;
    float gpuMs = 0.0f;
    float colorR = 0.0f;              // RGB borrowed from the profiler row palette for chart overlays.
    float colorG = 0.0f;
    float colorB = 0.0f;
    bool hasGpu = false;
    bool sampleValid = false;
    bool isFrameTotal = false;
};

// Snapshot of engine state needed to draw the UI for one frame.  The UI reads
// this structure but does not mutate engine objects directly; that keeps render
// code, input hit-testing, and runtime state changes separated.
struct InGameUIFrameData
{
    // Shared domain view consumed by both operator front ends. Legacy flat
    // fields remain for primary-surface consumers that have not adopted it.
    OperatorEditorFrameView operatorEditor;
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
    UIRenderVisibilityStats visibility;
    float fps = 0.0f;
    float renderMs = 0.0f;
    float physicsMs = 0.0f;
    float cpuFrameMs = 0.0f;
    float gpuFrameMs = 0.0f;
    float workerCoreTotalMs = 0.0f;   // Sum of worker-pool CPU chunk time from the last committed frame, in ms.

    // Lifetime: profiler and draw-trace names are borrowed for this immediate UI
    // pass. The profiler tab caches only bounded values needed for next-frame
    // input/layout; drawing gets refreshed from this snapshot every frame.
    ProfilerTab::FrameSnapshot profiler;
    UIProfilerMarkerOption profilerMarkerOptions[UI_PROFILER_MARKER_OPTION_MAX];
    int profilerMarkerOptionCount = 0;
    SkullbonezCore::Core::MainMemoryStats mainMemory;
    UIRenderMemoryStats renderMemory; // Value snapshot for the Memory tab/overlay only.
    const UIRuntimeReserveCapacityRow* reserveCapacityRows = nullptr;
    int reserveCapacityRowCount = 0;
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView reserveGrowthEvents[UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX];
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

    // Predicted seconds revealed per real second by the causal-unfold cursor.
    // Presentation pacing only; the Physics tab shows and edits it.
    float predictionRevealRate = 1.0f;
    int modelCount = 0;
    int modelCapacity = SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECT_CAPACITY;
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
    bool presentationInterpolation = true;
    bool presentationPinned = false;
    float presentationAlpha = 1.0f;
    float trackHeight = 0.0f;
    float autoCycleInterval = 0.0f;
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    UIPhysicsDebugStatus physicsDebug;
    bool physicsSleepEnabled = true;
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
    int editorUndoDepth = 0;
    int editorRedoDepth = 0;
    bool canSaveSceneDefaults = false;
    bool cinematicRendering = false;
    SkullbonezCore::Core::OrdinaryRenderConfig ordinaryRender;
    SkullbonezCore::Core::CinematicRenderConfig cinematic;
    UIRenderTargetPreviewResource renderTargetPreviews[UI_RENDER_TARGET_PREVIEW_MAX];
    int renderTargetPreviewCount = 0;
};

class InGameUI
{
  public:
    explicit InGameUI( Core::Profiler* profiler = nullptr ) : m_profiler( profiler )
    {
    }
    bool IsVisible() const;
    bool IsMinimized() const;
    void SetVisible( bool visible, double now = 0.0 );
    void ToggleVisible( double now );
    void SetMinimized( bool minimized, double now = 0.0 );
    void SetActiveTab( InGameUITab tab );
    InGameUITab GetActiveTab() const;
    bool BlocksCameraMouse() const;
    bool BlocksKeyboard() const;
    bool WantsNativeMouseCursor() const;
    void SetWindowBounds( int x, int y, int width, int height );

    // Captures a window-local semantic pointer anchor when the point belongs to this UI.
    bool CaptureInteractionAnchor( int clientX, int clientY, char* output, std::size_t outputSize ) const;

    // Resolves a recorded UI anchor against the current window layout.
    bool ResolveInteractionAnchor( const char* anchor, int& clientX, int& clientY ) const;
    void SetBlurEnabled( bool enabled );
    void SetRendererComboOpen( bool open );
    void SetWaterComboOpen( bool open );
    void SetSceneComboOpen( bool open );
    void SetSceneFilter( const char* filter );
    void SetProfilerExpandAll( bool expandAll );
    void SetProfilerTimelineEnabled( bool enabled );
    void SetPerformanceHistogramEnabled( bool enabled );
    void TogglePerformanceHistogramEnabled();
    bool IsMemoryOverlayEnabled() const;
    void ToggleMemoryOverlayEnabled();
    bool NeedsUiTextPass() const;
    void SetHitboxOverlayEnabled( bool enabled );
    void SetScrollY( float scrollY );
    void SetMouseOverride( bool enabled, int x = 0, int y = 0 );
    void CancelInputCapture();

    // UI retains only the last detached Look Lab presentation value. App
    // republishes it at authoring transitions, so idle frame composition never
    // polls the Runtime Direction owner.
    void SetLookLabView( const OperatorEditorLookLabView& view )
    {
        m_lookLabView = view;
    }
    const OperatorEditorLookLabView& LookLabView() const
    {
        return m_lookLabView;
    }

    // Clears UI-owned layout/backdrop caches after presentation invalidation;
    // GPU resource release belongs exclusively to Runtime/Render.
    void ResetPresentationState();
    SceneNavigationModel& SceneNavigation()
    {
        return m_sceneNavigation;
    }
    const SceneNavigationModel& SceneNavigation() const
    {
        return m_sceneNavigation;
    }

    // Returns the UI-owned automation pointer substitution by value so Runtime
    // can apply it while constructing the detached input snapshot.
    InputControl::UIPointerOverride InputOverride() const;
    InGameUIInputResult UpdateInput( const InputControl::UIInputSnapshot& input, int screenWidth, int screenHeight,
                                     double now, bool editorModeEnabled, bool editorPlacementMode, bool editorPlaceStatic,
                                     bool editorTerrainAlign, int cameraModeIndex, uint32_t cameraModeEnabledMask,
                                     std::span<const char* const> sceneOptions, int selectedSceneOption );

    // Builds one complete ordered frame of backend-neutral draw values. The
    // returned view remains valid until the next Draw call on this owner.
    const UIDrawList& Draw( const InGameUIFrameData& data );

  private:

    // Lifetime: Init owns this profiler beyond the cohesive UI owner; input and
    // draw paths borrow it without resolving process-global diagnostics state.
    Core::Profiler* m_profiler = nullptr;
    SceneNavigationModel m_sceneNavigation;
    OperatorEditorLookLabView m_lookLabView;

    // Lifetime: the interaction owner holds every widget and gesture record
    // shared by hit testing and drawing. It never retains an InGameUI reach-back.
    UIWindowInteractionOwner m_windowInteraction;
    UIDrawList m_frameDrawList;
    UIDrawList m_histogramDrawList;
    UIDrawList m_memoryOverlayDrawList;
    void DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data, const UIRect& windowBounds,
                            const UIRect& contentBounds, const UIRect& footerBounds );
};

} // namespace UI
} // namespace SkullbonezCore
