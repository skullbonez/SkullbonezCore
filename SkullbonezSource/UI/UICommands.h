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
    SunX,
    SunY,
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
    Count
};

struct UIOnlyCommands
{
    bool userInteracted = false;
};

struct UIRendererCommands
{
    bool toggleVsync = false;
    int requestedRendererIndex = -1; // 0=GL, 1=DX11, 2=DX12, -1=no request
};

struct UISceneCommands
{
    bool resetScene = false;
    bool resetSceneDefaults = false;
    bool requestDemoScene = false;
    bool saveSceneDefaults = false;
    int requestedSceneIndex = -1; // index into sceneOptions, -1=no request
};

struct UIPhysicsCommands
{
    bool toggleCollisionVisualizer = false;
    bool togglePhysicsSleepPolicy = false;
    bool togglePhysicsDebugTransparent = false;
    bool toggleBroadphaseOverlay = false;
    float requestedPhysicsDebugAlpha = -1.0f;
    float requestedPhysicsDebugContactLinger = -1.0f;
    uint32_t togglePhysicsDebugFlags = 0;
    bool stepPhysicsPipelinePrevious = false;
    bool stepPhysicsPipelineNext = false;
};

struct UISceneOptionCommands
{
    bool toggleTextOnly = false;
    bool toggleFixedStep = false;
    bool toggleTerrainHidden = false;
    bool toggleWaterHidden = false;
    bool toggleWaterFreeze = false;
    bool toggleWaterFlat = false;
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
    int requestedSeed = -1;
    int requestedSolverBallCount = -1;
    int requestedSolverBoxCount = -1;
};

struct UICinematicCommands
{
    // UI output for one frame. These are requests, not state: the render loop
    // consumes them and mutates the real CinematicRenderConfig.
    // requestedModeSceneIndex uses sceneOptions indexing for concept/cine style
    // scenes; -1 = Demo Scene/default style, -2 = no request this frame.
    bool toggleRendering = false;
    int requestedModeSceneIndex = -2;
    UICinematicFeature requestedFeature = UICinematicFeature::None;
    UICinematicParam requestedParam = UICinematicParam::None;
    float requestedValue = 0.0f;
};

struct InGameUICommands
{
    UIOnlyCommands ui;
    UIRendererCommands renderer;
    UISceneCommands scene;
    UIPhysicsCommands physics;
    UISceneOptionCommands sceneOptions;
    UIWaterCommands water;
    UIRunCommands run;
    UICinematicCommands cinematic;
};

struct InGameUIInputResult
{
    InGameUICommands commands;
};

} // namespace UI
} // namespace SkullbonezCore
