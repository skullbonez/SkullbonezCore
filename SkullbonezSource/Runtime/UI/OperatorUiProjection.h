/*
File: SkullbonezSource/Runtime/UI/OperatorUiProjection.h
Purpose:
  Declares UI-owned projection from detached Runtime facts into operator rows.

Summary:
  App samples concrete domain owners into narrow per-domain facts. Runtime/UI
  alone chooses operator row order and maps those facts into the shared editor
  and GameUI frame values before Render receives the completed snapshot.

Invariants:
  - No source fact contains a mutable Runtime owner, callback, or service surface.
  - Every source-label borrow ends before the completed UI frame is submitted.
  - Reserve-capacity rows remain live until the synchronous UI draw completes.
  - Projection performs no GPU work and emits no process command.

Related:
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp samples facts.
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
  - SkullbonezSource/Runtime/UI/GameUI/UI.h
*/
#pragma once

#include "../../Core/MainMemoryStats.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Rendering/RenderDiagnosticsTypes.h"
#include "GameUI/UI.h"
#include "../RuntimeFrameViews.h"
#include "RuntimeViewModel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{
struct InGameUIFrameData;
struct OperatorEditorFrameView;
struct UIRuntimeReserveCapacityRow;
} // namespace UI
namespace Runtime
{
// Invariant: one Scene generation is sampled for both operator surfaces.
// Labels remain frame-local borrows, while every mutable Scene owner stays in App.
struct OperatorUiSceneFacts
{
    RuntimeViewModel runtime;
    const char* currentScenePath = nullptr;
    const char* currentSceneName = nullptr;
    const char* const* sceneOptions = nullptr;
    int currentSceneBrowserIndex = -1;
    int sceneOptionCount = 0;
    int selectedCineModeSceneIndex = -1;
    int entityCount = 0;
    int rngSeed = 1;
    int solverBallCount = 0;
    int solverBoxCount = 0;
    float energyForDisplay = 0.0f;
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    bool sceneHasCurrentEntry = false;
    bool exitOnComplete = false;
    bool testComplete = false;
};

enum class OperatorUiGizmoMode : uint8_t
{
    Translate,
    Rotate,
    Scale
};

// Invariant: both operator surfaces display one rendering configuration,
// presentation-alpha sample, and detached gizmo-mode decision.
struct OperatorUiRenderingFacts
{
    Core::OrdinaryRenderConfig ordinary;
    Core::CinematicRenderConfig cinematic;
    RuntimeUiTextFrameFacts uiText;
    OperatorUiGizmoMode gizmoMode = OperatorUiGizmoMode::Translate;
    bool vsyncEnabled = false;
    bool presentationInterpolation = true;
    bool shadowsEnabled = false;
    bool cinematicRendering = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterFrozen = false;
    bool waterFlat = false;
    bool waterNoReflect = false;
    bool waterRtReflect = false;
};

enum class OperatorUiForecastCause : uint8_t
{
    None,
    InvalidContract,
    NonFiniteState,
    PrivateStepFailure,
    InvalidPublication,
    InnerEnvelope,
    OuterEnvelope,
    SustainedEscape,
    Collision
};

// Invariant: health, drift, and first-failure identity belong to one immutable
// Planning forecast publication.
struct OperatorUiForecastFacts
{
    double simulatedSeconds = 0.0;
    double simulatedSecondsPerRealSecond = 0.0;
    double rollingWindowAgeSeconds = 0.0;
    double energyDrift = 0.0;
    double angularMomentumDrift = 0.0;
    double maximumAbsoluteEnergyDrift = 0.0;
    double maximumAngularMomentumDrift = 0.0;
    double firstFailureSeconds = 0.0;
    uint64_t newestAbsoluteTick = 0u;
    uint64_t retainedBytes = 0u;
    uint32_t firstFailureSubject = 0u;
    uint32_t firstFailureOther = 0u;
    OperatorUiForecastCause firstFailureCause = OperatorUiForecastCause::None;
    bool available = false;
    bool active = false;
    bool workerInFlight = false;
    bool failed = false;
    bool configured = false;
    bool numericalHealthy = false;
    bool systemOrbitalHealthy = false;
    bool auxiliaryOrbitalHealthy = false;
    bool energyDriftAvailable = false;
    bool angularMomentumDriftAvailable = false;
};

// Invariant: hierarchy policy and selection identify one Scene entity-catalog
// generation; entity rows below are indexed only within that generation.
struct OperatorUiHierarchyFacts
{
    uint32_t totalRowCount = 0u;
    int selectedRow = -1;
    int selectedObjectType = 0;
    int objectTypeCount = 0;
    int undoDepth = 0;
    int redoDepth = 0;
    bool sceneDirty = false;
    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placeStaticObject = false;
    bool crossScenePauseLocked = false;
    bool fixedStep = false;
    bool autoTerrainAlign = false;
    bool buildingAssetsAvailable = false;
};

// Invariant: one row carries one entity identity from the hierarchy generation;
// it contains no Scene store handle or mutation surface.
struct OperatorUiHierarchyEntityFacts
{
    const char* displayName = "";
    uint32_t sceneObjectId = 0u;
    uint32_t groupRootObjectId = 0u;
    int groupPartIndex = -1;
    bool assetBacked = false;
    bool visible = true;
    bool locked = false;
};

// Invariant: all fields describe one selected entity observation; label borrows
// expire with the synchronous operator projection.
struct OperatorUiInspectorFacts
{
    const char* displayName = "";
    const char* renderMaterialName = "";
    const char* contactMaterialName = "";
    const char* assetName = "";
    const char* assetInstanceName = "";
    const char* assetPartName = "";
    UI::OperatorEditorInspectorSelectionState selectionState = UI::OperatorEditorInspectorSelectionState::None;
    uint32_t sceneObjectId = 0u;
    uint32_t selectionCount = 0u;
    int renderMaterialKind = 0;
    int colliderShapeKind = 0;
    int behaviorGroupKind = 0;
    int behaviorPartIndex = -1;
    float position[3] = {};
    float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float linearVelocity[3] = {};
    float angularVelocity[3] = {};
    float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float mass = 0.0f;
    float volume = 0.0f;
    float boundingRadius = 0.0f;
    float dragCoefficient = 0.0f;
    float friction = 0.0f;
    float restitution = 0.0f;
    float roughness = 0.0f;
    float metallic = 0.0f;
    float specular = 0.0f;
    bool visible = true;
    bool locked = false;
    bool fixed = false;
    bool sleeping = false;
    bool assetBacked = false;
};

// Invariant: world controls and physics policy come from one sampled Scene
// generation and contain no mutable World or Physics owner.
struct OperatorUiWorldFacts
{
    int modelCount = 0;
    int modelCapacity = 0;
    int solverBallCount = 0;
    int solverBoxCount = 0;
    int rngSeed = 1;
    float timeScale = 1.0f;
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;
    float terrainFriction = 0.0f;
    float objectFriction = 0.0f;
    float rollingFriction = 0.0f;
    float tornadoRadius = 0.0f;
    float tornadoHeight = 0.0f;
    float tornadoInward = 0.0f;
    float tornadoSwirl = 0.0f;
    float tornadoLift = 0.0f;
    bool fixedStep = false;
    bool physicsSleepEnabled = true;
    bool tornadoEnabled = false;
};

// Invariant: settings values form one operator-edit generation across render,
// physics, and environment controls; App applies any returned typed commands.
struct OperatorUiSettingsFacts
{
    Core::OrdinaryRenderConfig ordinary;
    Core::CinematicRenderConfig cinematic;
    UI::UIPhysicsDebugStatus physicsDebug;
    int modelCapacity = 0;
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
    float tornadoRadius = 0.0f;
    float tornadoHeight = 0.0f;
    float tornadoInwardAcceleration = 0.0f;
    float tornadoSwirlAcceleration = 0.0f;
    float tornadoLiftAcceleration = 0.0f;
    float terrainFriction = 0.0f;
    float objectFriction = 0.0f;
    float rollingFriction = 0.0f;
    bool textOnly = false;
    bool vsyncEnabled = false;
    bool pipelineSyncEnabled = false;
    bool physicsSleepEnabled = true;
    bool tornadoEnabled = false;
    bool tornadoVisualShell = false;
    bool tornadoFieldVectors = false;
    bool waterFrozen = false;
    bool waterFlat = false;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterNoReflect = false;
    bool waterRtReflect = false;
    bool cinematicRendering = false;
};

// Invariant: camera, pointer, and editor interaction values describe one input
// turn and carry no router/controller capability.
struct OperatorUiInteractionFacts
{
    const char* cameraModeLabel = "";
    float trackHeight = 0.0f;
    float autoCycleInterval = 0.0f;
    float rayCastImpulseStrength = 0.0f;
    float launcherProjectileSpeed = 0.0f;
    uint32_t cameraModeEnabledMask = 0u;
    int cameraModeIndex = 0;
    int editorObjectType = 0;
    int editorUndoDepth = 0;
    int editorRedoDepth = 0;
    bool rayCastVisualization = false;
    bool cameraMouseActive = false;
    bool editorModeEnabled = false;
    bool editorPlacementMode = false;
    bool editorPlaceStatic = false;
    bool editorTerrainAlign = false;
    bool editorViewportLookActive = false;
};

// Invariant: every timing and identity field belongs to one profiler marker in
// one sampled frame; names are synchronous borrows.
struct OperatorUiProfilerMarkerFacts
{
    const char* name = "";
    const char* leafName = "";
    uint32_t hash = 0u;
    int parentIndex = -1;
    int depth = 0;
    int colorIndex = 0;
    float lastFrameMs = 0.0f;
    float lastSelfMs = 0.0f;
    float avgMs = 0.0f;
    float selfAvgMs = 0.0f;
    float lastFrameWorkerMs = 0.0f;
    float workerAvgMs = 0.0f;
    float p50Ms = 0.0f;
    float p99Ms = 0.0f;
    float gpuLastFrameMs = 0.0f;
    bool hasGpu = false;
};

// Invariant: one row describes one worker's contribution to the sampled
// profiler frame and cannot control worker execution.
struct OperatorUiWorkerCoreFacts
{
    int workerIndex = -1;
    int jobCount = 0;
    float coreMs = 0.0f;
    float avgCoreMs = 0.0f;
    float spanStartMs = 0.0f;
    float spanEndMs = 0.0f;
};

// Invariant: one secondary-diagnostics row describes one catalog identity
// without its backend texture handle; its label borrow is frame-local.
struct OperatorUiSecondaryRenderTargetFacts
{
    const char* label = "";
    int width = 0;
    int height = 0;
    bool available = false;
    bool depth = false;
    bool hdr = false;
};

// Invariant: secondary diagnostics are one bounded measurement generation;
// every render-target row belongs to the same renderer snapshot.
struct OperatorUiSecondaryDiagnosticsFacts
{
    std::array<OperatorUiSecondaryRenderTargetFacts, UI::OPERATOR_EDITOR_RENDER_TARGET_CAPACITY> renderTargets = {};
    const char* rendererName = "";
    const char* physicsPipelineStageName = "";
    int renderTargetCount = 0;
    int drawCalls = 0;
    int uiDrawCalls = 0;
    int workerThreadCount = 0;
    int maxWorkerThreadCount = 1;
    int physicsPipelineStageIndex = 0;
    int physicsPipelineStageCount = 0;
    float fps = 0.0f;
    float renderMs = 0.0f;
    float physicsMs = 0.0f;
    float cpuFrameMs = 0.0f;
    float gpuFrameMs = 0.0f;
    float physicsDebugAlpha = 0.0f;
    float physicsDebugContactLinger = 0.0f;
    float rayCastImpulseStrength = 0.0f;
    float launcherProjectileSpeed = 0.0f;
    uint64_t trackedEngineBytes = 0u;
    uint64_t reconciledTotalBytes = 0u;
    uint64_t uploadUsedBytes = 0u;
    uint64_t uploadCapacityBytes = 0u;
    uint64_t replayReserveGrowthEvents = 0u;
    uint32_t physicsDebugFlags = 0u;
    bool collisionVisualizer = false;
    bool physicsDebugTransparent = false;
    bool broadphaseOverlay = false;
    bool tornadoVisualShell = false;
    bool tornadoFieldVectors = false;
    bool rayCastVisualization = false;
};

class OperatorUiRenderTargetListFacts;
inline void ProjectOperatorUiRenderTargets( UI::InGameUIFrameData& uiData, const OperatorUiRenderTargetListFacts& facts );

// Invariant: Append is the only construction seam, preserves renderer catalog
// order, and accepts presentation values only. A backend handle cannot cross
// into the private bounded rows or be recovered by Runtime/UI.
class OperatorUiRenderTargetListFacts
{
  public:
    bool Append( const char* label, int width, int height, bool available, bool depth, bool hdr )
    {
        if ( m_count >= UI::UI_RENDER_TARGET_PREVIEW_MAX )
        {
            return false;
        }

        m_targets[static_cast<std::size_t>( m_count )] = { label, width, height, available, depth, hdr };
        ++m_count;
        return true;
    }

  private:
    friend void ProjectOperatorUiRenderTargets( UI::InGameUIFrameData& uiData,
                                                const OperatorUiRenderTargetListFacts& facts );

    // Invariant: one private row describes one catalog identity without its
    // backend texture handle; its label borrow ends after synchronous draw.
    struct Target
    {
        const char* label = "";
        int width = 0;
        int height = 0;
        bool available = false;
        bool depth = false;
        bool hdr = false;
    };

    std::array<Target, UI::UI_RENDER_TARGET_PREVIEW_MAX> m_targets = {};
    int m_count = 0;
};

// Invariant: diagnostics facts form one immutable measurement generation. The fixed
// arrays copy profiler/reserve values so Runtime/UI never borrows their owners;
// draw-trace labels retain only the renderer snapshot's synchronous lifetime.
struct OperatorUiDiagnosticsFacts
{
    RuntimeFrameMetricsSnapshot metrics;
    Rendering::RenderVisibilityStats visibility;
    Rendering::RenderMemoryStats renderMemory;
    Rendering::DrawCallTraceSnapshot drawTrace;
    Core::MainMemoryStats mainMemory;
    std::array<OperatorUiProfilerMarkerFacts, UI::ProfilerTab::MAX_MARKERS> markers = {};
    std::array<OperatorUiWorkerCoreFacts, UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES> workerSamples = {};
    std::array<Core::Allocation::RuntimeReserveGrowthEventView, UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX>
        reserveGrowthEvents = {};
    std::array<Core::Allocation::RuntimeReserveCapacityView, UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX> reserveCapacityRows =
        {};
    int markerCount = 0;
    int workerSampleCount = 0;
    int reserveGrowthEventCount = 0;
    int reserveCapacityRowCount = 0;
    int workerThreadCount = 0;
    int maxWorkerThreadCount = 1;
    int replayMemoryPreset = 0;
    int replayRequestedRetentionSeconds = 0;
    int replayRequestedBudgetMiB = 0;
    int replayPresentationRetentionSeconds = 0;
    int replaySolverRetentionSeconds = 0;
    uint64_t reserveGrowthEventTotalCount = 0u;
    uint64_t reserveGrowthEventDroppedCount = 0u;
    double now = 0.0;
    float predictionRevealRate = 0.0f;
    bool tracyBuildEnabled = false;
    bool tracyInitialized = false;
    bool tracyViewerConnected = false;
    bool replayMemoryBudgetClamped = false;
    bool replayMemorySolverWindowReduced = false;
    bool renderMemoryAvailable = false;
    bool reserveCapacityAvailable = false;
};

inline Core::MainMemoryStats ProjectMemoryTabAvailability( bool sourceValid, const Core::MainMemoryStats& sampledMemory )
{
    return sourceValid ? sampledMemory : Core::MainMemoryStats {};
}

void ProjectOperatorEditorScene( UI::OperatorEditorFrameView& view, const OperatorUiSceneFacts& facts );
void ProjectOperatorEditorRendering( UI::OperatorEditorFrameView& view, const OperatorUiRenderingFacts& facts );
void ProjectOperatorEditorForecast( UI::OperatorEditorFrameView& view, const OperatorUiForecastFacts& facts );
void ProjectOperatorEditorLookLab( UI::OperatorEditorFrameView& view, const UI::OperatorEditorLookLabView& lookLab );
void ProjectOperatorEditorReplay( UI::OperatorEditorFrameView& view, int memoryPreset, int requestedRetentionSeconds,
                                  int requestedBudgetMiB, int presentationRetentionSeconds, int solverRetentionSeconds,
                                  bool memoryBudgetClamped, bool solverWindowReduced );
void ProjectOperatorEditorSurfaces( UI::OperatorEditorFrameView& view, bool primaryVisible, bool secondaryVisible );
inline void BeginOperatorEditorHierarchy( UI::OperatorEditorFrameView& view, const OperatorUiHierarchyFacts& facts )
{
    view.scene.dirty = facts.sceneDirty;
    view.tools = { facts.editorModeEnabled, facts.placementModeEnabled, facts.placeStaticObject, facts.crossScenePauseLocked,
                   facts.fixedStep,         facts.autoTerrainAlign,     facts.undoDepth,         facts.redoDepth };
    view.hierarchy.totalRowCount = facts.totalRowCount;
    view.hierarchy.rowCount = (std::min)( view.hierarchy.totalRowCount, UI::OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY );
    view.hierarchy.truncated = view.hierarchy.totalRowCount > view.hierarchy.rowCount;
    view.assets = { facts.selectedObjectType, facts.objectTypeCount, facts.buildingAssetsAvailable };
}

inline void AppendOperatorEditorHierarchyRow( UI::OperatorEditorFrameView& view, const OperatorUiHierarchyFacts& hierarchy,
                                              const OperatorUiHierarchyEntityFacts& entity, uint32_t sourceIndex )
{
    if ( sourceIndex >= view.hierarchy.rowCount )
    {
        return;
    }

    UI::OperatorEditorHierarchyRow& row = view.hierarchy.rows[sourceIndex];
    row.displayName = entity.displayName;
    row.sceneObjectId = entity.sceneObjectId;
    row.groupRootObjectId = entity.groupRootObjectId;
    row.groupPartIndex = entity.groupPartIndex;
    row.assetBacked = entity.assetBacked;
    row.visible = entity.visible;
    row.locked = entity.locked;
    row.selected = static_cast<int>( sourceIndex ) == hierarchy.selectedRow;

    if ( row.selected )
    {
        view.hierarchy.selectedSceneObjectId = row.sceneObjectId;
    }
}
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void ProjectOperatorEditorInspectorAndWorld( UI::OperatorEditorFrameView& view, const OperatorUiInspectorFacts& inspector,
                                             const OperatorUiWorldFacts& world );
#endif
void ProjectOperatorEditorDiagnostics( UI::OperatorEditorFrameView& view, const OperatorUiSecondaryDiagnosticsFacts& facts );

void ProjectOperatorUiDiagnostics( UI::InGameUIFrameData& uiData, const OperatorUiDiagnosticsFacts& facts,
                                   UI::UIRuntimeReserveCapacityRow* reserveCapacityRows );
void ProjectOperatorUiPresentation( UI::InGameUIFrameData& uiData, const OperatorUiSceneFacts& facts,
                                    const UI::OperatorEditorFrameView& operatorEditorView );
void ProjectOperatorUiSettings( UI::InGameUIFrameData& uiData, const OperatorUiSettingsFacts& facts );
void ProjectOperatorUiInteraction( UI::InGameUIFrameData& uiData, const OperatorUiInteractionFacts& facts );
inline void ProjectOperatorUiViewport( UI::InGameUIFrameData& uiData, int width, int height )
{
    uiData.surface.screenW = width;
    uiData.surface.screenH = height;
}

inline void ProjectOperatorUiRenderIdentity( UI::InGameUIFrameData& uiData, const char* rendererName, int drawCallsBeforeUi )
{
    uiData.surface.rendererName = rendererName;
    uiData.surface.drawCallsBeforeUI = drawCallsBeforeUi;
}

inline void ProjectOperatorUiRecordingBrowser( UI::InGameUIFrameData& uiData, const char* const* options, int optionCount,
                                               int selectedOption )
{
    uiData.scene.interactionRecordingOptions = options;
    uiData.scene.interactionRecordingOptionCount = optionCount;
    uiData.scene.selectedInteractionRecordingOption = selectedOption;
}

inline void ProjectOperatorUiRenderTargets( UI::InGameUIFrameData& uiData, const OperatorUiRenderTargetListFacts& facts )
{
    uiData.renderTargets.count = facts.m_count;

    for ( int index = 0; index < uiData.renderTargets.count; ++index )
    {
        const OperatorUiRenderTargetListFacts::Target& source = facts.m_targets[static_cast<std::size_t>( index )];
        UI::UIRenderTargetPreviewResource& target = uiData.renderTargets.previews[index];
        target = { source.label, source.width, source.height, source.available, source.depth, source.hdr };
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
