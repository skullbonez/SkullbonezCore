/*
File: SkullbonezSource/UI/OperatorEditorExchange.h
Purpose:
  Defines the shared value boundary used by either operator editor surface.

Summary:
  GameUI and ImGui use the same domain-grouped frame snapshot and fixed-capacity
  typed command queues, including detached forecast status and lifecycle intent,
  but runtime selection activates only one human surface. A deterministic
  arbitration pass also admits optional automation/probe intent before projecting
  one canonical packet into established owner command paths.

Glossary:
  Domain view: Read-only scene, property, rendering, diagnostics, authoring,
    replay, or tool values
    copied for one presentation frame.
  Tool command: One-frame edit-mode, history, pause, or step intent applied by
    the established editor and scene-flow owners after arbitration.
  Canonical packet: The existing InGameUICommands value consumed by runtime
    owner appliers after this exchange has resolved the active producer lane.
  Duplicate: The same typed action and payload emitted by the active surface
    and an injected producer during one input turn.
  Conflict: Two commands with the same stable action identity but different
    payloads during one input turn.

Invariants:
  - Views and commands contain values or immediate-frame borrowed labels only;
    no scene, replay, renderer, input, or UI owner pointer crosses this seam.
  - Every queue is inline and bounded; submission cannot grow an STL container.
  - GameUI commands have deterministic first priority, exact secondary duplicates
    coalesce, and conflicting duplicate payloads fail through recoverable result.
  - Projection produces at most one established owner request per action type.

Related:
  - SkullbonezSource/UI/UICommands.h
  - SkullbonezSource/Runtime/App/InputFrame.cpp
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbResult.h"

#include <array>
#include <cstdint>

namespace SkullbonezCore::UI
{
struct InGameUICommands;

struct OperatorEditorSceneView
{
    // Lifetime: sceneName is borrowed only for the current presentation frame.
    const char* sceneName = "";

    // Lifetime: sceneOptions and every pointed-to label are borrowed from the
    // scene-navigation owner for this one synchronous presentation frame.
    const char* const* sceneOptions = nullptr;
    int currentSceneIndex = -1;
    int sceneCount = 0;
    int currentFrame = 0;
    int modelCount = 0;
    float timeScale = 1.0f;
    bool canSaveCurrentScene = false;
    bool dirty = false;
};

inline constexpr uint32_t OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY = 512u;

struct OperatorEditorHierarchyRow
{
    // Lifetime: displayName is borrowed from SceneEntityStore for this frame.
    const char* displayName = "";
    uint32_t sceneObjectId = 0u;
    uint32_t groupRootObjectId = 0u;
    int groupPartIndex = -1;
    bool assetBacked = false;
    bool visible = true;
    bool locked = false;
    bool selected = false;
};

struct OperatorEditorHierarchyView
{
    OperatorEditorHierarchyRow rows[OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY] = {};
    uint32_t rowCount = 0u;
    uint32_t totalRowCount = 0u;
    uint32_t selectedSceneObjectId = 0u;
    bool truncated = false;
};

struct OperatorEditorAssetView
{
    int selectedObjectType = 0;
    int objectTypeCount = 0;
    bool registeredLibraryAvailable = false;
};

struct OperatorEditorPropertyView
{
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
};

enum class OperatorEditorInspectorSelectionState : uint8_t
{
    None,
    Single,
    Mixed,
    Stale
};

struct OperatorEditorInspectorView
{
    // Lifetime: labels borrow fixed scene/collider strings for this synchronous
    // presentation frame; the UI must not retain their addresses.
    const char* displayName = "";
    const char* renderMaterialName = "";
    const char* contactMaterialName = "";
    const char* assetName = "";
    const char* assetInstanceName = "";
    const char* assetPartName = "";
    OperatorEditorInspectorSelectionState selectionState = OperatorEditorInspectorSelectionState::None;
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

struct OperatorEditorWorldView
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
    bool fixedStep = false; // Scene/capture render-frame-lockstep request; Runtime resolves effective pacing.
    bool physicsSleepEnabled = true;
    bool tornadoEnabled = false;
};

struct OperatorEditorRenderingView
{
    static constexpr int ordinaryParameterCount = 38;
    static constexpr int cinematicParameterCount = 64;
    static constexpr int cinematicFeatureCount = 8;

    // Invariant: parameter indices match UIRenderParam/UICinematicParam and
    // are guarded by static assertions at the Run composition boundary.
    float ordinaryParameters[ordinaryParameterCount] = {};
    float cinematicParameters[cinematicParameterCount] = {};
    bool cinematicFeatures[cinematicFeatureCount] = {};
    bool vsyncEnabled = false;
    bool shadowsEnabled = false;
    bool cinematicRendering = false;
    bool presentationInterpolation = true;
    float presentationAlpha = 1.0f;
    bool terrainHidden = false;
    bool waterHidden = false;
    bool waterFrozen = false;
    bool waterFlat = false;
    int waterReflectionMode = 0;
};

inline constexpr int OPERATOR_EDITOR_RENDER_TARGET_CAPACITY = 12;

struct OperatorEditorRenderTargetView
{
    const char* label = "";
    int width = 0;
    int height = 0;
    bool available = false;
    bool depth = false;
    bool hdr = false;
};

struct OperatorEditorDiagnosticsView
{
    OperatorEditorRenderTargetView renderTargets[OPERATOR_EDITOR_RENDER_TARGET_CAPACITY] = {};
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
    float workerCoreTotalMs = 0.0f;
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
    bool terrainContactProbe = false;
    bool tornadoVisualShell = false;
    bool tornadoFieldVectors = false;
    bool rayCastVisualization = false;
};

struct OperatorEditorViewportView
{
    // Lifetime: labels are borrowed from runtime camera/gesture owners for one
    // synchronous presentation frame.
    const char* cameraModeLabel = "unknown";
    const char* gizmoModeLabel = "translate";
    bool presentationPinned = false;
};

struct OperatorEditorReplayView
{
    int memoryPreset = 0;
    int requestedRetentionSeconds = 0;
    int requestedBudgetMiB = 0;
    int presentationRetentionSeconds = 0;
    int solverRetentionSeconds = 0;
    bool memoryBudgetClamped = false;
    bool solverWindowReduced = false;
};

enum class OperatorEditorForecastCause : uint8_t
{
    None = 0,
    InvalidContract,
    NonFiniteState,
    PrivateStepFailure,
    InvalidPublication,
    InnerEnvelope,
    OuterEnvelope,
    SustainedEscape,
    Collision
};

// Concept: this detached value is the only forecast state either operator
// surface may inspect; UI never borrows the Planning owner or its sample ring.
struct OperatorEditorForecastView
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
    OperatorEditorForecastCause firstFailureCause = OperatorEditorForecastCause::None;
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

struct OperatorEditorSurfaceView
{
    bool gameUiVisible = true;
    bool secondaryVisible = false;
};

struct OperatorEditorToolView
{
    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placeStaticObject = false;
    bool crossScenePauseLocked = false;
    bool fixedStep = false; // Scene/capture render-frame-lockstep request; Runtime resolves effective pacing.
    bool autoTerrainAlign = false;
    int undoDepth = 0;
    int redoDepth = 0;
};

struct OperatorEditorLookLabView
{
    // Invariant: this is detached status only; Runtime retains candidate and
    // transaction authority. TestOwnerRequestQueues.cpp pins its fingerprint.
    uint64_t seed = 0;
    bool hasCandidate = false;
    bool savePending = false;
    std::array<char, 128> detail = {};
    std::array<char, 512> bundleDirectory = {};
};

struct OperatorEditorFrameView
{
    OperatorEditorSceneView scene;
    OperatorEditorPropertyView property;
    OperatorEditorRenderingView rendering;
    OperatorEditorViewportView viewport;
    OperatorEditorReplayView replay;
    OperatorEditorForecastView forecast;
    OperatorEditorSurfaceView surfaces;
    OperatorEditorToolView tools;
    OperatorEditorLookLabView lookLab;
    OperatorEditorHierarchyView hierarchy;
    OperatorEditorAssetView assets;
    OperatorEditorInspectorView inspector;
    OperatorEditorWorldView world;
    OperatorEditorDiagnosticsView diagnostics;
};

enum class OperatorEditorSceneCommandType : uint8_t
{
    ResetCurrentScene,
    ResetSceneDefaults,
    RequestDemoScene,
    SetCurrentSceneIndex,
    SaveCurrentScene,
    CreateScene
};

struct OperatorEditorSceneCommand
{
    OperatorEditorSceneCommandType type = OperatorEditorSceneCommandType::ResetCurrentScene;
    int sceneIndex = -1;
    char sceneName[64] = {};
};

enum class OperatorEditorPropertyCommandType : uint8_t
{
    SetTimeScale,
    ToggleFixedStep,
    SetModelCount,
    SetSeed,
    SetSolverBallCount,
    SetSolverBoxCount,
    SetWorldGravity,
    SetWorldFluidHeight,
    SetWorldFluidDensity,
    TogglePhysicsSleepPolicy,
    SetTerrainFriction,
    SetObjectFriction,
    SetRollingFriction,
    ToggleTornado,
    SetTornadoRadius,
    SetTornadoHeight,
    SetTornadoInward,
    SetTornadoSwirl,
    SetTornadoLift
};

enum class OperatorEditorEditPhase : uint8_t
{
    Preview,
    Commit
};

struct OperatorEditorPropertyCommand
{
    OperatorEditorPropertyCommandType type = OperatorEditorPropertyCommandType::SetTimeScale;
    float value = 0.0f;
    int integerValue = 0;
    OperatorEditorEditPhase phase = OperatorEditorEditPhase::Commit;
};

enum class OperatorEditorRenderingCommandType : uint8_t
{
    ToggleVsync,
    ToggleShadows,
    ToggleTerrainHidden,
    ToggleWaterHidden,
    ToggleWaterFreeze,
    ToggleWaterFlat,
    CycleWaterReflection,
    ToggleCinematicRendering,
    ToggleCinematicFeature,
    SetOrdinaryParameter,
    SetCinematicParameter,
    SaveOrdinaryDefaults,
    SaveSkyDefaults
};

struct OperatorEditorRenderingCommand
{
    OperatorEditorRenderingCommandType type = OperatorEditorRenderingCommandType::ToggleVsync;
    int parameter = -1;
    float value = 0.0f;
    OperatorEditorEditPhase phase = OperatorEditorEditPhase::Commit;
};

enum class OperatorEditorDiagnosticsCommandType : uint8_t
{
    ToggleCollisionVisualizer,
    TogglePhysicsDebugTransparent,
    ToggleBroadphaseOverlay,
    ToggleTerrainContactProbe,
    ToggleTornadoVisualShell,
    ToggleTornadoFieldVectors,
    ToggleRayCastVisualization,
    TogglePhysicsDebugFlag,
    StepPhysicsPipelinePrevious,
    StepPhysicsPipelineNext,
    SetPhysicsDebugAlpha,
    SetPhysicsContactLinger,
    SetRayCastImpulseStrength,
    SetLauncherProjectileSpeed,
    SetWorkerThreads
};

struct OperatorEditorDiagnosticsCommand
{
    OperatorEditorDiagnosticsCommandType type = OperatorEditorDiagnosticsCommandType::ToggleCollisionVisualizer;
    uint32_t flag = 0u;
    int integerValue = 0;
    float value = 0.0f;
    OperatorEditorEditPhase phase = OperatorEditorEditPhase::Commit;
};

enum class OperatorEditorReplayCommandType : uint8_t
{
    SetMemoryPolicy,
    SetRecordingEnabled,
    JumpToStart,
    JumpToEnd,
    TogglePlayPause,
    StepBackward,
    StepForward,
    SetRevealSpeed,
    Scrub,
    TogglePrediction,
    SetPredictionHorizon,
    RestoreBranch,
    Save,
    Load,
    ReturnToLive,
    SelectCauseRow
};

struct OperatorEditorReplayCommand
{
    OperatorEditorReplayCommandType type = OperatorEditorReplayCommandType::SetMemoryPolicy;
    int presetIndex = -1;
    int retentionSeconds = -1;
    int budgetMiB = -1;
    int rowIndex = -1;
    float value = 0.0f;
    bool enabled = false;
};

enum class OperatorEditorForecastCommandType : uint8_t
{
    ToggleContinuous,
    Reset,
    Exit
};

struct OperatorEditorForecastCommand
{
    OperatorEditorForecastCommandType type = OperatorEditorForecastCommandType::ToggleContinuous;
};

const char* OperatorEditorForecastCauseName( OperatorEditorForecastCause cause ) noexcept;

enum class OperatorEditorToolCommandType : uint8_t
{
    ToggleEditorMode,
    TogglePlacementMode,
    Undo,
    Redo,
    ToggleCrossScenePause,
    StepPausedScene,
    SelectSceneObject,
    DeleteSelection,
    DuplicateSelection,
    SetPlacementObjectType,
    SetPlaceStatic,
    ToggleTerrainAlign,
    SetEntityVisible,
    SetEntityLocked
};

struct OperatorEditorToolCommand
{
    OperatorEditorToolCommandType type = OperatorEditorToolCommandType::ToggleEditorMode;
    uint32_t sceneObjectId = 0u;
    int value = 0;
    bool enabled = false;
};

template <typename Command, uint32_t Capacity> struct OperatorEditorCommandQueue
{
    static constexpr uint32_t capacity = Capacity;
    Command commands[Capacity] = {};
    uint32_t count = 0u;

    [[nodiscard]] bool Empty() const noexcept
    {
        return count == 0u;
    }
};

using OperatorEditorSceneCommandQueue = OperatorEditorCommandQueue<OperatorEditorSceneCommand, 8u>;
using OperatorEditorPropertyCommandQueue = OperatorEditorCommandQueue<OperatorEditorPropertyCommand, 24u>;
using OperatorEditorRenderingCommandQueue = OperatorEditorCommandQueue<OperatorEditorRenderingCommand, 8u>;
using OperatorEditorDiagnosticsCommandQueue = OperatorEditorCommandQueue<OperatorEditorDiagnosticsCommand, 8u>;
using OperatorEditorReplayCommandQueue = OperatorEditorCommandQueue<OperatorEditorReplayCommand, 8u>;
using OperatorEditorForecastCommandQueue = OperatorEditorCommandQueue<OperatorEditorForecastCommand, 4u>;
using OperatorEditorToolCommandQueue = OperatorEditorCommandQueue<OperatorEditorToolCommand, 16u>;

struct OperatorEditorCommandQueues
{
    OperatorEditorSceneCommandQueue scene;
    OperatorEditorPropertyCommandQueue property;
    OperatorEditorRenderingCommandQueue rendering;
    OperatorEditorDiagnosticsCommandQueue diagnostics;
    OperatorEditorReplayCommandQueue replay;
    OperatorEditorForecastCommandQueue forecast;
    OperatorEditorToolCommandQueue tools;

    [[nodiscard]] bool Empty() const noexcept
    {
        return scene.Empty() && property.Empty() && rendering.Empty() && diagnostics.Empty() && replay.Empty() &&
               forecast.Empty() && tools.Empty();
    }
};

struct OperatorEditorArbitrationResult
{
    OperatorEditorCommandQueues commands;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    uint32_t acceptedGameUiCommands = 0u;
    uint32_t acceptedSecondaryCommands = 0u;
    uint32_t coalescedDuplicateCommands = 0u;
};

SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorSceneCommandQueue& queue,
                                                            const OperatorEditorSceneCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorPropertyCommandQueue& queue,
                                                            const OperatorEditorPropertyCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorRenderingCommandQueue& queue,
                                                            const OperatorEditorRenderingCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorDiagnosticsCommandQueue& queue,
                                                            const OperatorEditorDiagnosticsCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorReplayCommandQueue& queue,
                                                            const OperatorEditorReplayCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorForecastCommandQueue& queue,
                                                            const OperatorEditorForecastCommand& command,
                                                            bool* duplicate = nullptr );
SkullbonezCore::Core::SbResult SubmitOperatorEditorCommand( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            OperatorEditorToolCommandQueue& queue,
                                                            const OperatorEditorToolCommand& command,
                                                            bool* duplicate = nullptr );

// Converts the representative GameUI packet fields into the shared queues and
// clears those fields so only the post-arbitration projection reaches owners.
SkullbonezCore::Core::SbResult NormalizeGameUiOperatorEditorCommands( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                      InGameUICommands& commands );
OperatorEditorArbitrationResult ArbitrateOperatorEditorCommands( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                 const OperatorEditorCommandQueues& gameUi,
                                                                 const OperatorEditorCommandQueues& secondary );
SkullbonezCore::Core::SbResult ProjectOperatorEditorCommands( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                              const OperatorEditorCommandQueues& exchange,
                                                              InGameUICommands& commands );
uint64_t FingerprintOperatorEditorFrameView( const OperatorEditorFrameView& view ) noexcept;
} // namespace SkullbonezCore::UI
