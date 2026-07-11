/*
File: SkullbonezSource/UI/UICommands.h
Purpose:
  Implements UI Commands widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  UICommands.h implements UI Commands widgets, layout, drawing, or UI state
  for the in-engine controls. As a public header, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  FBO (Framebuffer Object): Engine off-screen render target abstraction used by
  reflection and post-processing code.
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.
  Command struct: One-frame request packet emitted by UI code and consumed by
  the run loop.
  Sound sample request: One-frame Sound-tab command to preview a decoded impact
    candidate or assign it to the selected material set.
  Flash mode request: One-frame Sound-tab command to cycle emitted, candidate,
    rejected, or hidden contact-audio body flashes.
  Simple mode request: One-frame Sound-tab command to use linear velocity energy
    instead of solver contact rows for impact audio.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

enum class UICinematicParam
{
    // Each enum value maps one Cine-tab slider to one CinematicRenderConfig field.
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
    // Each enum value maps one Render-tab slider to one OrdinaryRenderConfig field.
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
    Count
};

enum class UISoundParam
{
    None = -1,
    SimpleMinLinearEnergy,
    SimpleMinLinearDeltaSpeed,
    SimpleLinearEnergyRange,
    MasterGain,
    MaxDistanceScale,
    MinClosingSpeed,
    MinImpactScore,
    ImpactScoreRangeSeconds,
    BurstVoicesPerWindow,
    RollingLevelDb,
    RollingMaxDistance,
    RollingMinSlipSpeed,
    RollingVoicesPerWindow,
    SetMinImpulse,
    SetImpulseRange,
    SetCooldownMs,
    SetOverrideCooldownMs,
    SetMaxDistance,
    SetBaseGain,
    SetPitchMin,
    SetPitchMax,
    SetMaxVoices,
    Count
};

enum class UISoundBandParam
{
    None = -1,
    MinImpulse,
    ImpulseRange,
    BaseGain,
    PitchMin,
    PitchMax,
    Count
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
};

struct UIPhysicsCommands
{
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
    uint32_t togglePhysicsDebugFlags = 0;
    bool stepPhysicsPipelinePrevious = false;
    bool stepPhysicsPipelineNext = false;
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
    // consumes them and mutates the real CinematicRenderConfig.
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

struct UISoundCommands
{
    // Sound-tab output is a one-frame request. Set and band indices come from
    // the current UI snapshot; Run validates them before touching audio data.
    bool toggleEnabled = false;
    bool toggleDebugCounters = false;
    bool cycleFlashMode = false;
    bool toggleSimpleMode = false;
    int requestedSetIndex = -1;
    int requestedBandIndex = -1;
    UISoundParam requestedParam = UISoundParam::None;
    UISoundBandParam requestedBandParam = UISoundBandParam::None;
    float requestedValue = 0.0f;
    int previewSampleIndex = -1;
    int selectSampleIndex = -1;
};

struct UIReplayMemoryCommands
{
    // One-frame replay policy request from the Memory tab. RunInput translates
    // these UI-facing values into ReplayRuntime's authoritative policy owner.
    bool requestPolicy = false;
    int requestedPresetIndex = -1;
    int requestedRetentionSeconds = -1;
    int requestedBudgetMiB = -1;
};

struct InGameUICommands
{
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
    UISoundCommands sound;
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
