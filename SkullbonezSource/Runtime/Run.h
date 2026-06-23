/*
File: SkullbonezSource/Runtime/Run.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Render pass: A named slice of DrawPrimitives() with explicit inputs,
  outputs, and resource ownership.
  Render target: Texture the renderer draws into before another pass samples or
  presents it.
  DX11/OpenGL: Retired runtime renderers. Their source backends have been
  removed; old command-line values now fail early.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  HDR (High Dynamic Range): Floating-point scene color that can hold values
  brighter than display white until tonemapping resolves it.
  Shadow frame: Per-frame light-space matrices, depth texture handle, and
  filtering constants consumed by shadow receiver shaders.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  SDF (Signed Distance Field): Texture representation used for crisp scalable
  text rendering.

Invariants:
  - RunRenderPassResources owns backend/device resources and must be reset
    while the renderer backend is still alive.
  - Pass input structs borrow frame data only for the current DrawPrimitives()
    call; no pass may store those references after it returns.
  - Shadow receiver pointers are valid only until the next shadow reset or the
    next frame rebuilds ShadowPassResources.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "../Core/Common.h"
#include "../Assets/AssetSystem.h"
#include "CameraCollection.h"
#include "../Core/Timer.h"
#include "Input.h"
#include "InputController.h"
#include "CaptureController.h"
#include "DiagnosticsController.h"
#include "EngineContext.h"
#include "RuntimeCommandQueue.h"
#include "RuntimeViewModel.h"
#include "Replay/ReplayRecorder.h"
#include "Scene/SceneController.h"
#include "SimulationController.h"
#include "../Assets/TextureCollection.h"
#include "Window.h"
#include "../Rendering/Text.h"
#include "../World/Terrain.h"
#include "../World/SkyBox.h"
#include "../Maths/GeometricMath.h"
#include "../GameObjects/GameModelCollection.h"
#include "../World/WorldEnvironment.h"
#include "../Rendering/IFramebuffer.h"
#include "../Rendering/IShader.h"
#include "../Rendering/RenderSceneView.h"
#include "../Rendering/Shadow.h"
#include "../Scene/TestScene.h"
#include "../Physics/Debug/BroadphaseVisualizer.h"
#include "../Physics/Debug/CollisionVisualizer.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "Editor/LauncherLaser.h"
#include "../UI/UI.h"


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
struct SceneRuntimeResetSnapshot;
}

struct TornadoVisualSettings
{
    bool enabled = true;                                                         // Render-only sparse funnel shell; physics force state remains separate.
    bool autoEnableWithTornado = true;                                           // UI/CLI tornado toggles keep the production visual paired by default.
    float shellAlpha = 0.14f;
    float dustAlpha = 0.20f;
    float ribbonWidth = 5.5f;
    int ribbonCount = 7;
    int ribbonSegments = 48;
    int particleCount = 96;
    float rotationSpeed = 1.25f;
};

struct RunRuntimeSettings
{
    bool isVsyncEnabled = true;                                                  // Swap-chain sync interval (true = vsync)
    bool isPipelineSyncEnabled = false;                                          // Force CPU/GPU sync via Finish() before render
    bool isPhysicsSleepEnabled =
        true;                                                                    // Live Catto sleep policy; false keeps bodies awake while leaving collision/solving active
    Physics::TornadoFieldConfig tornadoField;                                    // Live vortex force/debug vector field controlled by CLI/UI
    TornadoVisualSettings tornadoVisual;                                         // Render-only tornado art tuning outside deterministic physics state.
};

struct RunTimerState
{
    Environment::Timer frameTimer;
    Environment::Timer workTimer;
    Environment::Timer updateTimer;
    Environment::Timer cameraTimer;
    Environment::Timer simulationTimer;

    float physicsTime = 0.0f;                                                    // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f;                                             // Smoothed physics time accumulator
    float renderTime = 0.0f;                                                     // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;                                              // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;                                                 // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f;                                             // Half-second averaged kinetic energy
    float cpuFrameWorkMs = 0.0f;                                                 // Last frame CPU work before Present/VSync
    float gpuFrameWorkMs = 0.0f;                                                 // Last available GPU work before Present/VSync
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUIDrawCalls = 0;                                                     // Actual UI draw calls measured around Frame/UI last frame
};

// Concept: pass resource structs name ownership before the frame graph exists.
//
// These structs are intentionally small. They do not implement rendering; they
// make lifetime and ownership visible so each pass can later move behind a
// cleaner interface without rediscovering which framebuffer, shader, or
// per-frame payload belongs to it.
struct ReflectionPassResources
{
    // Lifetime: this render target is backend/device-owned. Resize and backend
    // teardown must reset it before the renderer releases the underlying GPU
    // resource memory.
    std::unique_ptr<Rendering::IFramebuffer> target;
};

struct SkyPassResources
{
    // The cube-map skybox is owned by the shared skybox subsystem. This pass
    // owns only the procedural cinematic atmosphere shader used when cinematic
    // mode asks the sky pass to draw a generated background.
    std::unique_ptr<Rendering::IShader> atmosphereShader;
};

struct CinematicScenePassResources
{
    // Full-resolution floating-point scene color/depth. World geometry renders
    // here first so volumetric light and tonemap can sample the completed scene.
    std::unique_ptr<Rendering::IFramebuffer> hdrTarget;
};

struct VolumetricLightPassResources
{
    // Half-resolution because light shafts are intentionally soft. The shader
    // samples the HDR scene and depth, then writes a texture the tonemap pass can
    // composite over the final image.
    std::unique_ptr<Rendering::IFramebuffer> target;
    std::unique_ptr<Rendering::IShader> shader;
};

struct TonemapPassResources
{
    // Final full-screen resolve from HDR scene color to the window backbuffer.
    // This is also where fog, bloom, grade, and optional volumetric light meet.
    std::unique_ptr<Rendering::IShader> shader;
};

struct FullscreenPassResources
{
    // Shared dynamic vertex buffer for two-triangle full-screen passes. It
    // stores only clip-space position and UV; each shader decides what to sample.
    uint32_t quadVB = 0;
};

struct ShadowPassResources
{
    // Terrain target: broad map centered on terrain bounds. Object target:
    // tighter map centered near the camera so nearby body shadows keep detail.
    std::unique_ptr<Rendering::IFramebuffer> terrainTarget;
    std::unique_ptr<Rendering::IFramebuffer> objectTarget;

    // Lifetime: these payloads borrow texture handles from the targets above.
    // Reset both payloads whenever either target is destroyed, and rebuild them
    // every frame before terrain/object receivers read the pointers.
    Rendering::ShadowFrameData terrainFrame;
    Rendering::ShadowFrameData objectFrame;
    Rendering::ShadowCasterBatches objectCasterBatches;
};

struct RunRenderPassResources
{
    // Ownership map for pass-owned renderer resources. Runtime subsystems keep
    // long-lived world state elsewhere; this aggregate is only for resources
    // created by named render passes and released through pass reset hooks.
    ReflectionPassResources reflection;
    SkyPassResources sky;
    CinematicScenePassResources cinematicScene;
    VolumetricLightPassResources volumetricLight;
    TonemapPassResources tonemap;
    FullscreenPassResources fullscreen;
    ShadowPassResources shadows;
};

struct RunSubsystemState
{
    Assets::AssetSystem assets;
    std::unique_ptr<Geometry::Terrain> terrain;
    bool isFlatSlopeTerrain = false;
    // Lifetime: all pass resources are released before backend teardown/rebuild
    // and lazily recreated by the ensure hooks that own their target size and
    // shader contracts.
    RunRenderPassResources renderPasses;

    Environment::CameraCollection* cameras = nullptr;
    Textures::TextureCollection* textures = nullptr;
    Window* window = nullptr;
    Geometry::SkyBox* skyBox = nullptr;
};

enum class RunCameraMode
{
    Demo = 0,
    Scene,
    Free,
    Launcher,
    Manipulator,
    Count
};

struct RunCameraState
{
    Hardware::InputState input = {};                                             // Snapshot consumed by camera controls for this frame.

    int selectedCamera = 0;                                                      // Keeps track of which camera is selected
    RunCameraMode mode = RunCameraMode::Demo;                                    // Explicit operator camera mode shown in the minimized HUD.
    bool needsMouseLookReset = true;                                             // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    float cameraTime = 0.0f;                                                     // Camera helper clock
    int trackBallIndex = -1;                                                     // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;                                                  // Camera height above tracked ball
    float autoCycleInterval = -1.0f;                                             // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;                                                 // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;                                                 // Number of per-ball screenshots taken so far
};

struct RunLiveStyleControlState
{
    bool enabled = false;                                                        // Polls a small control folder for live style JSON and screenshot requests
    char directory[260] = {};                                                    // Folder containing live.style.json, capture.txt, and status.txt
    char stylePath[300] = {};                                                    // Style descriptor applied without reloading the scene
    char capturePath[300] = {};                                                  // Text command file used to request one screenshot
    char statusPath[300] = {};                                                   // Latest harness status for scripts/humans
    char pendingScreenshotPath[512] = {};                                        // Screenshot path requested by capture.txt
    uint64_t styleStamp = 0;                                                     // Last applied live.style.json write stamp
    uint64_t captureStamp = 0;                                                   // Last consumed capture.txt write stamp
    int styleApplyCount = 0;                                                     // Successful live style applications
    int captureCount = 0;                                                        // Successful live screenshots
    bool hasPendingScreenshot = false;                                           // Capture should run after render/UI this frame
};

enum class OverlayMode
{
    None,                                                                        // Clean screen — nothing shown
    Timers,                                                                      // Renderer name, model count, physics solver, profiler overlay
    SceneStats,                                                                  // Scene telemetry values used by deterministic tests
    BarsNormalized,                                                              // Visual profiler bars — segments fill the bar width (relative)
    BarsAbsolute,                                                                // Visual profiler bars — white = idle/vsync (absolute frame budget)
    Keys,                                                                        // Keyboard reference panel
};

struct RunDebugState
{
    OverlayMode overlayMode =
        OverlayMode::None;                                                       // HUD overlay cycle state (0 key advances through timers, scene stats, bars, and keys)
    bool isWaterFreezeDebug = false;                                             // Freeze ocean animation at current shape (toggle with 1)
    bool isWaterNoReflect = false;                                               // Disable ocean reflection entirely (2 cycles: FBO→DXR→none)
    bool isWaterRTReflect = false;                                               // Use DXR ray-traced reflection (2 cycles: FBO→DXR→none; DXR only if supported)
    bool isWaterFlatDebug = false;                                               // Force ocean mesh fully flat, no displacement (toggle with 3)
    bool isTerrainHidden = false;                                                // Hide terrain mesh (toggle with 4)
    bool isWaterHidden = false;                                                  // Hide water mesh (toggle with 5)
    uint32_t physicsDebugFlags =
        Physics::PHYSICS_DEBUG_NONE;                                             // Draw object axes, contact manifolds, and sleep state (cycle with C)
    bool isPhysicsDebugTransparent =
        false;                                                                   // Draw translucent debug collision volumes behind physics debug lines (toggle with 6)
    float physicsDebugAlpha = 0.28f;                                             // Translucent debug volume alpha
    float physicsDebugContactLinger = 0.45f;                                     // Seconds to keep contact manifolds visible after their solver row disappears
    int physicsDebugPipelineStageCursor = 0;                                     // F7/F8-selected Catto pipeline stage for PHYSICS_DEBUG_PIPELINE
    bool isCollisionVisualizer = false;                                          // Render solid collision/sleep colours for balls and boxes (toggle with V)
    bool isTextOnly = false;                                                     // Suppress all 3D rendering; show solid background with large pangram text
    bool isUITestPattern = false;                                                // Bright 2D backdrop behind UI for visual blur tests
    bool isTopTextHidden = false;                                                // Hide top-left HUD text while leaving other overlays active
    bool isBroadphaseOverlay = false;                                            // Broadphase spatial grid visualizer overlay (toggle with G)
    float frozenWaterTime = 0.0f;                                                // Simulation time captured when freeze was toggled on
#ifdef _DEBUG
    char reproSnapshotMessage[128] = {};                                         // Short HUD confirmation after launcher-mode repro dump
    double reproSnapshotMessageUntil = 0.0;                                      // Simulation timer value after which the HUD message expires
#endif
};

struct RunRayCastTestLine
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    float ageSeconds = 0.0f;
    bool active = false;
    bool hit = false;
};

enum class RunLauncherFireMode
{
    Laser,
    Projectile
};

struct RunRayCastTestState
{
    static constexpr std::size_t MAX_LINES = 64;

    std::array<RunRayCastTestLine, MAX_LINES> lines = {};
    int nextLine = 0;
    RunLauncherFireMode fireMode = RunLauncherFireMode::Laser;
    bool visualizeRays = false;
    float impulseStrength = 1800.0f;
    float projectileSpeed = 160.0f;
};

struct RunMousePickupState
{
    bool active = false;
    bool mouseCaptured = false;
    int modelIndex = -1;
    Math::Vector::Vector3 planePoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 planeNormal = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 grabOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 preservedAngularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 lastImpulse = Math::Vector::ZERO_VECTOR;
};

enum class RunReplayTrack
{
    Presentation,
    Solver
};

struct RunReplayScrubberState
{
    bool visible = false;
    bool dragging = false;
    bool paused = false;
    bool simulationPaused = false;
    bool branchHovered = false;
    bool pauseHovered = false;
    bool pauseRestoreFlyMode = false;
    bool pauseRestoreLauncherMode = false;
    bool mouseCaptured = false;
    bool saveHovered = false;
    bool loadHovered = false;
    bool restoreWasDown = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveHoveredTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    bool leftWasDown = false;
    float position = 1.0f;                                                       // 0 = oldest retained sample, 1 = live edge.
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};

struct RunReplayPathTraceNode
{
    ReplayBodyId id;
    ReplayBodyId parentId;
    ReplayFrameIndex firstFrame = 0;
    Math::Vector::Vector3 contactPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 contactNormal = Math::Vector::ZERO_VECTOR;
    int depth = 0;
};

struct RunReplayPathTarget
{
    ReplayBodyId id;
    int modelIndex = -1;
    char name[64] = {};
};

enum class RunReplayCameraFocusKind
{
    None,
    Body,
    Manifold,
    SolverRow,
    PredictionContact
};

enum class RunReplayCauseTreeRowKind
{
    Body,
    Manifold,
    SolverRow,
    PredictionContact
};

struct RunReplayCameraState
{
    bool active = false;
    RunCameraMode restoreCameraMode = RunCameraMode::Demo;
    bool hasRestorePose = false;
    bool ownsSimulationPause = false;
    uint32_t restoreCameraHash = CAMERA_FREE;
    Math::Vector::Vector3 restoreEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 restoreView = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 restoreUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::None;
    ReplayBodyId focusedId;
    ReplayBodyId counterpartId;
    int focusedRow = -1;
    Math::Vector::Vector3 targetPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 targetNormal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulseVector = Math::Vector::ZERO_VECTOR;
    float targetRadius = 1.0f;
    RunReplayCauseTreeRowKind focusRowKind = RunReplayCauseTreeRowKind::Body;
    int focusModelIndex = -1;
    int focusCounterpartModelIndex = -1;
    int focusContactIndex = -1;
    int focusSolverRowIndex = -1;
    int focusFeatureId = 0;
    bool focusTerrain = false;
};

struct RunReplayCauseTreeRow
{
    RunReplayCauseTreeRowKind kind = RunReplayCauseTreeRowKind::Body;
    ReplayBodyId id;
    ReplayBodyId parentId;
    ReplayBodyId counterpartId;
    ReplayFrameIndex firstFrame = 0;
    int depth = 0;
    int modelIndex = -1;
    int counterpartModelIndex = -1;
    int contactIndex = -1;
    int solverRowIndex = -1;
    int pipelineIndex = -1;
    int featureId = 0;
    int manifoldPointCount = 0;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    float warmStartImpulse = 0.0f;
    float bias = 0.0f;
    float effectiveMass = 0.0f;
    float frictionLimit = 0.0f;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulse = Math::Vector::ZERO_VECTOR;
    bool prediction = false;
    bool terrain = false;
    bool warmStarted = false;
    char name[64] = {};
    char detail[160] = {};
};

struct RunReplayCauseTreeState
{
    std::vector<RunReplayCauseTreeRow> rows;
    int hoveredRow = -1;
    int selectedRow = -1;
    ReplayBodyId focusedId;
    bool hasWindowPlacement = false;
    int x = 0;
    int y = 0;
    int width = 380;
    int height = 420;
    float scrollY = 0.0f;
    bool draggingWindow = false;
    bool resizingWindow = false;
    bool leftWasDown = false;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int resizeStartMouseX = 0;
    int resizeStartMouseY = 0;
    int resizeStartWidth = 0;
    int resizeStartHeight = 0;
};

struct RunReplayPathVisualizerState
{
    bool hasTarget = false;
    ReplayBodyId targetId;
    int targetModelIndex = -1;
    char targetName[64] = {};
    std::vector<RunReplayPathTraceNode> futureNodes;
    std::vector<RunReplayPathTarget> targets;
};

struct RunReplayPredictionBodyBackup
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    float fixedContactHighlightSeconds = 0.0f;
    bool fixed = false;
};

struct RunReplayPredictionBodySample
{
    ReplayBodyId id;
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
};

struct RunReplayPredictionFrame
{
    ReplayFrameIndex frameIndex = 0;
    std::vector<RunReplayPredictionBodySample> bodies;
    std::vector<Physics::PhysicsDebugContact> debugContacts;
};

struct RunReplayPredictionState
{
    bool enabled = false;
    bool checkboxHovered = false;
    bool decreaseHovered = false;
    bool increaseHovered = false;
    bool horizonHovered = false;
    bool horizonDragging = false;
    bool dirty = true;
    bool building = false;
    bool complete = false;
    float horizonSeconds = 10.0f;
    int targetModelIndex = -1;
    int nextTick = 1;
    int targetTickCount = 0;
    ReplayBodyId targetId;
    ReplayFrameIndex sourceFrameIndex = 0;
    uint64_t sourceSolverHash = 0;
    double lastBuildTime = 0.0;
    ReplaySolverWorldSnapshot predictionWorld;
    ReplaySolverWorldSnapshot liveRestoreWorld;
    std::vector<RunReplayPredictionBodyBackup> predictionBodies;
    std::vector<RunReplayPredictionBodyBackup> liveRestoreBodies;
    std::vector<RunReplayPredictionFrame> frames;
    std::vector<RunReplayPathTraceNode> futureNodes;
};

struct RunReplayVelocityEditState
{
    bool enabled = false;
    bool toggleHovered = false;
    bool keyboardAltWasDown = false;
    bool dragging = false;
    bool draggingAngular = false;
    bool mouseCaptured = false;
    bool leftWasDown = false;
    int hotLinearAxis = -1;
    int hotAngularAxis = -1;
    int activeAxis = -1;
    float dragStartAxisT = 0.0f;
    float dragStartAngle = 0.0f;
    Math::Vector::Vector3 dragStartLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 dragStartAngularVelocity = Math::Vector::ZERO_VECTOR;
};

struct RunReplayPoseBackup
{
    int modelIndex = -1;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
};

struct RunLoadedReplayPresentationState
{
    bool enabled = false;
    std::vector<ReplayPresentationSample> samples;
    std::size_t bodyDictionaryCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    char path[260] = {};
};

struct RunReplayV2TargetRestoreResult
{
    std::size_t checkpointCount = 0;
    std::size_t eventCount = 0;
    std::size_t hashCount = 0;
    std::size_t eventsApplied = 0;
    std::size_t bodyCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex checkpointFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    uint32_t eventCursor = 0;
    uint32_t branchId = 0;
    uint32_t parentBranchId = 0;
    uint64_t solverHash = 0;
    uint64_t presentationHash = 0;
    bool generatedTopologyRebuilt = false;
    bool madeLiveBranch = false;
};

#ifdef _DEBUG
struct RunReplayScrubProbeState
{
    bool enabled = false;
    bool completed = false;
    float normalized = 0.25f;
    int minSampleCount = 24;
    float minDistanceSquared = 0.0001f;
};

struct RunReplayRestoreProbeState
{
    bool enabled = false;
    bool completed = false;
    float normalized = 0.25f;
    int minSampleCount = 24;
};

struct RunReplaySaveProbeState
{
    bool enabled = false;
    bool completed = false;
    bool runtimeResetCoverageInjected = false;
    bool eventCoverageInjected = false;
    int minSampleCount = 24;
    char path[260] = {};
};
#endif

struct RunEditorPlacementState
{
    static constexpr std::size_t GIZMO_DRAG_GROUP_CAPACITY = 16;

    bool editorModeEnabled = false;
    bool placementModeEnabled = false;
    bool placeStaticObject = false;
    bool autoTerrainAlign = false;
    RunCameraMode restoreCameraModeAfterEditor = RunCameraMode::Demo;
    bool viewportLookActive = false;
    bool placementPreviewVisible = false;
    bool placementScaleActive = false;
    bool gizmoDragActive = false;
    bool gizmoDragIsRotation = false;
    bool gizmoDragIsScale = false;
    bool altShortcutWasDown = false;
    bool tabShortcutWasDown = false;
    bool tildeShortcutWasDown = false;
    int objectType = UI::EditorTab::OBJECT_BOX;
    int placedObjectSerial = 0;
    int selectedModelIndex = -1;
    int hotGizmoAxis = -1;
    int hotRotationAxis = -1;
    int activeGizmoAxis = -1;
    float gizmoDragStartAxisT = 0.0f;
    float gizmoDragStartRotationAngle = 0.0f;
    float placementYawRadians = 0.0f;
    int placementAltitudeSteps = 0;
    int placementScaleWheelSteps = 0;
    Math::Vector::Vector3 placementTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementCenter = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementRayHit = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScale = Math::Vector::Vector3( 6.0f, 6.0f, 6.0f );
    Math::Vector::Vector3 placementScaleStart = Math::Vector::Vector3( 6.0f, 6.0f, 6.0f );
    Math::Vector::Vector3 placementScaleTerrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 placementScaleRayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion placementOrientation = Math::Orientation::IDENTITY_QUATERNION;
    POINT placementScaleStartClient = {};
    Math::Vector::Vector3 gizmoDragStartPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion gizmoDragStartOrientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::CollisionDetection::CollisionShape gizmoDragStartShape;
    int gizmoDragGroupCount = 0;
    std::array<int, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupIndices = {};
    std::array<Math::Vector::Vector3, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartPositions = {};
    std::array<Math::Orientation::Quaternion, GIZMO_DRAG_GROUP_CAPACITY> gizmoDragGroupStartOrientations = {};
};

class RunEditorTracer
{
  private:
    std::vector<float> m_lineData;

    void EmitLine( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitArrow( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b, float r, float g, float bl );
    void EmitRing( const Math::Vector::Vector3& center, int axis, float radius, float r, float g, float bl );
    void EmitSphere( const Math::Vector::Vector3& center, float radius, float r, float g, float bl );
    void EmitBox( const Math::Vector::Vector3& center,
                  const Math::Vector::Vector3& xAxis,
                  const Math::Vector::Vector3& yAxis,
                  const Math::Vector::Vector3& zAxis,
                  float r,
                  float g,
                  float bl );

  public:
    RunEditorTracer();
    void Clear();
    void AddPlacementRay( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& hitPoint );
    void AddPlacementGhost( int objectType,
                            const Math::Vector::Vector3& center,
                            const Math::Vector::Vector3& terrainPoint,
                            const Math::Vector::Vector3& placementScale,
                            const Math::Orientation::Quaternion& orientation );
    void
    AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, float alpha, bool hit );
    void AddReplayPathSegment( const Math::Vector::Vector3& start,
                               const Math::Vector::Vector3& end,
                               float r,
                               float g,
                               float b );
    void AddReplayContactMarker( const Math::Vector::Vector3& point,
                                 const Math::Vector::Vector3& normal,
                                 float r,
                                 float g,
                                 float b );
    void AddReplayImpulseVector( const Math::Vector::Vector3& point,
                                 const Math::Vector::Vector3& impulse,
                                 float r,
                                 float g,
                                 float b );
    void AddReplayFutureTargetMarker( const Math::Vector::Vector3& center, float radius, int depth );
    void AddReplayTargetMarker( const GameObjects::GameModel& model );
    void AddSelectionOutline( const GameObjects::GameModel& model );
    void AddGizmo( const Math::Vector::Vector3& origin,
                   float radius,
                   int hotTranslateAxis,
                   int hotRotationAxis,
                   int activeAxis,
                   bool activeRotation,
                   bool scaleMode,
                   bool activeScale );
    void AddReplayVelocityGizmo( const GameObjects::GameModel& model,
                                 int hotLinearAxis,
                                 int hotAngularAxis,
                                 int activeAxis,
                                 bool activeAngular );
    void Render( const Math::Transformation::Matrix4& viewProjection );
};

struct RunRequiredContactState
{
    char nameA[64] = {};
    char nameB[64] = {};
    int bodyA = -1;
    int bodyB = -1;
    bool touched = false;
};

struct RunRequiredBroadphaseXCellsState
{
    int minCellX = 0;
    int maxCellX = 0;
    int cellY = 0;
    int cellZ = 0;
    int lastActiveCellCount = 0;
    int lastObservedMinX = 0;
    int lastObservedMaxX = 0;
    int lastMissingCellX = -1;
    bool hasObservedXRange = false;
    bool activated = false;
};

struct RunUIStressState
{
    bool enabled = false;                                                        // Deterministic scene-driven UI stress runner
    unsigned int randomState = 0x7F4A7C15u;                                      // LCG state, seeded from scene UI options
    int actionsPerFrame = 4;                                                     // Cheap UI state mutations per rendered frame
    int framesRun = 0;                                                           // Stress-run frame counter independent of scene resets
};

enum class GeneratedObjectTypeOverride
{
    Mixed,
    AllBalls,
    AllBoxes
};

/* -- Skullbonez Run
---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Run
{

  private:
    // Concept: these private pass contracts are the extraction boundary.
    //
    // DrawPrimitives() still owns pass order, but each pass receives a named
    // input bundle and returns only the data later passes need. References and
    // pointers here are borrowed for one frame; long-lived GPU resources live in
    // RunRenderPassResources instead.
    enum class SkyPassMode
    {
        CubemapOnly,                                                             // Force the authored cube-map skybox path.
        CinematicIfEnabled                                                       // Allow the procedural cinematic sky when the active config requests it.
    };

    enum class ObjectPassMode
    {
        Opaque,                                                                  // Normal body draw before water.
        Transparent                                                              // Debug alpha body draw after water so overlays remain readable.
    };

    struct RenderFrameContext
    {
        // Shared inputs for the ordered world-render passes. This is a borrowed
        // per-frame contract: every value is rebuilt after SetCamera(), consumed
        // during DrawPrimitives(), and discarded before the next frame.
        Math::Transformation::Matrix4 baseView;
        Math::Transformation::Matrix4 projection;
        Math::Transformation::Matrix4 viewProjection;
        Math::Transformation::Matrix4 reflectionView;
        Math::Transformation::Matrix4 reflectionViewProjection;
        Math::Vector::Vector3 eye;
        Math::Vector::Vector3 viewCenter;
        Math::Vector::Vector3 up;
        Math::Vector::Vector3 reflectionEye;
        Math::Vector::Vector3 reflectionCenter;
        Math::Vector::Vector3 reflectionUp;

        // Invariant: ordinary and cinematic lighting both use a directional sun
        // so direct-light shading and shadow maps agree on the same vector.
        float lightPosition[4] = { 200.0f, 400.0f, 1200.0f, 0.0f };
        float waterY = 0.0f;                                                     // World-space fluid surface height used by reflection clipping and water shading.

        // Non-null only when cinematic rendering wraps this frame. Passes use
        // the pointer as an opt-in contract, not as ownership.
        bool cinematicEnabled = false;
        const CinematicRenderConfig* cinematic = nullptr;

        // Renderer-facing scene adapter for model draws, shadow casters, DXR
        // transforms, and debug scene overlays. The adapter is owned by Run;
        // passes borrow it only for this frame.
        Rendering::IRenderSceneView* scene = nullptr;
    };

    struct ObjectPassInputs
    {
        // Object pass view of the body collection. It can act as the opaque
        // pass or the transparent debug pass, but target binding stays with the
        // caller.
        const RenderFrameContext& frame;
        // Documents where this object pass sits in the frame ordering.
        ObjectPassMode mode;
        const CinematicRenderConfig* cinematic;
        const Rendering::ShadowFrameData* shadow;
        bool collisionStateColorsVisible;                                        // Route bodies through collision-state visualization instead of materials.
        float collisionVisualizerAlphaOverride;                                  // -1 keeps visualizer defaults; otherwise overrides debug alpha.
        float bodyAlpha;                                                         // 1 for opaque bodies; debug alpha for the transparent object pass.
        const std::vector<uint8_t>* modelMask;                                   // Optional replay focus mask for split opaque/faded body rendering.
        bool drawMaskedModels;                                                   // True draws only masked bodies; false draws everything outside the mask.
    };

    struct TerrainPassInputs
    {
        // Terrain reads the same camera/light contract as objects, plus the
        // terrain shadow frame when shadows were built for the current frame.
        const RenderFrameContext& frame;
        const CinematicRenderConfig* cinematic;
        const Rendering::ShadowFrameData* shadow;
    };

    struct ReflectionPassInputs
    {
        // Produces the texture sampled by water. The pass may choose the DXR
        // raytraced path or the mirrored-camera render-target path, but both
        // must return a texture handle and matching sample transform.
        const RenderFrameContext& frame;
        const CinematicRenderConfig* cinematic;
        const Rendering::ShadowFrameData* objectShadow;
        bool collisionStateColorsVisible;                                        // Reflection must match the selected body visualization mode.
        // Disables DXR reflection because the mirrored raster path can honor
        // debug alpha and collision-state rendering.
        bool transparentBodyPass;
        float collisionVisualizerAlphaOverride;                                  // Forwarded to reflected collision-state geometry.
        float bodyAlpha;                                                         // Forwarded to reflected production body rendering.
    };

    struct ReflectionPassOutput
    {
        uint32_t reflectionTextureHandle = 0;                                    // Engine texture handle consumed by WorldEnvironment::RenderFluid.
        // Matrix used by water to project the current surface pixel into the
        // reflection texture returned by this pass.
        Math::Transformation::Matrix4 reflectionSampleViewProjection;
        bool usedDxr = false;                                                    // True when the texture came from the DXR dispatch instead of the planar target.
    };

    struct WaterPassInputs
    {
        // Water is deliberately downstream of reflection. It must not rebuild
        // reflection itself; it only receives the texture/sample transform that
        // the reflection pass produced for this frame.
        const RenderFrameContext& frame;
        const ReflectionPassOutput& reflection;
        const CinematicRenderConfig* cinematic;
        bool waterHidden;                                                        // Caller-controlled debug visibility; reflection resources stay outside this flag.
        bool flatWater;                                                          // Debug water style: flat shading instead of animated waves.
        bool noReflection;                                                       // Debug override: keep water visible but force the no-reflection shader path.
        bool freezeTime;                                                         // Debug override: hold wave animation at frozenTime.
        float frozenTime;                                                        // Simulation time captured when water animation was frozen.
    };

    struct WaterPassDebugInfo
    {
        bool rendered = false;
        bool skippedHidden = false;
        bool skippedModeOff = false;
        bool reflectionValid = false;
        bool reflectionRaytraced = false;
        bool noReflection = false;
        bool flatWater = false;
        bool freezeTime = false;
        uint32_t reflectionTextureHandle = 0;
        float waterTime = 0.0f;
        int styleWaterMode = -1;
    };

    struct TornadoVisualPassInputs
    {
        // Production tornado art uses the final world view/depth after opaque
        // objects, terrain, and water. Physics field state is read-only shape input.
        const RenderFrameContext& frame;
    };

    struct DebugOverlayPassInputs
    {
        // Debug overlays draw after production geometry and use the final world
        // view-projection. They do not participate in material or pass-resource
        // ownership.
        const RenderFrameContext& frame;
    };

    struct ShadowPassInputs
    {
        // Shadows are optional. A null cinematic pointer means no shadow maps
        // should be built and receivers should get null shadow outputs.
        const RenderFrameContext& frame;
        const CinematicRenderConfig* cinematic;
    };

    struct ShadowPassOutput
    {
        // Borrowed pointers into ShadowPassResources. Receivers must consume
        // them during the same DrawPrimitives() call; ShadowPass resource
        // release and the next frame both invalidate them.
        const Rendering::ShadowFrameData* terrainShadow = nullptr;
        const Rendering::ShadowFrameData* objectShadow = nullptr;
    };

    /* -- FullscreenQuadPass
    ------------------------------------------------------------------------------------------------------------------------------------

        Shared two-triangle draw surface for generated sky, volumetric light,
        and tonemap. It owns only the dynamic vertex buffer; shader meaning is
        owned by the pass that uses it.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class FullscreenQuadPass
    {
      public:
        explicit FullscreenQuadPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        uint32_t QuadVB() const;

      private:
        Run& m_run;
    };

    /* -- SkyPass
    -----------------------------------------------------------------------------------------------------------------------------------------------

        Draws the current sky into whichever render target the caller has bound.
        The cube-map path samples authored face textures; the cinematic path
        owns a generated-atmosphere shader and uses FullscreenQuadPass.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class SkyPass
    {
      public:
        explicit SkyPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view, SkyPassMode mode );

      private:
        void RenderCinematicSky( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view );

        Run& m_run;
    };

    /* -- SceneTargetPass
    ---------------------------------------------------------------------------------------------------------------------------------------

        Owns the HDR scene target used by cinematic rendering. Begin() binds and
        clears the target, then asks SkyPass to draw the background before world
        geometry is rendered into the target.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class SceneTargetPass
    {
      public:
        explicit SceneTargetPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        bool IsReady() const;
        void Begin( const RenderFrameContext& frame, SkyPass& skyPass );

      private:
        Run& m_run;
    };

    /* -- ShadowPass
    --------------------------------------------------------------------------------------------------------------------------------------------

        Builds terrain/object shadow maps before receiver passes run. It owns
        the shadow targets and the per-frame receiver payloads that terrain and
        object shaders borrow for the rest of DrawPrimitives().
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class ShadowPass
    {
      public:
        explicit ShadowPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame, const CinematicRenderConfig& cinematic );
        void ReleaseGpuResources();
        ShadowPassOutput Render( const ShadowPassInputs& inputs );

      private:
        Rendering::ShadowFrameData BuildTerrainFrameData( const CinematicRenderConfig& cinematic,
                                                          const Math::Vector::Vector3& lightDirectionWorld ) const;
        Rendering::ShadowFrameData BuildObjectFrameData( const CinematicRenderConfig& cinematic,
                                                         const Math::Vector::Vector3& lightDirectionWorld,
                                                         const Math::Vector::Vector3& focusHint,
                                                         Rendering::IRenderSceneView& scene );
        void RenderShadowMap( Rendering::IFramebuffer& target,
                              const Rendering::ShadowFrameData& shadowFrame,
                              const CinematicRenderConfig& cinematic,
                              bool renderTerrain,
                              bool renderObjects,
                              Rendering::IRenderSceneView& scene,
                              const Rendering::ShadowCasterBatches* objectCasters );

        Run& m_run;
    };

    /* -- ReflectionPass
    ----------------------------------------------------------------------------------------------------------------------------------------

        Produces the reflection texture consumed by WaterPass. It chooses DXR
        reflection when possible, otherwise it renders a mirrored scene into the
        planar reflection target.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class ReflectionPass
    {
      public:
        explicit ReflectionPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        ReflectionPassOutput Render( const ReflectionPassInputs& inputs, SkyPass& skyPass );

      private:
        Run& m_run;
    };

    /* -- ObjectPass
    --------------------------------------------------------------------------------------------------------------------------------------------

        Draws production bodies or collision-state solids into the current
        target. The caller chooses whether this is the opaque or transparent
        body pass; this class owns the object shader texture-slot contract.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class ObjectPass
    {
      public:
        explicit ObjectPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const ObjectPassInputs& inputs );

      private:
        Run& m_run;
    };

    /* -- TerrainPass
    -------------------------------------------------------------------------------------------------------------------------------------------

        Draws the terrain mesh with its material texture, cinematic style
        uniforms, and optional shadow receiver payload.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class TerrainPass
    {
      public:
        explicit TerrainPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const TerrainPassInputs& inputs );

      private:
        Run& m_run;
    };

    /* -- WaterPass
    ---------------------------------------------------------------------------------------------------------------------------------------------

        Draws calm/ocean water after reflection has produced its texture. Water
        samples only the reflection slot and never rebuilds reflection itself.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class WaterPass
    {
      public:
        explicit WaterPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const WaterPassInputs& inputs );
        const WaterPassDebugInfo& LastDebugInfo() const
        {
            return m_debugInfo;
        }

      private:
        Run& m_run;
        WaterPassDebugInfo m_debugInfo;
    };

    /* -- TornadoVisualPass
    -------------------------------------------------------------------------------------------------------------------------------------

        Draws sparse production tornado ribbons and dust after opaque world
        depth exists, while leaving debug field vectors in DebugOverlayPass.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class TornadoVisualPass
    {
      public:
        explicit TornadoVisualPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        bool Render( const TornadoVisualPassInputs& inputs );

      private:
        Run& m_run;
        std::vector<float> m_vertices;
    };

    /* -- DebugOverlayPass
    --------------------------------------------------------------------------------------------------------------------------------------

        Draws non-production world overlays after the main scene. These overlays
        are intentionally separate from ObjectPass so debug visuals do not leak
        into material or shadow contracts.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class DebugOverlayPass
    {
      public:
        explicit DebugOverlayPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const DebugOverlayPassInputs& inputs );

      private:
        Run& m_run;
    };

    /* -- VolumetricPass
    ----------------------------------------------------------------------------------------------------------------------------------------

        Reads the completed HDR scene color/depth target and writes a
        half-resolution light-shaft texture for TonemapPass to composite.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class VolumetricPass
    {
      public:
        explicit VolumetricPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        bool Render( const RenderFrameContext& frame );

      private:
        Run& m_run;
    };

    /* -- TonemapPass
    -------------------------------------------------------------------------------------------------------------------------------------------

        Resolves the HDR scene target back to the window backbuffer. It owns the
        final post shader contract: scene color, scene depth, optional
        volumetric light, and cinematic grading uniforms.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class TonemapPass
    {
      public:
        explicit TonemapPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const RenderFrameContext& frame, bool sceneAlreadyUnbound, bool volumetricReady );

      private:
        Run& m_run;
    };

    /* -- UiTextPass
    --------------------------------------------------------------------------------------------------------------------------------------------

        Named 2D pass for the existing HUD, in-game UI, and SDF text renderer.
        It leaves UI layout code in the UI subsystem but gives the frame loop a
        pass-level Render/Release contract like the 3D passes.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class UiTextPass
    {
      public:
        explicit UiTextPass( Run& run ) : m_run( run )
        {
        }

        void EnsureGpuResources();
        void ReleaseGpuResources();
        bool ShouldRender() const;
        void Render( double secondsPerFrame );

      private:
        Run& m_run;
    };

    SceneController m_sceneController;                                           // Owns scene queue and current scene-run state
    std::vector<std::string> m_sceneBrowserPaths;
    std::vector<std::string> m_sceneBrowserNames;
    std::vector<const char*> m_sceneBrowserNamePtrs;
    bool m_leftSceneCycleWasDown = false;
    bool m_rightSceneCycleWasDown = false;
    double m_lastEscapeTapTime = -1000.0;
    float m_cmdTimeScaleOverride = 0.0f;                                         // CLI --time-scale override applied after each scene load (0 = not set)
    bool m_cmdFixedStep = false;                                                 // CLI --fixed-step override applied after each scene load
    unsigned int m_cmdSeedOverride = 0;                                          // CLI --seed override applied after each scene load (0 = not set)
    bool m_cmdNoWater = false;                                                   // CLI --no-water starts fluid below terrain
    bool m_cmdNoSleep = false;                                                   // Startup CLI --no-sleep request; the live policy can still be toggled from the Physics tab
    bool m_cmdHasTornadoOverride = false;
    bool m_cmdTornadoEnabled = false;
    bool m_cmdTornadoVectors = false;
    bool m_cmdHasCinematicRenderingOverride = false;
    bool m_cmdCinematicRendering = false;
    bool m_cmdHasCinematicShadowsOverride = false;
    bool m_cmdCinematicShadows = false;
    bool m_cmdDemoHeroStyle = false;                                             // CLI --demohero applies the low-poly hero look to generated demo mode
    bool m_cmdInteractiveSceneRun = false;                                       // CLI --interactive/--hold keeps scene automation from quitting the app
    int m_cmdFrameCountOverride = -1;                                            // CLI --frames override applied after each scene load
    bool m_cmdUIStress = false;                                                  // CLI --ui-stress enables generated/demo stress without a scene file
    unsigned int m_cmdUIStressSeed = 0;                                          // CLI --ui-stress-seed
    int m_cmdUIStressActions = 5;                                                // CLI --ui-stress-actions
    int m_selectedCineModeSceneIndex = -1;                                       // -1=Demo/default look, otherwise scene-browser index of live cine/concept look
    CinematicRenderConfig m_defaultCinematicRender;                              // engine.cfg cinematic baseline restored by the Demo Scene cine mode
    int m_startupGameModelCapacity = DEFAULT_GAME_MODEL_CAPACITY;
    int m_startupWorkerThreads = -1;
    GeneratedObjectTypeOverride m_generatedObjectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    float m_UITimeScaleOverride = 0.0f;
    int m_UIModelCountOverride = -1;
    int m_UISolverBallCountOverride = -1;
    int m_UISolverBoxCountOverride = -1;
    bool m_cmdHasPhysicsDebugFlagsOverride = false;
    uint32_t m_cmdPhysicsDebugFlagsOverride = Physics::PHYSICS_DEBUG_NONE;
    bool m_cmdHasPhysicsDebugTransparentOverride = false;
    bool m_cmdPhysicsDebugTransparentOverride = false;
    bool m_cmdHasPhysicsDebugAlphaOverride = false;
    float m_cmdPhysicsDebugAlphaOverride = 0.28f;
    bool m_cmdHasPhysicsDebugContactLingerOverride = false;
    float m_cmdPhysicsDebugContactLingerOverride = 0.45f;

    DiagnosticsController m_diagnostics;                                         // Perf/test logs and queryable physics diagnostic trace
#ifdef _DEBUG
    RunReplayScrubProbeState m_replayScrubProbe;                                 // CLI-only SkullScope replay scrub self-test state.
    RunReplayRestoreProbeState m_replayRestoreProbe;                             // CLI-only solver restore hash self-test state.
    RunReplaySaveProbeState m_replaySaveProbe;                                   // CLI-only v2 replay artifact save self-test state.
#endif
    RunRuntimeSettings m_runtimeSettings;                                        // Scene/app runtime swap policy toggles
    RunTimerState m_timers;                                                      // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                                                 // Window, camera, texture, terrain, and pass resource ownership
    RuntimeInputContext m_runtimeInput;                                          // Semantic input mode/action state owned by input routing.
    RunCameraState m_camera;                                                     // Camera/input state and ball-tracking settings
    SimulationController m_simulation;                                           // Simulation timestep policy and physics accumulators
    ReplayRecorder m_replay;                                                     // Bounded replay presentation recorder for recent-frame inspection.
    ReplaySolverRecorder m_solverReplay;                                         // Bounded same-tick solver-state recorder kept in tandem with presentation replay.
    ReplayEventRecorder m_replayEvents;                                          // Bounded intent/event stream kept beside v2 replay tracks.
    ReplayBranchInfo m_replayBranch;                                             // Current live replay branch provenance.
    RunLoadedReplayPresentationState
        m_loadedPresentationReplay;                                              // Optional v2 file-backed presentation samples for smooth scrub playback.
    RunReplayScrubberState m_replayScrubber;
    RunReplayCameraState m_replayCamera;                                         // Runtime Replay Camera focus/restore state for scrub and Cause inspection.
    RunReplayPathVisualizerState
        m_replayPathVisualizer;                                                  // Mouse-selected cause/effect trace over retained solver replay samples.
    RunReplayPredictionState m_replayPrediction;                                 // Optional live solver lookahead for the selected replay path target.
    RunReplayCauseTreeState m_replayCauseTree;                                   // Right-side object hierarchy for the active replay cause/effect chain.
    RunReplayVelocityEditState m_replayVelocityEdit;                             // Alt-enabled live velocity handles feeding the prediction cache.
    std::vector<uint8_t> m_replayFocusModelMask;                                 // Render-only body mask for selected replay prediction chains.
    std::vector<RunReplayPoseBackup> m_replayPoseBackups;
    ReplayLauncherVisualSample m_replayLauncherVisualBackup;
    bool m_replayLauncherVisualBackupActive = false;
    uint32_t m_solverReplayMismatchReports = 0;
    bool m_solverReplayMismatchSuppressed = false;
    CaptureController m_capture;                                                 // Screenshot trigger and capture state
    RunLiveStyleControlState m_liveStyle;                                        // Live style tweak/capture harness state
    UI::InGameUI m_UI;                                                           // Encapsulated in-game diagnostics window
    RunDebugState m_debug;                                                       // Runtime debug/overlay toggles
    RunRayCastTestState m_rayCastTest;                                           // Launcher-mode firing state and fading debug lines
    RunMousePickupState m_mousePickup;                                           // Manipulator-mode click-drag physics pickup state.
    RunEditorPlacementState m_editor;                                            // Object placement and selection editor state
    RunEditorTracer m_editorTracer;                                              // Render-only ray tests, ghost previews, and editor gizmo lines
    std::vector<RunRequiredContactState> m_requiredSceneContacts;
    std::vector<RunRequiredBroadphaseXCellsState> m_requiredBroadphaseXCells;
    RunUIStressState m_uiStress;                                                 // Deterministic UI stress run state
    Physics::BroadphaseVisualizer m_broadphaseVisualizer;                        // Spatial grid debug overlay (G key toggle)
    Physics::CollisionVisualizer m_collisionVisualizer;                          // Solid collision/sleep model visualizer (V key toggle)
    Physics::PhysicsDebugVisualizer
        m_physicsDebugVisualizer;                                                // Line overlay for object axes, contact manifolds, and sleep state
    LauncherLaser m_launcherLaser;                                               // Visible launcher-mode laser shots; render-only feedback.
    Environment::WorldEnvironment m_cWorldEnvironment;                           // Fluid, gravity, and terrain bounds shared by physics and water.
    GameObjects::GameModelCollection m_cGameModelCollection;                     // Scene bodies plus solver-visible object state.
    RuntimeCommandQueue m_runtimeCommands;                                       // Deferred runtime/tool command intent.
    EngineContext m_engineContext;                                               // Bound view over runtime-owned systems.
    RuntimeViewModel m_runtimeViewModel;                                         // Scalar runtime snapshot for presentation/diagnostics.
    std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms = {};
    FullscreenQuadPass m_fullscreenQuadPass;                                     // Shared full-screen vertex buffer pass used by sky/post effects
    SkyPass m_skyPass;                                                           // Background sky pass, reused by reflection and scene target passes
    SceneTargetPass m_sceneTargetPass;                                           // Cinematic HDR scene-target begin/release pass
    ShadowPass m_shadowPass;                                                     // Terrain/object shadow-map producer pass
    ReflectionPass m_reflectionPass;                                             // Water reflection texture producer pass
    ObjectPass m_objectPass;                                                     // Production body and collision-solid pass
    TerrainPass m_terrainPass;                                                   // Terrain material/shadow receiver pass
    WaterPass m_waterPass;                                                       // Calm/ocean water pass
    TornadoVisualPass m_tornadoVisualPass;                                       // Sparse alpha tornado shell/dust pass
    DebugOverlayPass m_debugOverlayPass;                                         // Broadphase and physics debug overlay pass
    VolumetricPass m_volumetricPass;                                             // Half-resolution cinematic light-shaft pass
    TonemapPass m_tonemapPass;                                                   // HDR-to-backbuffer resolve pass
    UiTextPass m_uiTextPass;                                                     // HUD/UI/text pass

    inline static int sPerfPass = 0;
    void Render();                                                               // Skips 3D in text-only runs, then records passes for the current camera state.
    RunSceneState& SceneState();                                                 // Mutable scene-run state owned by SceneController
    const RunSceneState& SceneState() const;                                     // Read-only scene-run state owned by SceneController
    void BindEngineContext();                                                    // Binds runtime-owned systems into EngineContext
    void RefreshRuntimeViewModel();                                              // Rebuilds scalar presentation state from EngineContext
    void RelativeUpdateCamera( uint32_t hash );                                  // Keeps non-selected relative cameras inside terrain height limits.
    void UpdateLogic( float simulationDt, float cameraDt );                      // simulationDt drives physics; cameraDt is unscaled wall time.
    void TakeInput();                                                            // Applies focused input to camera, UI, scene cycling, diagnostics, and editor tools.
    bool DrainRuntimeCommands();                                                 // Applies queued runtime/tool command intents at the frame boundary.
    void StepPhysicsPipelineStage( int direction );                              // direction is a left/right cursor step for pipeline visualization.
    void UpdateRuntimeInputModeAfterAction(
        RuntimeInputAction action,
        RuntimeInputActionSource source );                                       // Records the mode transition caused by one runtime/tool action.
    bool ReplayInspectionActive() const;                                         // True when replay owns inspection camera semantics.
    bool ReplayInspectionMouseLookActive() const;                                // True when replay inspection is consuming mouse-look.
    bool MouseLookOwnsCursor() const;                                            // True when camera/editor/replay mouse-look should hide the system cursor.
    bool ShouldHideNativeCursor() const;                                         // True when the current tool mode should hide the Windows cursor.
    void ApplyCursorOwnership();                                                 // Applies current cursor ownership to the system cursor.
    void ReleaseMouseToUI();                                                     // Gives mouse focus back to Win32/UI when tools stop owning it.
    void EnterFlyModeCamera();                                                   // Switches camera state into free-flight controls.
    void ExitFlyModeCamera();                                                    // Restores terrain camera bounds and leaves launcher mode.
    const char* CameraModeLabel( RunCameraMode mode ) const;                     // Compact name for UI and transition diagnostics.
    uint32_t CameraModeEnabledMask() const;                                      // One bit per camera mode; disabled modes remain visible in UI.
    bool IsDemoCameraModeAvailable() const;                                      // True when Demo can track at least one live model.
    RunCameraMode NormalizeCameraModeForCurrentScene(
        RunCameraMode mode ) const;                                              // Clamps passive camera modes to generated-demo vs authored-scene ownership.
    bool IsFlyCameraMode() const;                                                // True when the current mode uses free-flight camera controls.
    bool IsLauncherCameraMode() const;                                           // True when the current mode owns launcher firing semantics.
    bool IsManipulatorCameraMode() const;                                        // True when mouse pickup owns world left-drag semantics.
    void ApplyCameraMode( RunCameraMode mode,
                          RuntimeInputActionSource source );                     // Applies keyboard/UI camera-mode requests.
    void CycleCameraMode();                                                      // Tab cycles through enabled explicit camera modes.
    void SetUpCameras();                                                         // Creates generated-demo cameras when no scene file supplies them.
    void SetUpCamerasFromScene(
        const TestScene& scene );                                                // Applies authored camera records without disturbing scene automation gates.
    void SetUpGameModels( int count );                                           // Populates generated mixed-object scenes for legacy launch paths.
    void SetUpSolverObjects( int balls, int boxes );                             // Populates deterministic solver scenes with exact ball/box counts.
    void SetUpGameModelsFromScene(
        const TestScene& scene );                                                // Converts authored scene models into runtime objects and solver bodies.
    void SetUpRequiredContactsFromScene( const TestScene& scene );
    void SetUpRequiredBroadphaseXCellsFromScene( const TestScene& scene );
    void UpdateRequiredSceneContacts();                                          // Scene automation waits for authored contact gates to appear in live physics
                                        // contacts.
    void UpdateRequiredSceneBroadphaseXCells(
        const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
        int activeCellCount );                                                   // Scene automation waits for authored X-cell ranges to appear in the live grid.
    bool RequiredSceneContactsComplete() const;                                  // True when there are no gates or all gates have been touched
    bool RequiredSceneBroadphaseXCellsComplete()
        const;                                                                   // True when there are no gates or all X-cell ranges have been activated
    void RegisterBuiltInAssets();                                                // Seeds source asset records before renderer-owned resources are rebuilt.
    std::string ResolveSourceAssetPath( Assets::AssetKind kind,
                                        const char* logicalName,
                                        const std::string& relativePath );       // Resolves DATA_ROOT path while preserving
                                                                           // source asset identity for rebuilds.
    void DrawPrimitives();                                                       // Orders terrain, object, helper, water, post, and overlay passes for one frame.
    RenderFrameContext BuildRenderFrameContext(
        bool cinematicRender,
        const CinematicRenderConfig& renderConfig );                             // Names per-frame camera/light inputs consumed by render passes
    CinematicRenderConfig& ActiveCinematicConfig();                              // Mutable cinematic style config for the active scene/run
    const CinematicRenderConfig& ActiveCinematicConfig() const;                  // Read-only cinematic style config for the active scene/run
    bool IsCinematicRenderingEnabled() const;                                    // True when the HDR/post stack should wrap the main scene
    void ReleaseBackendOwnedRenderResources(
        const char* phaseName );                                                 // Ordered GPU-resource release hook while the backend is alive.
    void RebuildRegisteredRenderResources();                                     // Recreates renderer resources from source asset records
    void LogRenderResourceLifecycleStep( const char* phase, const char* step )
        const;                                                                   // Debug event log record for a named resource-lifetime phase.
    Textures::TextureCollection& Textures();                                     // Runtime texture registry accessor used by render passes
    uint32_t TextureHandle( uint32_t textureHash );                              // Resolves a runtime texture hash to a renderer handle
    void SelectRenderTexture( uint32_t textureHash );                            // Runtime texture hash selected for the default draw texture slot.
    int WindowScreenWidth() const;                                               // Client width, falling back to config before window init.
    int WindowScreenHeight() const;                                              // Client height, falling back to config before window init.
    void SetViewingOrientation();                                                // Camera-view setup for the current frame.
    void SaveScreenshot( const char* path );                                     // Backbuffer capture path; current encoder writes BMP files.
    bool SaveReplayBufferFromScrubber( RunReplayTrack track );                   // Writes one retained in-memory replay track to replays/.
    bool PromptLoadReplayPresentationArtifact( HWND hwnd );                      // Open a .skreplay picker for a v2 scrub source.
    bool SaveCurrentSceneDefaults();                                             // UI-controlled scene defaults persisted to the active scene file.
    bool SaveCurrentEditableSceneSnapshot();                                     // UI-created scenes persist live models plus starter-scene defaults.
    bool SaveRenderDefaults();                                                   // Ordinary Render-tab values persisted to engine.cfg.
    void RefreshSceneBrowserList();                                              // Discovers scene files available to the in-game scene dropdown
    int CurrentSceneBrowserIndex() const;                                        // Selected discovered-scene dropdown index.
    bool CreateSceneFromUI( const char* requestedName );                         // Creates and loads a flat starter scene from the Scene tab.
    void LoadSceneFromBrowserIndex( int index );                                 // In-game scene dropdown selection loader.
    void LoadDemoSceneFromUI();                                                  // Scene-tab entry point for the generated demo scene.
    bool ApplyCinematicModeFromBrowserIndex( int index );                        // Live cine/concept style change; leaves scene objects intact.
    bool ApplyAdjacentCinematicMode( int direction );                            // Cycles live cine/concept looks without rebuilding the scene
    void ApplyLiveStyleScene( const TestScene& styleScene );                     // Style-only cinematic/material JSON; no object rebuild.
    void ApplyDemoHeroStyleOverride();                                           // Low-poly hero style override for generated demo mode.
    void LoadAdjacentSceneFromBrowser( int direction );                          // Keyboard scene cycling through the discovered scene dropdown list
    void EnterInteractiveSceneRun();                                             // Locks scene automation into non-quitting interactive mode
    bool CanSceneAutomationQuit() const;                                         // True for CLI suites/tests; false once the user owns scene flow
    void HoldCompletedInteractiveScene();                                        // Keep the current scene alive after interactive automation completes
    bool HasSceneQueueEntry( int index ) const;                                  // True when index points at a queued scene/demo entry
    bool HasCurrentSceneQueueEntry() const;                                      // True when currentSceneIndex points at a queued entry
    const std::string* CurrentSceneQueuePath() const;                            // Queued scene path; nullptr means no current entry.
    RunInternal::SceneRuntimeResetSnapshot
    CaptureSceneRuntimeResetSnapshot();                                          // Captures live runtime controls before a scene reset rebuilds objects
    void RestoreSceneRuntimeResetSnapshot(
        const RunInternal::SceneRuntimeResetSnapshot& snapshot,
        bool suppressExitOnComplete );                                           // Restores preserved live controls after scene file/defaults rebuild
    void ClearSceneRuntimeUIOverrides();                                         // New scene/defaults should become authoritative again.
    void LogPerfMemory( const char* checkpoint );                                // Log memory usage to perf CSV
    void LoadScene(
        int index,
        bool preserveUIState = false,
        bool suppressExitOnComplete = false,
        bool preserveRuntimeState = false );                                     // Queue-indexed scene load; preserve flags keep selected runtime/UI state.
    void ResetCurrentScene(
        bool preserveUIState = false,
        bool suppressExitOnComplete = false,
        bool preserveRuntimeState = true );                                      // User-triggered reset/reload of current scene or generated demo mode
    void ApplyUIModelCountOverride( int count );                                 // Rebuilds the active generated model pool from the UI slider
    void ApplyUISolverObjectCounts( int balls, int boxes );                      // Rebuilds generated solver objects from exact UI counts
    void ApplyUIWorldOverride( float gravity,
                               float fluidHeight,
                               float fluidDensity );                             // Live world/fluid scalar override from UI controls.
    void ApplyConfiguredWorldEnvironment();                                      // Restores engine.cfg world/fluid defaults for a fresh scene load.
    void ApplyNoWaterOverride();                                                 // Pushes fluid surface below the active terrain when requested
    void ApplyTornadoDefaultsForActiveScene();                                   // Centers the tornado around the active inner-water/basin region
    void SyncTornadoFieldToPhysics();                                            // Sends live tornado state to the physics collection
    void UseDefaultTerrain();                                                    // Restores the normal height-map terrain when leaving analytic test scenes
    void UseFlatSlopeTerrain( float baseY,
                              float slopeX,
                              float slopeZ );                                    // Activates analytic flat-slope terrain for focused physics scenes
    void UpdateWorldTerrainBounds();                                             // Keeps world/fluid helpers aligned with the active terrain bounds
    bool AdvanceScene();                                                         // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty,
                     float mouseMovemementQty );                                 // Keyboard/mouse deltas dispatched to CameraCollection.
    // Tight light-space frame for nearby object receivers.
    // Depth casters requested from the sun view.
    unsigned int NextUIStressRandom();
    int NextUIStressInt( int maxExclusive );
    float NextUIStressFloat( float minValue, float maxValue );
    void RunUIStressActions();
    void ResetReplayTimelineForActiveScene(
        bool preserveBranchMetadata = false );                                   // Scene/model rebuilds start a fresh in-memory replay branch.
    ReplayFrameIndex NextReplayEventFrameIndex() const;                          // Event frame cursor matching the next captured physics tick.
    void RecordReplayEvent( ReplayEventKind kind,
                            ReplayFrameIndex frameIndex,
                            uint32_t flags,
                            int32_t value0,
                            int32_t value1,
                            int32_t value2,
                            int32_t value3,
                            uint64_t data0,
                            const char* text );                                  // Appends a bounded v2 event-stream row when replay is active.
    void RecordReplayWorldOverrideEvent(
        float previousGravity,
        float previousFluidHeight,
        float previousFluidDensity,
        float gravity,
        float fluidHeight,
        float fluidDensity );                                                    // Records exact world scalar payloads for future event replay.
    void RecordReplayLauncherConfigEvent(
        uint32_t changedFlags );                                                 // Records launcher settings that affect future fire events.
    void RecordReplayLauncherFireEvent(
        const Math::Vector::Vector3& rayOrigin,
        const Math::Vector::Vector3& rayDirection,
        const Math::Vector::Vector3& cameraUp );                                 // Records camera-derived launcher fire payloads.
    void RecordReplayGeneratedSceneConfigEvent();                                // Records generated-scene object counts and seed metadata.
    void
    RecordReplayEditorPlaceEvent( int objectType,
                                  bool fixedObject,
                                  bool terrainAlign,
                                  int modelCountBefore,
                                  const Math::Vector::Vector3& terrainPoint,
                                  const Math::Vector::Vector3& placementScale,
                                  float placementYawRadians );                   // Records editor placement commits for saved v2 replay.
    void
    RecordReplayEditorTransformEvent( int modelIndex,
                                      uint32_t changedFlags,
                                      const GameObjects::GameModel& model,
                                      int scaleAxis,
                                      float scaleFactor );                       // Records committed editor transform/scale gizmo changes.
    void CaptureReplayPhysicsStep();                                             // Capture-only hook after one committed fixed physics tick.
    static void CaptureReplayPhysicsStepThunk( void* userData );
    void AfterPhysicsStep();                                                     // Post-step hooks that must see committed physics state.
    static void AfterPhysicsStepThunk( void* userData );
    void ApplyMousePickupPhysicsStep();                                          // Manipulator spring impulse before one fixed physics step.
    void RestoreMousePickupAngularVelocity();                                    // Holds grabbed body angular velocity stable during drag.
    static void ApplyMousePickupPhysicsStepThunk( void* userData );
    void BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const;
    void RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample );
    bool ApplyReplayEventForRestoreTarget(
        const ReplayEventSample& event,
        char* outReason,
        std::size_t reasonSize );                                                // Applies loaded v2 event payloads without recording new replay rows.
    void ClearReplayPathVisualizer();
    bool TryPickReplayPathTargetFromMouse( bool additive, bool clearOnMiss );
    void MarkReplayPredictionDirty();
    void ClearReplayPredictionCache();
    void CancelReplayPredictionJob( bool clearSamples );
    bool BeginReplayPredictionJob( ReplayFrameIndex sourceFrameIndex, uint64_t sourceSolverHash );
    bool StepReplayPredictionJob( double budgetMilliseconds );
    bool CaptureReplayPredictionBodyState( std::vector<RunReplayPredictionBodyBackup>& outBodies );
    bool ApplyReplayPredictionBodyState( const std::vector<RunReplayPredictionBodyBackup>& bodies );
    void CaptureReplayPredictionFrame( ReplayFrameIndex frameIndex );
    void RenderReplayPredictionVisualizer( RunEditorTracer& tracer );
    void RenderReplayPathVisualizer( RunEditorTracer& tracer );
    bool BuildReplayFocusModelMask();
    bool BuildReplayCauseTreeRows();
    bool TickReplayCauseTreeInput( HWND hwnd, bool uiBlocksMouse, int wheelDelta );
    bool TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id,
                                                Math::Vector::Vector3& outPosition,
                                                float* outRadius = nullptr ) const;
    bool FocusReplayCauseTreeBody( ReplayBodyId id );
    void ActivateReplayCameraForCauseRow( const RunReplayCauseTreeRow& row, int rowIndex );
    void ClearReplayCameraFocus( bool restoreCamera );
    void RenderReplayCauseFocusOverlay( RunEditorTracer& tracer );
    void RenderReplayCauseTreeOverlay();
    void SetReplayVelocityEditEnabled( bool enabled );
    bool TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse );
    int ResolveReplayVelocityEditModelIndex() const;
    int HitReplayVelocityLinearAxis( const Math::Vector::Vector3& rayOrigin,
                                     const Math::Vector::Vector3& rayDirection ) const;
    int HitReplayVelocityAngularAxis( const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection ) const;
    bool TryReplayVelocityAxisRayParameter( int axis,
                                            const Math::Vector::Vector3& rayOrigin,
                                            const Math::Vector::Vector3& rayDirection,
                                            float& outAxisT ) const;
    bool TryReplayVelocityAngularRayAngle( int axis,
                                           const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection,
                                           float& outAngle ) const;
    void ApplyReplayVelocityEditDrag( const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection );
    void ApplyReplayVelocityEditToModel( int modelIndex,
                                         const Math::Vector::Vector3& linearVelocity,
                                         const Math::Vector::Vector3& angularVelocity );
    void RenderReplayVelocityEditOverlay( RunEditorTracer& tracer );
    bool HasLoadedReplayPresentation() const;
    const ReplayPresentationSample* LoadedReplayPresentationSampleAtNormalized( float normalized ) const;
    const ReplayPresentationSample* LoadedReplayPresentationLatestSample() const;
    void ArmLoadedReplayPresentationScrubber( float normalized );
    void ResetReplayScrubber();
    void SetReplaySimulationPaused( bool paused );
    void EnterReplayInspectionCamera();
    void ExitReplayInspectionCamera();
    void UpdateReplayInspectionCamera();
    void CompareLatestReplaySamples();
    bool TickReplayScrubberInput( HWND hwnd, bool uiBlocksMouse );
    bool ShouldRenderReplayScrubber() const;
    bool IsReplayScrubPaused() const;
    bool RestoreReplayScrubberSelectionAsLive( double now,
                                               RunReplayV2TargetRestoreResult* outV2Result = nullptr,
                                               char* outReason = nullptr,
                                               std::size_t reasonSize = 0 );
    const ReplayPresentationSample* CurrentReplayScrubSample() const;
    const ReplaySolverFrameSample* CurrentReplaySolverScrubSample() const;
    void RenderReplayScrubberOverlay();
    bool ApplyReplayPresentationSampleForRender( const ReplayPresentationSample& sample );
    bool ApplyReplaySolverSampleForRender( const ReplaySolverFrameSample& sample );
    void RestoreReplayPresentationRenderPose();
    void ApplyReplayLauncherVisualSampleForRender( const ReplayLauncherVisualSample& sample );
    void RestoreReplayLauncherVisualForRender();
    bool ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize );
    bool CaptureCurrentReplaySolverHash( const ReplaySolverFrameSample& reference,
                                         uint64_t& outSolverHash,
                                         uint64_t& outPresentationHash,
                                         std::size_t& outBodyCount );
    bool RestoreReplayV2ArtifactTargetState( const char* path,
                                             ReplayFrameIndex requestedFrame,
                                             bool makeLiveBranch,
                                             RunReplayV2TargetRestoreResult& outResult,
                                             char* outReason,
                                             std::size_t reasonSize );
    bool
    RestoreReplaySolverSampleAsLive( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize );

    // --- Per-frame tick helpers (called from Execute()) ---
    void TickPhysics( double dt );                                               // Physics dispatch: fixed-step and variable-step accumulator
    bool TickScreenshots();                                                      // Screenshot triggers; returns true when frame should restart (continue)
    void TickLiveStyleControl();                                                 // Poll live.style.json/capture.txt and apply look changes without scene reload
    void TickLiveStyleControlCapture();
    void TickAutoCycle();                                                        // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    void TickPerfLog();                                                          // Write per-frame perf CSV row and periodic memory checkpoint
    bool TickSceneAdvance();                                                     // Frame count, exit/hold on completion, restarts; returns true to continue
    void UpdateWaterHeightControls( float dt );                                  // Slide water surface up/down while held
    void ClearRayCastTestLines();                                                // Scene/model rebuilds invalidate fading launcher visuals.
    void AddRayCastTestLine( const Math::Vector::Vector3& start,
                             const Math::Vector::Vector3& end,
                             bool hit );                                         // One fading ray visual, gated by runtime test-line visibility.
    void TickRayCastTestLines( float dt );                                       // Ages fading launcher visuals
    bool TryRayCastTestHit( const Math::Vector::Vector3& rayOrigin,
                            const Math::Vector::Vector3& rayDirection,
                            float maxDistance,
                            int& outIndex,
                            float& outT );                                       // Finds closest model hit along a ray
    bool TryLauncherTerrainHit( const Math::Vector::Vector3& rayOrigin,
                                const Math::Vector::Vector3& rayDirection,
                                float maxDistance,
                                float& outT ) const;                             // Finds the nearest terrain crossing along a launcher ray.
    void FireRayCastTest();                                                      // Dispatches the selected launcher-mode fire action.
    void FireLauncherLaser( const Math::Vector::Vector3& rayOrigin,
                            const Math::Vector::Vector3& rayDirection,
                            const Math::Vector::Vector3& cameraUp );             // Casts a runtime test ray, draws the laser, and
                                                                     // applies impulse to the first dynamic hit.
    void
    FireLauncherProjectile( const Math::Vector::Vector3& rayOrigin,
                            const Math::Vector::Vector3& rayDirection,
                            const Math::Vector::Vector3& cameraUp );             // Shoots a small dynamic sphere from the camera.
    bool TryBuildMouseWorldRay( Math::Vector::Vector3& outOrigin, Math::Vector::Vector3& outDirection )
        const;                                                                   // Mouse position projected into a world-space ray.
    bool TryGetMouseTerrainPlacement(
        Math::Vector::Vector3& outPosition ) const;                              // Raycast current mouse position to terrain for editor placement
    bool
    TryGetMouseTerrainPlacement( Math::Vector::Vector3& outPosition,
                                 Math::Vector::Vector3* outRayOrigin,
                                 Math::Vector::Vector3* outRayDirection ) const; // Raycast with optional ray output
    bool TryComputeEditorObjectCenter( int objectType,
                                       const Math::Vector::Vector3& terrainPoint,
                                       const Math::Vector::Vector3& placementScale,
                                       const Math::Orientation::Quaternion& orientation,
                                       Math::Vector::Vector3& outCenter )
        const;                                                                   // Terrain hit converted to object center; false when placement is invalid.
    bool TryComputeEditorPlacementPreview( int objectType );                     // Snapped ghost placement data from the mouse ray.
    void ResetEditorUnfocusedInputState();                                       // Clears transient editor gestures when app focus is lost.
    void ClearEditorManipulationState();                                         // Clears placement/gizmo gesture state while preserving editor mode.
    void ToggleEditorPlacementMode( RuntimeInputActionSource source );           // Enters/exits placement mode from keyboard or UI.
    void HandleEditorKeyboardShortcuts();                                        // Applies editor-mode Alt/Tab shortcuts.
    void ApplyEditorUICommands( const SkullbonezCore::UI::InGameUICommands& uiCommands,
                                bool keyboardToggleEditorMode );                 // Applies Editor-tab UI and keyboard mode toggles.
    void TickEditorViewportAndPlacementScaleInput(
        int unhandledWheelDelta );                                               // Updates viewport-look and placement scale/altitude gestures.
    bool TickEditorWorldClick(
        const RuntimeMouseEdges& mouseEdges,
        bool suppressWorldActionThisFrame );                                     // Handles editor placement, selection, and gizmo mouse ownership.
    void HandleEditorSaveHotkeys();                                              // Handles F2 scene snapshots and F3 screenshot commands.
    void UpdateEditorInteractionPreview();                                       // Refreshes ghost and gizmo hover state before world-click handling
    bool TryPickEditorModel( const Math::Vector::Vector3& rayOrigin,
                             const Math::Vector::Vector3& rayDirection,
                             int& outIndex ) const;                              // Ray-picks editable objects
    bool TryPickMousePickupModel( const Math::Vector::Vector3& rayOrigin,
                                  const Math::Vector::Vector3& rayDirection,
                                  int& outIndex,
                                  float& outRayT ) const;                        // Ray-picks movable manipulator objects.
    void CancelMousePickup();                                                    // Releases manipulator drag/capture state.
    bool TickMousePickupInput(
        HWND hwnd,
        const RuntimeMouseEdges& mouseEdges,
        bool suppressWorldActionThisFrame );                                     // Handles manipulator left-click pickup and target updates.
    int HitEditorGizmoAxis( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection )
        const;                                                                   // Hovered gizmo axis, or -1 when none is hit.
    int HitEditorRotationGizmoAxis( const Math::Vector::Vector3& rayOrigin, const Math::Vector::Vector3& rayDirection )
        const;                                                                   // Hovered rotation ring axis, or -1 when none is hit.
    bool TryEditorAxisRayParameter( int axis,
                                    const Math::Vector::Vector3& rayOrigin,
                                    const Math::Vector::Vector3& rayDirection,
                                    float& outAxisT ) const;                     // Projects mouse ray onto a gizmo axis
    bool TryEditorRotationRayAngle( int axis,
                                    const Math::Vector::Vector3& rayOrigin,
                                    const Math::Vector::Vector3& rayDirection,
                                    float& outAngle ) const;                     // Projects mouse ray onto a rotation ring plane
    void MoveSelectedEditorObjectAlongAxis(
        const Math::Vector::Vector3& rayOrigin,
        const Math::Vector::Vector3& rayDirection );                             // Active gizmo drag along a selected axis.
    void RotateSelectedEditorObjectAroundAxis(
        const Math::Vector::Vector3& rayOrigin,
        const Math::Vector::Vector3& rayDirection );                             // Active rotation-ring drag around a selected axis.
    void ScaleSelectedEditorObjectAlongAxis(
        const Math::Vector::Vector3& rayOrigin,
        const Math::Vector::Vector3& rayDirection );                             // Active scale-axis drag along a selected axis.
    void RenderEditorOverlay(
        const Math::Transformation::Matrix4& viewProjection,
        const Math::Vector::Vector3& cameraEye,
        const Math::Vector::Vector3& cameraUp );                                 // Placement ghost, launcher laser, and object gizmo overlays.
    void PlaceEditorObjectAtMouse( int objectType,
                                   bool fixedObject );                           // Place a UI-selected object on the terrain under the mouse
    bool PlaceEditorObjectAtTerrainPoint(
        int objectType,
        bool fixedObject,
        const Math::Vector::Vector3& terrainPoint,
        bool recordReplayEvent = true );                                         // Places an object at an already-resolved terrain hit
#ifdef _DEBUG
    void LogSceneFinished( const char* reason );
    bool PickLauncherReproTarget( int& outIndex, float& outRayT, float& outCrosshairDistance );
    void WriteLauncherReproSnapshot();
    void BeginPhysicsDiagnosticsRun( const char* scenePath );
    void TickReplayScrubProbe();
    void TickReplayRestoreProbe();
    void TickReplaySaveProbe();
    void EndPhysicsDiagnosticsRun( const char* status );
#endif

  public:
    Run( std::vector<std::string> sceneQueue );                                  // sceneQueue empty string selects generated demo mode.
    ~Run();
    void Initialise();                                                           // Initialises shared resources and loads first scene
    void RunSceneLoadOnly( const char* snapshotOutPath = nullptr );              // Scene-load smoke path; skips the frame loop.
    void Execute();                                                              // Main message loop; sceneQueue decides generated demo versus suite playback.
    void SetTimeScaleOverride( float scale );                                    // Override timeScale for every scene loaded (CLI --time-scale)
    void SetFixedStepOverride();                                                 // Force fixed-step for every scene loaded (CLI --fixed-step)
    void SetSeedOverride( unsigned int seed );                                   // Override RNG seed for every scene loaded (CLI --seed)
    void SetNoWaterOverride();                                                   // Start scenes with fluid below terrain (CLI --no-water)
    void SetNoSleepOverride();                                                   // Disable physics sleeping for every scene loaded (CLI --no-sleep)
    void SetTornadoOverride( bool enabled );                                     // Enable/disable tornado mode for loaded scenes (CLI --tornado)
    void SetTornadoVectorFieldOverride( bool enabled );                          // Show/hide tornado velocity vectors at startup
    void SetCinematicRenderingOverride( bool enabled );                          // Force cinematic HDR/post rendering on/off for every scene loaded
    void SetCinematicShadowsOverride( bool enabled );                            // Force shadow maps on/off for every scene loaded
    void SetDemoHeroStyleOverride();                                             // Run generated demo mode with the low-poly hero rendering style
    void SetInteractiveRunOverride();                                            // Keep scene automation from quitting the app (CLI --interactive/--hold)
    void SetLiveStyleControlDirectory( const char* path );                       // Enable live style/capture harness in a control folder
    void SetFrameCountOverride( int frames );                                    // Stop scene/demo automation after N frames (CLI --frames)
    void SetUIStressOverride( unsigned int seed, int actionsPerFrame );          // Enable deterministic UI stress from CLI
    void SetReplayRecording( bool enabled,
                             int retentionSeconds,
                             const char* hashLogPath );                          // Enable bounded replay capture from CLI.
    bool LoadReplayPresentationArtifact( const char* path,
                                         bool activateScrubber );                // Load a v2 presentation artifact as a scrub source.
    void SetInitialOverlayMode( OverlayMode mode );
    void SetTopTextHidden( bool hidden );
    void SetBroadphaseVisualizerEnabled( bool enabled );
    void SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride );
    void SetPhysicsDebugFlagsOverride( uint32_t flags );
    void SetPhysicsDebugTransparentOverride( bool transparent );
    void SetPhysicsDebugAlphaOverride( float alpha );
    void SetPhysicsDebugContactLingerOverride( float seconds );
    void DumpTextureAssets( FILE* out ) const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogOverride( const char* path );                    // Override regression CSV path for all scenes
    void SetPhysicsCollisionTimeLogOverride( const char* path );                 // Override swept collision-time CSV path for all scenes
    void SetPhysicsDiagnosticsPath(
        const char* path,
        bool fixedStepForcedByDiagnostics );                                     // Enable queryable physics diagnostics (CLI --physics-diag)
    void SetReplayScrubProbe( float normalized );                                // Enable CLI-only replay scrub SkullScope probe.
    void SetReplayRestoreProbe( float normalized );                              // Enable CLI-only replay restore hash probe.
    void SetReplaySaveProbe( const char* path );                                 // Enable CLI-only v2 replay save probe.
    void VerifyLoadedReplayPresentationProbe( float normalized );                // Validate runtime scrubbing from a loaded v2 file.
    void VerifyReplaySolverCheckpointFileProbe(
        const char* path );                                                      // Validate hash-gated restore from a v2 solver checkpoint.
    void VerifyReplaySolverTargetFileProbe(
        const char* path );                                                      // Validate checkpoint-plus-event replay to a saved non-checkpoint target.
    void VerifyReplaySolverBranchFileProbe(
        const char* path );                                                      // Validate checkpoint-plus-event replay can become a live branch.
    void VerifyReplaySolverFailureFileProbe(
        const char* path );                                                      // Validate saved-file restore failures emit SkullScope diagnostics.
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
