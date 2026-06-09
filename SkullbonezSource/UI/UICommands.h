#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

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

struct InGameUICommands
{
    UIOnlyCommands ui;
    UIRendererCommands renderer;
    UISceneCommands scene;
    UIPhysicsCommands physics;
    UISceneOptionCommands sceneOptions;
    UIWaterCommands water;
    UIRunCommands run;
};

struct InGameUIInputResult
{
    InGameUICommands commands;
};

} // namespace UI
} // namespace SkullbonezCore
