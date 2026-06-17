/*
File: SkullbonezSource/SkullbonezRun.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  Render pass: A named slice of DrawPrimitives() with explicit inputs,
  outputs, and resource ownership.
  Render target: Texture the renderer draws into before another pass samples or
  presents it.
  DX11/OpenGL: Retired runtime renderers. Their source backends have been
  removed; old command-line values now fail early.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  HDR (High Dynamic Range): Floating-point scene color that can hold values
  brighter than display white until tonemapping resolves it.
  Shadow frame: Per-frame light-space matrices, depth texture handle, and
  filtering constants consumed by shadow receiver shaders.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.

Invariants:
  - RunRenderPassResources owns backend/device resources and must be reset
    while the renderer backend is still alive.
  - Pass input structs borrow frame data only for the current DrawPrimitives()
    call; no pass may store those references after it returns.
  - Shadow receiver pointers are valid only until the next shadow reset or the
    next frame rebuilds ShadowPassResources.

Related:
  - SkullbonezSource/SkullbonezRun.cpp
  - SkullbonezSource/SkullbonezRunRender.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <array>
#include <memory>
#include <string>
#include <vector>
#include "SkullbonezCommon.h"
#include "SkullbonezAssetSystem.h"
#include "SkullbonezCameraCollection.h"
#include "SkullbonezTimer.h"
#include "SkullbonezInput.h"
#include "SkullbonezRuntimeDiagnostics.h"
#include "SkullbonezSceneRuntime.h"
#include "SkullbonezSimulationSystem.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezWindow.h"
#include "SkullbonezText.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezSkyBox.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezIFramebuffer.h"
#include "SkullbonezIShader.h"
#include "SkullbonezShadow.h"
#include "SkullbonezTestScene.h"
#include "SkullbonezBroadphaseVisualizer.h"
#include "SkullbonezCollisionVisualizer.h"
#include "SkullbonezPhysicsDebugVisualizer.h"
#include "UI/SkullbonezUI.h"


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
struct SceneRuntimeResetSnapshot;
}

struct RunRuntimeSettings
{
    bool isVsyncEnabled = true;               // Swap-chain sync interval (true = vsync)
    bool isPipelineSyncEnabled = false;       // Force CPU/GPU sync via Finish() before render
    bool isPhysicsSleepEnabled = true;        // Live Catto sleep policy; false keeps bodies awake while leaving collision/solving active
    Physics::TornadoFieldConfig tornadoField; // Live vortex force/debug vector field controlled by CLI/UI
};

struct RunTimerState
{
    Environment::Timer frameTimer;
    Environment::Timer workTimer;
    Environment::Timer updateTimer;
    Environment::Timer cameraTimer;
    Environment::Timer simulationTimer;

    float physicsTime = 0.0f;        // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f; // Smoothed physics time accumulator
    float renderTime = 0.0f;         // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;  // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;     // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f; // Half-second averaged kinetic energy
    float cpuFrameWorkMs = 0.0f;     // Last frame CPU work before Present/VSync
    float gpuFrameWorkMs = 0.0f;     // Last available GPU work before Present/VSync
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUIDrawCalls = 0; // Actual UI draw calls measured around Frame/UI last frame
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
    SkullbonezWindow* window = nullptr;
    Geometry::SkyBox* skyBox = nullptr;
};

struct RunCameraState
{
    Hardware::InputState input = {}; // Current frame input state

    int selectedCamera = 0;          // Keeps track of which camera is selected
    bool isFlyMode = false;          // Free-fly camera mode active (toggle with F)
    bool isNudgeMode = false;        // Nudge mode: free camera + live simulation (toggle with N)
    bool needsMouseLookReset = true; // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    float cameraTime = 0.0f;         // Camera helper clock
    int trackBallIndex = -1;         // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;      // Camera height above tracked ball
    float autoCycleInterval = -1.0f; // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;     // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;     // Number of per-ball screenshots taken so far
};

struct RunScreenshotState
{
    bool isScreenshotSaved = false;   // Screenshot already written this run
    bool isScreenshotAndExit = false; // Capture frame 1 as SCENENAME.bmp then exit
    int screenshotFrame = -1;         // Save screenshot at this frame (-1 = unused)
    int screenshotMs = -1;            // Save screenshot at this elapsed ms (-1 = unused)
    char screenshotPath[256] = {};    // Output path for screenshot (empty = none)
    int screenshotInterval = -1;      // Save screenshot every N frames (-1 = disabled)
    int intervalCaptureCount = 0;     // Sequential counter for interval captures
    char screenshotDir[256] = {};     // Output directory for interval captures
};

struct RunLiveStyleControlState
{
    bool enabled = false;                 // Polls a small control folder for live .style and screenshot requests
    char directory[260] = {};             // Folder containing live.style, capture.txt, and status.txt
    char stylePath[300] = {};             // Style descriptor applied without reloading the scene
    char capturePath[300] = {};           // Text command file used to request one screenshot
    char statusPath[300] = {};            // Latest harness status for scripts/humans
    char pendingScreenshotPath[512] = {}; // Screenshot path requested by capture.txt
    uint64_t styleStamp = 0;              // Last applied live.style write stamp
    uint64_t captureStamp = 0;            // Last consumed capture.txt write stamp
    int styleApplyCount = 0;              // Successful live style applications
    int captureCount = 0;                 // Successful live screenshots
    bool hasPendingScreenshot = false;    // Capture should run after render/UI this frame
};

enum class OverlayMode
{
    None,           // Clean screen — nothing shown
    Timers,         // Renderer name, model count, physics solver, profiler overlay
    SceneStats,     // Scene telemetry values used by deterministic tests
    BarsNormalized, // Visual profiler bars — segments fill the bar width (relative)
    BarsAbsolute,   // Visual profiler bars — white = idle/vsync (absolute frame budget)
    Keys,           // Keyboard reference panel
};

struct RunDebugState
{
    OverlayMode overlayMode = OverlayMode::None;              // HUD overlay cycle state (0 key advances through timers, scene stats, bars, and keys)
    bool isWaterFreezeDebug = false;                          // Freeze ocean animation at current shape (toggle with 1)
    bool isWaterNoReflect = false;                            // Disable ocean reflection entirely (2 cycles: FBO→DXR→none)
    bool isWaterRTReflect = false;                            // Use DXR ray-traced reflection (2 cycles: FBO→DXR→none; DXR only if supported)
    bool isWaterFlatDebug = false;                            // Force ocean mesh fully flat, no displacement (toggle with 3)
    bool isTerrainHidden = false;                             // Hide terrain mesh (toggle with 4)
    bool isWaterHidden = false;                               // Hide water mesh (toggle with 5)
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE; // Draw object axes, contact manifolds, and sleep state (cycle with C)
    bool isPhysicsDebugTransparent = false;                   // Draw translucent debug collision volumes behind physics debug lines (toggle with 6)
    float physicsDebugAlpha = 0.28f;                          // Translucent debug volume alpha
    float physicsDebugContactLinger = 0.45f;                  // Seconds to keep contact manifolds visible after their solver row disappears
    int physicsDebugPipelineStageCursor = 0;                  // F7/F8-selected Catto pipeline stage for PHYSICS_DEBUG_PIPELINE
    bool isCollisionVisualizer = false;                       // Render solid collision/sleep colours for balls and boxes (toggle with V)
    bool isTextOnly = false;                                  // Suppress all 3D rendering; show solid background with large pangram text
    bool isUITestPattern = false;                             // Bright 2D backdrop behind UI for visual blur tests
    bool isTopTextHidden = false;                             // Hide top-left HUD text while leaving other overlays active
    bool isBroadphaseOverlay = false;                         // Broadphase spatial grid visualizer overlay (toggle with G)
    float frozenWaterTime = 0.0f;                             // Simulation time captured when freeze was toggled on
#ifdef _DEBUG
    char reproSnapshotMessage[128] = {};    // Short HUD confirmation after nudge-mode repro dump
    double reproSnapshotMessageUntil = 0.0; // Simulation timer value after which the HUD message expires
#endif
};

static constexpr int RUNTIME_PROJECTILE_POOL_SIZE = 10;

struct RunFireState
{
    // Runtime-created bullet model indices, recycled in a simple ring.
    RunFireState()
    {
        bulletIndices.fill( -1 );
    }

    std::array<int, RUNTIME_PROJECTILE_POOL_SIZE> bulletIndices = {};
    int bulletNext = 0;
    bool bulletPoolReady = false;
};

struct RunUIStressState
{
    bool enabled = false;                   // Deterministic scene-driven UI stress runner
    unsigned int randomState = 0x7F4A7C15u; // LCG state, seeded from scene UI options
    int actionsPerFrame = 4;                // Cheap UI state mutations per rendered frame
    int framesRun = 0;                      // Stress-run frame counter independent of scene resets
};

enum class GeneratedObjectTypeOverride
{
    Mixed,
    AllBalls,
    AllBoxes
};

/* -- Skullbonez Run ---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezRun
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
        CubemapOnly,       // Force the authored cube-map skybox path.
        CinematicIfEnabled // Allow the procedural cinematic sky when the active config requests it.
    };

    enum class ObjectPassMode
    {
        Opaque,     // Normal body draw before water.
        Transparent // Debug alpha body draw after water so overlays remain readable.
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
        float waterY = 0.0f; // World-space fluid surface height used by reflection clipping and water shading.

        // Non-null only when cinematic rendering wraps this frame. Passes use
        // the pointer as an opt-in contract, not as ownership.
        bool cinematicEnabled = false;
        const CinematicRenderConfig* cinematic = nullptr;
    };

    struct ObjectPassInputs
    {
        // Draws the body collection into the current render target. It can act
        // as the opaque pass or the transparent debug pass, but it must not own
        // target binding; the caller decides where the pass renders.
        const RenderFrameContext& frame;
        // Documents where this object pass sits in the frame ordering.
        ObjectPassMode mode;
        const CinematicRenderConfig* cinematic;
        const Rendering::ShadowFrameData* shadow;
        bool collisionStateColorsVisible;       // Route bodies through collision-state visualization instead of materials.
        float collisionVisualizerAlphaOverride; // -1 keeps visualizer defaults; otherwise overrides debug alpha.
        float bodyAlpha;                        // 1 for opaque bodies; debug alpha for the transparent object pass.
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
        bool collisionStateColorsVisible; // Reflection must match the selected body visualization mode.
        // Disables DXR reflection because the mirrored raster path can honor
        // debug alpha and collision-state rendering.
        bool transparentBodyPass;
        float collisionVisualizerAlphaOverride; // Forwarded to reflected collision-state geometry.
        float bodyAlpha;                        // Forwarded to reflected production body rendering.
    };

    struct ReflectionPassOutput
    {
        uint32_t reflectionTextureHandle = 0; // Engine texture handle consumed by WorldEnvironment::RenderFluid.
        // Matrix used by water to project the current surface pixel into the
        // reflection texture returned by this pass.
        Math::Transformation::Matrix4 reflectionSampleViewProjection;
        bool usedDxr = false; // True when the texture came from the DXR dispatch instead of the planar target.
    };

    struct WaterPassInputs
    {
        // Water is deliberately downstream of reflection. It must not rebuild
        // reflection itself; it only receives the texture/sample transform that
        // the reflection pass produced for this frame.
        const RenderFrameContext& frame;
        const ReflectionPassOutput& reflection;
        const CinematicRenderConfig* cinematic;
        bool waterHidden;  // Caller-controlled debug visibility; reflection resources stay outside this flag.
        bool flatWater;    // Debug water style: flat shading instead of animated waves.
        bool noReflection; // Debug override: keep water visible but force the no-reflection shader path.
        bool freezeTime;   // Debug override: hold wave animation at frozenTime.
        float frozenTime;  // Simulation time captured when water animation was frozen.
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

    /* -- FullscreenQuadPass ------------------------------------------------------------------------------------------------------------------------------------

        Shared two-triangle draw surface for generated sky, volumetric light,
        and tonemap. It owns only the dynamic vertex buffer; shader meaning is
        owned by the pass that uses it.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class FullscreenQuadPass
    {
      public:
        explicit FullscreenQuadPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        uint32_t QuadVB() const;

      private:
        SkullbonezRun& m_run;
    };

    /* -- SkyPass -----------------------------------------------------------------------------------------------------------------------------------------------

        Draws the current sky into whichever render target the caller has bound.
        The cube-map path samples authored face textures; the cinematic path
        owns a generated-atmosphere shader and uses FullscreenQuadPass.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class SkyPass
    {
      public:
        explicit SkyPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view, SkyPassMode mode );

      private:
        void RenderCinematicSky( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view );

        SkullbonezRun& m_run;
    };

    /* -- SceneTargetPass ---------------------------------------------------------------------------------------------------------------------------------------

        Owns the HDR scene target used by cinematic rendering. Begin() binds and
        clears the target, then asks SkyPass to draw the background before world
        geometry is rendered into the target.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class SceneTargetPass
    {
      public:
        explicit SceneTargetPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        bool IsReady() const;
        void Begin( const RenderFrameContext& frame, SkyPass& skyPass );

      private:
        SkullbonezRun& m_run;
    };

    /* -- ShadowPass --------------------------------------------------------------------------------------------------------------------------------------------

        Builds terrain/object shadow maps before receiver passes run. It owns
        the shadow targets and the per-frame receiver payloads that terrain and
        object shaders borrow for the rest of DrawPrimitives().
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class ShadowPass
    {
      public:
        explicit ShadowPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame, const CinematicRenderConfig& cinematic );
        void ReleaseGpuResources();
        ShadowPassOutput Render( const ShadowPassInputs& inputs );

      private:
        Rendering::ShadowFrameData BuildTerrainFrameData( const CinematicRenderConfig& cinematic, const Math::Vector::Vector3& lightDirectionWorld ) const;
        Rendering::ShadowFrameData BuildObjectFrameData( const CinematicRenderConfig& cinematic, const Math::Vector::Vector3& lightDirectionWorld, const Math::Vector::Vector3& focusHint );
        void RenderShadowMap( Rendering::IFramebuffer& target, const Rendering::ShadowFrameData& shadowFrame, const CinematicRenderConfig& cinematic, bool renderTerrain, bool renderObjects, const Rendering::ShadowCasterBatches* objectCasters );

        SkullbonezRun& m_run;
    };

    /* -- ReflectionPass ----------------------------------------------------------------------------------------------------------------------------------------

        Produces the reflection texture consumed by WaterPass. It chooses DXR
        reflection when possible, otherwise it renders a mirrored scene into the
        planar reflection target.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class ReflectionPass
    {
      public:
        explicit ReflectionPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        ReflectionPassOutput Render( const ReflectionPassInputs& inputs, SkyPass& skyPass );

      private:
        SkullbonezRun& m_run;
    };

    /* -- ObjectPass --------------------------------------------------------------------------------------------------------------------------------------------

        Draws production bodies or collision-state solids into the current
        target. The caller chooses whether this is the opaque or transparent
        body pass; this class owns the object shader texture-slot contract.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class ObjectPass
    {
      public:
        explicit ObjectPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const ObjectPassInputs& inputs );

      private:
        SkullbonezRun& m_run;
    };

    /* -- TerrainPass -------------------------------------------------------------------------------------------------------------------------------------------

        Draws the terrain mesh with its material texture, cinematic style
        uniforms, and optional shadow receiver payload.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class TerrainPass
    {
      public:
        explicit TerrainPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const TerrainPassInputs& inputs );

      private:
        SkullbonezRun& m_run;
    };

    /* -- WaterPass ---------------------------------------------------------------------------------------------------------------------------------------------

        Draws calm/ocean water after reflection has produced its texture. Water
        samples only the reflection slot and never rebuilds reflection itself.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class WaterPass
    {
      public:
        explicit WaterPass( SkullbonezRun& run )
            : m_run( run )
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
        SkullbonezRun& m_run;
        WaterPassDebugInfo m_debugInfo;
    };

    /* -- DebugOverlayPass --------------------------------------------------------------------------------------------------------------------------------------

        Draws non-production world overlays after the main scene. These overlays
        are intentionally separate from ObjectPass so debug visuals do not leak
        into material or shadow contracts.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class DebugOverlayPass
    {
      public:
        explicit DebugOverlayPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const DebugOverlayPassInputs& inputs );

      private:
        SkullbonezRun& m_run;
    };

    /* -- VolumetricPass ----------------------------------------------------------------------------------------------------------------------------------------

        Reads the completed HDR scene color/depth target and writes a
        half-resolution light-shaft texture for TonemapPass to composite.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class VolumetricPass
    {
      public:
        explicit VolumetricPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        bool Render( const RenderFrameContext& frame );

      private:
        SkullbonezRun& m_run;
    };

    /* -- TonemapPass -------------------------------------------------------------------------------------------------------------------------------------------

        Resolves the HDR scene target back to the window backbuffer. It owns the
        final post shader contract: scene color, scene depth, optional
        volumetric light, and cinematic grading uniforms.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class TonemapPass
    {
      public:
        explicit TonemapPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources( const RenderFrameContext& frame );
        void ReleaseGpuResources();
        void Render( const RenderFrameContext& frame, bool sceneAlreadyUnbound, bool volumetricReady );

      private:
        SkullbonezRun& m_run;
    };

    /* -- UiTextPass --------------------------------------------------------------------------------------------------------------------------------------------

        Named 2D pass for the existing HUD, in-game UI, and SDF text renderer.
        It leaves UI layout code in the UI subsystem but gives the frame loop a
        pass-level Render/Release contract like the 3D passes.
    -------------------------------------------------------------------------------------------------------------------------------------------------------------*/
    class UiTextPass
    {
      public:
        explicit UiTextPass( SkullbonezRun& run )
            : m_run( run )
        {
        }

        void EnsureGpuResources();
        void ReleaseGpuResources();
        bool ShouldRender() const;
        void Render( double secondsPerFrame );

      private:
        SkullbonezRun& m_run;
    };

    SceneRuntime m_sceneRuntime; // Owns scene queue and current scene-run state
    std::vector<std::string> m_sceneBrowserPaths;
    std::vector<std::string> m_sceneBrowserNames;
    std::vector<const char*> m_sceneBrowserNamePtrs;
    bool m_leftSceneCycleWasDown = false;
    bool m_rightSceneCycleWasDown = false;
    double m_lastEscapeTapTime = -1000.0;
    float m_cmdTimeScaleOverride = 0.0f; // CLI --time-scale override applied after each scene load (0 = not set)
    bool m_cmdFixedStep = false;         // CLI --fixed-step override applied after each scene load
    unsigned int m_cmdSeedOverride = 0;  // CLI --seed override applied after each scene load (0 = not set)
    bool m_cmdNoWater = false;           // CLI --no-water starts fluid below terrain
    bool m_cmdNoSleep = false;           // Startup CLI --no-sleep request; the live policy can still be toggled from the Physics tab
    bool m_cmdHasTornadoOverride = false;
    bool m_cmdTornadoEnabled = false;
    bool m_cmdTornadoVectors = false;
    bool m_cmdHasCinematicRenderingOverride = false;
    bool m_cmdCinematicRendering = false;
    bool m_cmdHasCinematicShadowsOverride = false;
    bool m_cmdCinematicShadows = false;
    bool m_cmdDemoHeroStyle = false;                // CLI --demohero applies the low-poly hero look to generated demo mode
    bool m_cmdInteractiveSceneRun = false;          // CLI --interactive/--hold keeps scene automation from quitting the app
    int m_cmdFrameCountOverride = -1;               // CLI --frames override applied after each scene load
    bool m_cmdUIStress = false;                     // CLI --ui-stress enables generated/demo stress without a scene file
    unsigned int m_cmdUIStressSeed = 0;             // CLI --ui-stress-seed
    int m_cmdUIStressActions = 5;                   // CLI --ui-stress-actions
    int m_selectedCineModeSceneIndex = -1;          // -1=Demo/default look, otherwise scene-browser index of live cine/concept look
    CinematicRenderConfig m_defaultCinematicRender; // engine.cfg cinematic baseline restored by the Demo Scene cine mode
    int m_startupGameModelCapacity = DEFAULT_GAME_MODEL_CAPACITY;
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

    RunPerfLogState m_perfLogState; // Perf/test logging paths, files, and flush policy
#ifdef _DEBUG
    RunPhysicsDiagnosticsState m_physicsDiagnostics; // Queryable model-facing physics diagnostic trace
#endif
    RunRuntimeSettings m_runtimeSettings;                     // Scene/app runtime swap policy toggles
    RunTimerState m_timers;                                   // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                              // Window, camera, texture, terrain, and pass resource ownership
    RunCameraState m_camera;                                  // Camera/input state and ball-tracking settings
    SimulationSystem m_simulation;                            // Simulation timestep policy and physics accumulators
    RunScreenshotState m_screenshot;                          // Screenshot trigger and capture state
    RunLiveStyleControlState m_liveStyle;                     // Live style tweak/capture harness state
    UI::InGameUI m_UI;                                        // Encapsulated in-game diagnostics window
    RunDebugState m_debug;                                    // Runtime debug/overlay toggles
    RunFireState m_fire;                                      // Runtime silver bullet pool state
    RunUIStressState m_uiStress;                              // Deterministic UI stress run state
    Physics::BroadphaseVisualizer m_broadphaseVisualizer;     // Spatial grid debug overlay (G key toggle)
    Physics::CollisionVisualizer m_collisionVisualizer;       // Solid collision/sleep model visualizer (V key toggle)
    Physics::PhysicsDebugVisualizer m_physicsDebugVisualizer; // Line overlay for object axes, contact manifolds, and sleep state
    Environment::WorldEnvironment m_cWorldEnvironment;        // SkullbonezCore::Environment::WorldEnvironment class
    GameObjects::GameModelCollection m_cGameModelCollection;  // SkullbonezCore::GameObjects::GameModelCollection class
    std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms = {};
    FullscreenQuadPass m_fullscreenQuadPass; // Shared full-screen vertex buffer pass used by sky/post effects
    SkyPass m_skyPass;                       // Background sky pass, reused by reflection and scene target passes
    SceneTargetPass m_sceneTargetPass;       // Cinematic HDR scene-target begin/release pass
    ShadowPass m_shadowPass;                 // Terrain/object shadow-map producer pass
    ReflectionPass m_reflectionPass;         // Water reflection texture producer pass
    ObjectPass m_objectPass;                 // Production body and collision-solid pass
    TerrainPass m_terrainPass;               // Terrain material/shadow receiver pass
    WaterPass m_waterPass;                   // Calm/ocean water pass
    DebugOverlayPass m_debugOverlayPass;     // Broadphase and physics debug overlay pass
    VolumetricPass m_volumetricPass;         // Half-resolution cinematic light-shaft pass
    TonemapPass m_tonemapPass;               // HDR-to-backbuffer resolve pass
    UiTextPass m_uiTextPass;                 // HUD/UI/text pass

    inline static int sPerfPass = 0;
    void Render();                                                                                                                     // Main render method
    RunSceneState& SceneState();                                                                                                       // Mutable scene-run state owned by SceneRuntime
    const RunSceneState& SceneState() const;                                                                                           // Read-only scene-run state owned by SceneRuntime
    void RelativeUpdateCamera( uint32_t hash );                                                                                        // Relative update specified camera
    void UpdateLogic( float simulationDt, float cameraDt );                                                                            // Per-frame logic; cameraDt is unscaled wall time
    void TakeInput();                                                                                                                  // Take user input
    void StepPhysicsPipelineStage( int direction );                                                                                    // Move the debug pipeline visualization cursor left/right
    void SetUpCameras();                                                                                                               // Camera init for generated demo mode
    void SetUpCamerasFromScene( const TestScene& scene );                                                                              // Camera init from scene file
    void SetUpGameModels( int count );                                                                                                 // Game model init for generated mixed-object mode
    void SetUpSolverObjects( int balls, int boxes );                                                                                   // Game model init: exact N solver balls + M solver boxes
    void SetUpGameModelsFromScene( const TestScene& scene );                                                                           // Game model init from scene file
    void RegisterBuiltInAssets();                                                                                                      // Registers built-in texture and shader source records
    std::string ResolveSourceAssetPath( Assets::AssetKind kind, const char* logicalName, const std::string& relativePath );            // Registers and resolves a source asset under DATA_ROOT
    void DrawPrimitives();                                                                                                             // Draws terrain, objects, helpers, and scene effects
    RenderFrameContext BuildRenderFrameContext( bool cinematicRender, const CinematicRenderConfig& renderConfig );                     // Names per-frame camera/light inputs consumed by render passes
    CinematicRenderConfig& ActiveCinematicConfig();                                                                                    // Mutable cinematic style config for the active scene/run
    const CinematicRenderConfig& ActiveCinematicConfig() const;                                                                        // Read-only cinematic style config for the active scene/run
    bool IsCinematicRenderingEnabled() const;                                                                                          // True when the HDR/post stack should wrap the main scene
    void ReleaseBackendOwnedRenderResources( const char* phaseName );                                                                  // Runs the ordered GPU-resource release hooks while the backend is alive
    void RebuildRegisteredRenderResources();                                                                                           // Recreates renderer resources from source asset records
    void LogRenderResourceLifecycleStep( const char* phase, const char* step ) const;                                                  // Writes a named resource-lifetime phase to the debug event log
    Textures::TextureCollection& Textures();                                                                                           // Runtime texture registry accessor used by render passes
    uint32_t TextureHandle( uint32_t textureHash );                                                                                    // Resolves a runtime texture hash to a renderer handle
    void SelectRenderTexture( uint32_t textureHash );                                                                                  // Binds a runtime texture hash to the default draw texture slot
    int WindowScreenWidth() const;                                                                                                     // Current window width, or config fallback before window init
    int WindowScreenHeight() const;                                                                                                    // Current window height, or config fallback before window init
    void SetViewingOrientation();                                                                                                      // Renders camera views etc
    void SaveScreenshot( const char* path );                                                                                           // Saves current backbuffer to a BMP file
    bool SaveCurrentSceneDefaults();                                                                                                   // Writes UI-controlled defaults back to the active scene file
    bool SaveRenderDefaults();                                                                                                         // Writes current ordinary Render-tab values back to engine.cfg
    void RefreshSceneBrowserList();                                                                                                    // Discovers scene files available to the in-game scene dropdown
    int CurrentSceneBrowserIndex() const;                                                                                              // Returns current scene index within the discovered scene dropdown list
    void LoadSceneFromBrowserIndex( int index );                                                                                       // Loads a scene selected from the in-game scene dropdown
    void LoadDemoSceneFromUI();                                                                                                        // Loads the generated demo scene from the in-game Scene tab
    bool ApplyCinematicModeFromBrowserIndex( int index );                                                                              // Applies a cine/concept look live without rebuilding the scene
    bool ApplyAdjacentCinematicMode( int direction );                                                                                  // Cycles live cine/concept looks without rebuilding the scene
    void ApplyLiveStyleScene( const TestScene& styleScene );                                                                           // Applies style-only cinematic/material directives without rebuilding objects
    void ApplyDemoHeroStyleOverride();                                                                                                 // Applies the low-poly hero style to generated demo mode
    void LoadAdjacentSceneFromBrowser( int direction );                                                                                // Keyboard scene cycling through the discovered scene dropdown list
    void EnterInteractiveSceneRun();                                                                                                   // Locks scene automation into non-quitting interactive mode
    bool CanSceneAutomationQuit() const;                                                                                               // True for CLI suites/tests; false once the user owns scene flow
    void HoldCompletedInteractiveScene();                                                                                              // Keep the current scene alive after interactive automation completes
    bool HasSceneQueueEntry( int index ) const;                                                                                        // True when index points at a queued scene/demo entry
    bool HasCurrentSceneQueueEntry() const;                                                                                            // True when currentSceneIndex points at a queued entry
    const std::string* CurrentSceneQueuePath() const;                                                                                  // Current queued scene path, or nullptr if no current entry
    RunInternal::SceneRuntimeResetSnapshot CaptureSceneRuntimeResetSnapshot();                                                         // Captures live runtime controls before a scene reset rebuilds objects
    void RestoreSceneRuntimeResetSnapshot( const RunInternal::SceneRuntimeResetSnapshot& snapshot, bool suppressExitOnComplete );      // Restores preserved live controls after scene file/defaults rebuild
    void ClearSceneRuntimeUIOverrides();                                                                                               // Clears UI rebuild overrides when a new scene/defaults should be authoritative
    void LogPerfMemory( const char* checkpoint );                                                                                      // Log memory usage to perf CSV
    void LoadScene( int index, bool preserveUIState = false, bool suppressExitOnComplete = false, bool preserveRuntimeState = false ); // Resets scene-specific state and loads a scene by queue index
    void ResetCurrentScene( bool preserveUIState = false, bool suppressExitOnComplete = false, bool preserveRuntimeState = true );     // User-triggered reset/reload of current scene or generated demo mode
    void ApplyUIModelCountOverride( int count );                                                                                       // Rebuilds the active generated model pool from the UI slider
    void ApplyUISolverObjectCounts( int balls, int boxes );                                                                            // Rebuilds generated solver objects from exact UI counts
    void ApplyUIWorldOverride( float gravity, float fluidHeight, float fluidDensity );                                                 // Applies live world/fluid scalar controls
    void ApplyNoWaterOverride();                                                                                                       // Pushes fluid surface below the active terrain when requested
    void ApplyTornadoDefaultsForActiveScene();                                                                                         // Centers the tornado around the active inner-water/basin region
    void SyncTornadoFieldToPhysics();                                                                                                  // Sends live tornado state to the physics collection
    void UseDefaultTerrain();                                                                                                          // Restores the normal height-map terrain when leaving analytic test scenes
    void UseFlatSlopeTerrain( float baseY, float slopeX, float slopeZ );                                                               // Activates analytic flat-slope terrain for focused physics scenes
    void UpdateWorldTerrainBounds();                                                                                                   // Keeps world/fluid helpers aligned with the active terrain bounds
    bool AdvanceScene();                                                                                                               // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty, float mouseMovemementQty );                                                                 // Moves the camera
    // Builds a tight light-space frame for nearby object receivers.
    // Renders requested depth casters from the sun view.
    unsigned int NextUIStressRandom();
    int NextUIStressInt( int maxExclusive );
    float NextUIStressFloat( float minValue, float maxValue );
    void RunUIStressActions();

    // --- Per-frame tick helpers (called from Run()) ---
    void TickPhysics( double dt );              // Physics dispatch: fixed-step and variable-step accumulator
    bool TickScreenshots();                     // Screenshot triggers; returns true when frame should restart (continue)
    void TickLiveStyleControl();                // Poll live.style/capture.txt and apply look changes without scene reload
    void TickLiveStyleControlCapture();         // Save pending harness screenshot after render/UI are drawn
    void TickAutoCycle();                       // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    void TickPerfLog();                         // Write per-frame perf CSV row and periodic memory checkpoint
    bool TickSceneAdvance();                    // Frame count, exit/hold on completion, restarts; returns true to continue
    void UpdateWaterHeightControls( float dt ); // Slide water surface up/down while held
    void ResetProjectilePool();                 // Clears cached projectile indices after scene/model rebuilds
    bool EnsureProjectilePool();                // Lazily creates the ten runtime silver bullets
    void FireProjectile();                      // Recycle and launch a high-speed silver bullet from the camera
#ifdef _DEBUG
    void LogSceneFinished( const char* reason );
    bool PickNudgeReproTarget( int& outIndex, float& outRayT, float& outCrosshairDistance );
    void WriteNudgeReproSnapshot();
    void BeginPhysicsDiagnosticsRun( const char* scenePath );
    void EndPhysicsDiagnosticsRun( const char* status );
#endif

  public:
    SkullbonezRun( std::vector<std::string> sceneQueue );               // Constructor (scene queue; empty string = generated demo scene)
    ~SkullbonezRun();                                                   // Default destructor
    void Initialise();                                                  // Initialises shared resources and loads first scene
    void RunSceneLoadOnly();                                            // Loads every queued scene once, then returns without entering the frame loop
    void Run();                                                         // Runs all scenes in sequence — main message loop
    void SetTimeScaleOverride( float scale );                           // Override timeScale for every scene loaded (CLI --time-scale)
    void SetFixedStepOverride();                                        // Force fixed-step for every scene loaded (CLI --fixed-step)
    void SetSeedOverride( unsigned int seed );                          // Override RNG seed for every scene loaded (CLI --seed)
    void SetNoWaterOverride();                                          // Start scenes with fluid below terrain (CLI --no-water)
    void SetNoSleepOverride();                                          // Disable physics sleeping for every scene loaded (CLI --no-sleep)
    void SetTornadoOverride( bool enabled );                            // Enable/disable tornado mode for loaded scenes (CLI --tornado)
    void SetTornadoVectorFieldOverride( bool enabled );                 // Show/hide tornado velocity vectors at startup
    void SetCinematicRenderingOverride( bool enabled );                 // Force cinematic HDR/post rendering on/off for every scene loaded
    void SetCinematicShadowsOverride( bool enabled );                   // Force shadow maps on/off for every scene loaded
    void SetDemoHeroStyleOverride();                                    // Run generated demo mode with the low-poly hero rendering style
    void SetInteractiveRunOverride();                                   // Keep scene automation from quitting the app (CLI --interactive/--hold)
    void SetLiveStyleControlDirectory( const char* path );              // Enable live .style/capture harness in a control folder
    void SetFrameCountOverride( int frames );                           // Stop scene/demo automation after N frames (CLI --frames)
    void SetUIStressOverride( unsigned int seed, int actionsPerFrame ); // Enable deterministic UI stress from CLI
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
    void SetPhysicsRegressionLogOverride( const char* path );                              // Override regression CSV path for all scenes
    void SetPhysicsCollisionTimeLogOverride( const char* path );                           // Override swept collision-time CSV path for all scenes
    void SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics ); // Enable queryable physics diagnostics (CLI --physics-diag)
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
