/*
File: SkullbonezSource/UI/UICommands.h
Purpose:
  Defines one-frame request packets emitted by in-engine operator controls.

Summary:
  Domain command structs keep presentation intent separate from concrete owner
  mutation. The shared editor exchange is normalized and arbitrated before its
  representative actions are projected back into this established packet.

Glossary:
  Command struct: One-frame request packet emitted by UI code and consumed by
  the run loop.
  Shared editor exchange: Fixed-capacity domain queues through which both
    operator front ends request the same existing owner operations.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.
  - Command packets are values; they contain no callback, owner pointer, or
    authority to mutate runtime state directly.

Related:
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "OperatorEditorExchange.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

enum class UICinematicParam
{
    // Each enum value maps one Cine-tab slider to one SkullbonezCore::Core::CinematicRenderConfig field.
    // The run loop receives this id, clamps the value, and applies it live.
    None = -1,
    Exposure,
    Gamma,
    SkyMode,
    TerrainMode,
    ObjectStyle,
    WaterMode,
    StyleSaturation,
    StyleContrast,
    StyleVignette,
    SunAzimuth,
    SunElevation,
    SunBrightness,
    SunRed,
    SunGreen,
    SunBlue,
    SkyGlow,
    HorizonRed,
    HorizonGreen,
    HorizonBlue,
    ZenithRed,
    ZenithGreen,
    ZenithBlue,
    CloudCoverage,
    CloudSoftness,
    CloudScale,
    CloudIntensity,
    ShaftStrength,
    ShaftFalloff,
    VolumetricStrength,
    VolumetricDensity,
    VolumetricDecay,
    BloomThreshold,
    BloomKnee,
    BloomStrength,
    BloomRadius,
    TerrainRelief,
    TerrainTintRed,
    TerrainTintGreen,
    TerrainTintBlue,
    TerrainAccentRed,
    TerrainAccentGreen,
    TerrainAccentBlue,
    TerrainGridScale,
    TerrainGridStrength,
    WaterTintRed,
    WaterTintGreen,
    WaterTintBlue,
    WaterAlpha,
    WaterReflection,
    WaterGlint,
    BasinCenterX,
    BasinCenterZ,
    BasinRadiusX,
    BasinRadiusZ,
    BasinFeather,
    BasinDepth,
    BasinRimLift,
    FogDensity,
    FogOpacity,
    FogStart,
    FogEnd,
    FogRed,
    FogGreen,
    FogBlue,
    Count
};

enum class UICinematicFeature
{
    // Each enum value maps one Cine-tab toggle to a render pass or visual feature.
    None = -1,
    Sky,
    Clouds,
    GodRays,
    VolumetricLight,
    Bloom,
    Fog,
    TerrainRelief,
    Shadows,
    Count
};

enum class UIRenderParam
{
    // Each enum value maps one Render-tab slider to one SkullbonezCore::Core::OrdinaryRenderConfig field.
    None = -1,
    SunIntensity,
    SunRed,
    SunGreen,
    SunBlue,
    AmbientStrength,
    SkyRed,
    SkyGreen,
    SkyBlue,
    GroundRed,
    GroundGreen,
    GroundBlue,
    ShadowStrength,
    ShadowSoftness,
    ShadowDepthBias,
    ShadowSlopeBias,
    WaterRed,
    WaterGreen,
    WaterBlue,
    WaterAlpha,
    WaterReflection,
    WaterFresnel,
    BallRoughness,
    BallSpecular,
    BoxRoughness,
    BoxSpecular,
    TrajectoryFutureWidth,
    TrajectoryFutureAlpha,
    TrajectoryFutureEdgeFeather,
    TrajectoryCausalWidth,
    TrajectoryCausalAlpha,
    TrajectoryCausalEdgeFeather,
    TrajectoryBaselineWidth,
    TrajectoryBaselineAlpha,
    TrajectoryBaselineEdgeFeather,
    TrajectoryMarkerWidth,
    TrajectoryMarkerAlpha,
    TrajectoryMarkerEdgeFeather,
    TrajectorySelectedEmphasis,
    Count
};

// Presentation vocabulary for the four visualizer layers exposed as individual
// UI toggles. Runtime maps these values to Physics-owned flags; UI never imports
// the concrete visualizer or assumes that its storage mask is an authority seam.
enum class UIPhysicsDebugOverlay : uint32_t
{
    None,
    Axes,
    Contacts,
    Sleep,
    Pipeline
};

// Detached status rendered by the Physics tab. Runtime samples each bool from
// its concrete diagnostics owner so UI does not interpret Physics-owned bits.
struct UIPhysicsDebugStatus
{
    uint32_t activeFlags = 0u;             // Display-only hexadecimal diagnostic value.
    const char* pipelineStageName = "";
    int pipelineStageIndex = 0;
    int pipelineStageCount = 0;
    float alpha = 0.0f;
    float contactLinger = 0.0f;
    bool axes = false;
    bool contacts = false;
    bool sleep = false;
    bool pipeline = false;
    bool terrainContact = false;
    bool collisionVisualizer = false;
    bool transparent = false;
    bool broadphase = false;
};

struct UIOnlyCommands
{
    // Commands are one-frame requests, not durable state. The UI sets them
    // while handling input; Run consumes them and mutates the engine.
    // This prevents UI widgets from directly owning renderer, scene, or physics
    // state.
    bool userInteracted = false;
};

struct UIRendererCommands
{
    bool toggleVsync = false;
    int requestedRendererIndex = -1;       // Retired compatibility field; DX12 is the only runtime renderer.
};

struct UISceneCommands
{
    bool resetScene = false;               // Rebuild current scene while preserving live runtime controls.
    bool resetSceneDefaults = false;       // Discard live scene edits and reload authored defaults.
    bool requestDemoScene = false;         // Switch to generated demo scene instead of a discovered scene file.
    bool saveSceneDefaults = false;        // Persist editable-scene defaults back to disk.
    bool createScene = false;              // Create a new starter scene from requestedSceneName.
    char requestedSceneName[64] = {};
    int requestedSceneIndex = -1;          // index into sceneOptions, -1=no request
    bool toggleCrossScenePause = false;    // Toggle the scene-flow owner's pause lock across scene transitions.
    bool requestSingleStep = false;        // Advance one paused scene turn; never retained beyond this input frame.
};

struct UIPhysicsCommands
{
    UIPhysicsDebugOverlay physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::None;
    bool toggleCollisionVisualizer = false;
    bool togglePhysicsSleepPolicy = false;
    bool togglePhysicsDebugTransparent = false;
    bool toggleBroadphaseOverlay = false;
    bool toggleTerrainContactProbe = false;
    bool toggleTornado = false;
    bool toggleTornadoVisualShell = false;
    bool toggleTornadoFieldVectors = false;
    bool toggleRayCastVisualization = false;
    bool requestTornadoRadius = false;
    bool requestTornadoHeight = false;
    bool requestTornadoInward = false;
    bool requestTornadoSwirl = false;
    bool requestTornadoLift = false;
    bool requestRayCastImpulseStrength = false;
    bool requestLauncherProjectileSpeed = false;
    bool requestTerrainFrictionCoeff = false;
    bool requestObjectFrictionCoeff = false;
    bool requestRollingFrictionCoeff = false;
    float requestedPhysicsDebugAlpha = -1.0f;
    float requestedPhysicsDebugContactLinger = -1.0f;
    float requestedTornadoRadius = 0.0f;
    float requestedTornadoHeight = 0.0f;
    float requestedTornadoInward = 0.0f;
    float requestedTornadoSwirl = 0.0f;
    float requestedTornadoLift = 0.0f;
    float requestedRayCastImpulseStrength = 0.0f;
    float requestedLauncherProjectileSpeed = 0.0f;
    float requestedTerrainFrictionCoeff = 0.0f;
    float requestedObjectFrictionCoeff = 0.0f;
    float requestedRollingFrictionCoeff = 0.0f;
    bool stepPhysicsPipelinePrevious = false;
    bool stepPhysicsPipelineNext = false;

    // Concept: prediction reveal pacing is presentation, not simulation. It
    // decides how fast the causal-unfold cursor sweeps an already-computed
    // horizon, so it never reaches physics, replay samples, or solver restores.
    // It lives beside the physics controls because that is where an operator
    // looks after enabling predict.
    bool requestPredictionRevealRate = false;
    float requestedPredictionRevealRate = 0.0f;
};

struct UIEditorCommands
{
    bool toggleEditorMode = false;
    bool togglePlacementMode = false;
    bool togglePlaceStatic = false;
    bool toggleTerrainAlign = false;
    bool enterPlacementMode = false;
    bool requestPlaceStatic = false;
    bool requestedPlaceStatic = false;
    int requestedObjectType = -1;
    bool requestUndo = false;              // Ask the editor history owner to undo one committed command.
    bool requestRedo = false;              // Ask the editor history owner to redo one committed command.
    bool requestSelectSceneObject = false; // Resolve stable scene identity at the editor-owner boundary.
    uint32_t requestedSceneObjectId = 0u;
    bool requestDeleteSelection = false;
    bool requestDuplicateSelection = false;
    bool requestSetEntityVisible = false;
    bool requestedEntityVisible = true;
    uint32_t visibilitySceneObjectId = 0u;
    bool requestSetEntityLocked = false;
    bool requestedEntityLocked = false;
    uint32_t lockSceneObjectId = 0u;
};

struct UISceneOptionCommands
{
    bool toggleTextOnly = false;
    bool toggleFixedStep = false;
    bool toggleTerrainHidden = false;
    bool toggleWaterHidden = false;
    bool toggleWaterFreeze = false;
    bool toggleWaterFlat = false;
    bool toggleShadows = false;
    float requestedTimeScale = -1.0f;
    int requestedModelCount = -1;
};

struct UIWaterCommands
{
    bool toggleWaterReflection = false;
    bool requestWorldGravity = false;
    bool requestWorldFluidHeight = false;
    bool requestWorldFluidDensity = false;
    float requestedWorldGravity = 0.0f;
    float requestedWorldFluidHeight = 0.0f;
    float requestedWorldFluidDensity = 0.0f;
    int requestedWaterReflectionMode = -1; // 0=FBO, 1=DXR, 2=None, -1=no request
};

struct UIRunCommands
{
    int requestedCameraMode = -1;
    int requestedSeed = -1;
    int requestedSolverBallCount = -1;
    int requestedSolverBoxCount = -1;
};

struct UIProfilerCommands
{
    int requestedWorkerThreads = -2;       // -2 = unchanged, -1 = auto, 0 = disabled, >0 = explicit worker count
};

struct UICinematicCommands
{
    // UI output for one frame. These are requests, not state: the render loop
    // consumes them and mutates the real SkullbonezCore::Core::CinematicRenderConfig.
    // requestedModeSceneIndex uses sceneOptions indexing for concept/cine style
    // scenes; -1 = Demo Scene/default style, -2 = no request this frame.
    bool toggleRendering = false;
    bool saveSkyDefaults = false;
    int requestedModeSceneIndex = -2;
    UICinematicFeature requestedFeature = UICinematicFeature::None;
    UICinematicParam requestedParam = UICinematicParam::None;
    float requestedValue = 0.0f;
};

struct UIRenderCommands
{
    bool toggleShadows = false;
    bool saveDefaults = false;
    UIRenderParam requestedParam = UIRenderParam::None;
    float requestedValue = 0.0f;
};

struct UIReplayMemoryCommands
{
    // One-frame replay policy request from the Memory tab. InputFrame translates
    // these UI-facing values into the authoritative replay policy owner.
    bool requestPolicy = false;
    int requestedPresetIndex = -1;
    int requestedRetentionSeconds = -1;
    int requestedBudgetMiB = -1;
};

struct InGameUICommands
{
    OperatorEditorCommandQueues operatorEditor;
    UIOnlyCommands ui;
    UIRendererCommands renderer;
    UISceneCommands scene;
    UIEditorCommands editor;
    UIPhysicsCommands physics;
    UISceneOptionCommands sceneOptions;
    UIWaterCommands water;
    UIRunCommands run;
    UIProfilerCommands profiler;
    UIRenderCommands renderTuning;
    UICinematicCommands cinematic;
    UIReplayMemoryCommands replayMemory;
};

struct InGameUIInputResult
{
    enum class NativeMouseCaptureRequest
    {
        Unchanged,
        Acquire,
        Release
    };

    InGameUICommands commands;
    int unhandledWheelDelta = 0;
    NativeMouseCaptureRequest nativeMouseCapture = NativeMouseCaptureRequest::Unchanged;
};

} // namespace UI
} // namespace SkullbonezCore
