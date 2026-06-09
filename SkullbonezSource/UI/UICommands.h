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

// Compatibility wrapper for existing SkullbonezRun code. New UI code writes to
// commands, then SyncLegacyFields mirrors those values into the old flat fields.
struct InGameUIInputResult
{
    InGameUICommands commands;

    bool userInteracted = false;
    bool toggleVsync = false;
    bool toggleCollisionVisualizer = false;
    bool togglePhysicsSleepPolicy = false;
    bool togglePhysicsDebugTransparent = false;
    bool toggleBroadphaseOverlay = false;
    bool toggleTextOnly = false;
    bool toggleFixedStep = false;
    bool toggleTerrainHidden = false;
    bool toggleWaterHidden = false;
    bool toggleWaterFreeze = false;
    bool toggleWaterFlat = false;
    bool toggleWaterReflection = false;
    bool resetScene = false;
    bool resetSceneDefaults = false;
    bool requestDemoScene = false;
    bool saveSceneDefaults = false;
    float requestedTimeScale = -1.0f;
    float requestedPhysicsDebugAlpha = -1.0f;
    float requestedPhysicsDebugContactLinger = -1.0f;
    int requestedModelCount = -1;
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
    bool stepPhysicsPipelinePrevious = false;
    bool stepPhysicsPipelineNext = false;
    int requestedRendererIndex = -1;
    int requestedWaterReflectionMode = -1;
    int requestedSceneIndex = -1;

    void SyncLegacyFields()
    {
        userInteracted = commands.ui.userInteracted;
        toggleVsync = commands.renderer.toggleVsync;
        requestedRendererIndex = commands.renderer.requestedRendererIndex;

        resetScene = commands.scene.resetScene;
        resetSceneDefaults = commands.scene.resetSceneDefaults;
        requestDemoScene = commands.scene.requestDemoScene;
        saveSceneDefaults = commands.scene.saveSceneDefaults;
        requestedSceneIndex = commands.scene.requestedSceneIndex;

        toggleCollisionVisualizer = commands.physics.toggleCollisionVisualizer;
        togglePhysicsSleepPolicy = commands.physics.togglePhysicsSleepPolicy;
        togglePhysicsDebugTransparent = commands.physics.togglePhysicsDebugTransparent;
        toggleBroadphaseOverlay = commands.physics.toggleBroadphaseOverlay;
        requestedPhysicsDebugAlpha = commands.physics.requestedPhysicsDebugAlpha;
        requestedPhysicsDebugContactLinger = commands.physics.requestedPhysicsDebugContactLinger;
        togglePhysicsDebugFlags = commands.physics.togglePhysicsDebugFlags;
        stepPhysicsPipelinePrevious = commands.physics.stepPhysicsPipelinePrevious;
        stepPhysicsPipelineNext = commands.physics.stepPhysicsPipelineNext;

        toggleTextOnly = commands.sceneOptions.toggleTextOnly;
        toggleFixedStep = commands.sceneOptions.toggleFixedStep;
        toggleTerrainHidden = commands.sceneOptions.toggleTerrainHidden;
        toggleWaterHidden = commands.sceneOptions.toggleWaterHidden;
        toggleWaterFreeze = commands.sceneOptions.toggleWaterFreeze;
        toggleWaterFlat = commands.sceneOptions.toggleWaterFlat;
        requestedTimeScale = commands.sceneOptions.requestedTimeScale;
        requestedModelCount = commands.sceneOptions.requestedModelCount;

        toggleWaterReflection = commands.water.toggleWaterReflection;
        requestWorldGravity = commands.water.requestWorldGravity;
        requestWorldFluidHeight = commands.water.requestWorldFluidHeight;
        requestWorldFluidDensity = commands.water.requestWorldFluidDensity;
        requestedWorldGravity = commands.water.requestedWorldGravity;
        requestedWorldFluidHeight = commands.water.requestedWorldFluidHeight;
        requestedWorldFluidDensity = commands.water.requestedWorldFluidDensity;
        requestedWaterReflectionMode = commands.water.requestedWaterReflectionMode;

        requestedSeed = commands.run.requestedSeed;
        requestedSolverBallCount = commands.run.requestedSolverBallCount;
        requestedSolverBoxCount = commands.run.requestedSolverBoxCount;
    }
};

} // namespace UI
} // namespace SkullbonezCore
