/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.h
Purpose:
  Declares the runtime renderer owner for ordered render passes.

Summary:
  RuntimeRenderer owns pass objects and frame ordering. RenderResourceLifecycle
  owns backend-epoch state behind one explicit surface. Established owner views
  supply lifetime-stable dependencies; immutable per-frame records carry
  submission facts and completed overlays.
  UiTextPass owns its font batch and presentation capabilities; this class
  contributes the late graph edge and supplies its backend-epoch owners.
  A content-neutral extension scope lets higher composition append one typed
  world pass at the renderer-owned post-water scheduling point.

Glossary:
  RuntimeRenderer: Owner of pass instances and the live frame-graph builder.
  Pass order: The stable sequence of shadows, sky, reflection, objects, terrain,
    water, post effects, and UI/text.
  Lane R result: Recoverable resource setup or GPU-drain failure reported
    through an owner/message result instead of throwing through the render owner.
  Resource context: Creation/rebuild-only view of the renderer factory and
  resize-sensitive dimensions.
  Backend-owned resource: GPU object that must be released before backend
    teardown.
  World extension: One synchronous callback-owned graphics pass registered by
    higher composition without exposing its content owner to this renderer.

Invariants:
  - RuntimeRenderer owns pass instances; Run owns one RuntimeRenderer.
  - One graph accumulates world, late UI, development UI, and Present rows in
    execution order; wrappers never clear or reconstruct it mid-frame.
  - Backend resource release begins only after a successful GPU drain, then
    keeps consumer passes ahead of producer passes.
  - The world-extension registration is consumed before its stack scope ends.
  - UI-text callers provide only operation-specific graph ABI records;
    RuntimeRenderer injects its current resource, texture, geometry, and
    diagnostics owners.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "RuntimeRenderHost.h"
#include "RuntimeRenderFrameValues.h"
#include "RuntimeRenderPasses.h"
#include "RenderResourceLifecycle.h"
#include "RenderPresentationSettings.h"
#include "../../Rendering/RenderGraph.h"
#include "../../Rendering/RenderDiagnosticsTypes.h"
#include "../../Rendering/RenderSceneSnapshot.h"
#include "../../Rendering/WorldRenderExtension.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
namespace Runtime::DevelopmentTools
{
class ImGuiEditorOwner;
}
#endif
namespace Runtime
{
struct RenderDiagnosticsReadout
{

    // Detached UI-facing diagnostics. The renderer name is copied into bounded
    // storage so no backend-owned character pointer escapes RuntimeRenderer.
    std::array<char, 64> rendererName = {};
    int drawCalls = 0;
    Rendering::RenderMemoryStats memory;
};

// Graph ABI: each record below is caller-owned until RenderUiText returns and
// contains only the direct values/capabilities consumed by one focused pass.
// Operation: draws text-only output or the ordered HUD chrome stream.
struct UiChromeGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const OverlayDebugState* debug = nullptr;
    bool crossScenePauseLocked = false;
    const SceneSessionState* scene = nullptr;
    const CameraControlState* camera = nullptr;
    int sceneQueueSize = 0;
    const char* cameraModeLabel = "";
    const char* launcherFireModeLabel = "";
    bool launcherCameraMode = false;
    const ReplayHudStatus* replayHud = nullptr;
    UiTextViewport viewport;
    double reproMessageAgeSeconds = 0.0;
    Rendering::Dx12TextureOwner* renderTextures = nullptr;
    Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

// Operation: copies bounded profiler, timing, memory, and Replay diagnostics.
struct UiOperatorDiagnosticsGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    UI::InGameUIFrameData* uiData = nullptr;
    const ReplayHudStatus* replayHud = nullptr;
    RunTimerState* timers = nullptr;
    const RuntimeRenderModelFrameView* models = nullptr;
    DiagnosticsRuntime* diagnosticsRuntime = nullptr;
    UI::InGameUI* ui = nullptr;
    Threading::WorkerPool* workerPool = nullptr;
    double secondsPerFrame = 0.0;
    Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

// Operation: projects immutable render, world, physics, and cinematic settings.
struct UiOperatorSettingsGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    UI::InGameUIFrameData* uiData = nullptr;
    const OverlayDebugState* debug = nullptr;
    const RenderPresentationSettings* renderPresentation = nullptr;
    const SceneWorld* world = nullptr;
    const SkullbonezCore::Core::EngineConfig* config = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    bool cinematicRendering = false;
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

// Operation: projects input mode, camera, ray-cast, and editor interaction state.
struct UiOperatorInteractionGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    UI::InGameUIFrameData* uiData = nullptr;
    const RunRayCastTestState* rayCastTest = nullptr;
    const RunEditorPlacementState* editor = nullptr;
    const RuntimeInputContext* runtimeInput = nullptr;
    const CameraControlState* camera = nullptr;
    const UI::InGameUI* ui = nullptr;
    uint32_t cameraModeEnabledMask = 0u;
    const char* cameraModeLabel = "";
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

// Operation: publishes scene navigation and the shared operator editor view.
struct UiOperatorPresentationGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    UI::InGameUIFrameData* uiData = nullptr;
    const SceneSessionState* scene = nullptr;
    const RuntimeViewModel* runtimeViewModel = nullptr;
    const UI::RunSceneBrowserState* sceneBrowser = nullptr;
    const UI::OperatorEditorFrameView* operatorEditorView = nullptr;
    bool sceneHasCurrentEntry = false;
    const char* currentScenePath = nullptr;
    int currentSceneBrowserIndex = 0;
    float sceneEnergyForDisplay = 0.0f;
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

// Operation: resolves preview resources, draws Legacy UI, and flushes text.
struct UiOperatorSubmissionGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    UI::InGameUIFrameData* uiData = nullptr;
    UI::InGameUI* ui = nullptr;
    const RuntimeRenderTargetPreviewSnapshot* renderTargetPreviews = nullptr;
    Assets::AssetSystem* assets = nullptr;
    Rendering::Dx12ResourceBuilder* renderResources = nullptr;
    Rendering::Dx12TextureOwner* renderTextures = nullptr;
    Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    int uiPassDrawCallStart = 0;
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

// Operation: draws the Replay scrubber from its immutable publication.
struct UiReplayGraphInvocation
{
    UiTextPass* pass = nullptr;
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const ReplayOverlay::ReplayOverlayStateView* overlay = nullptr;
    Core::Profiler* profiler = nullptr;
    bool legacySurfaceActive = true;
    bool scenePhysicsEnabled = false;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    UiTextViewport viewport;
    double nowSeconds = 0.0;
    Rendering::Dx12TextureOwner* renderTextures = nullptr;
    Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    const Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

class RuntimeRenderer
{
  public:
    struct BackendResourceReleaseContext
    {
        const char* phaseName = nullptr;
        UI::InGameUI& ui;
        RuntimeTools& tools;
    };

    struct FrameEntryContext
    {
        const RuntimeRenderModelFrameView& renderModels;
        RuntimeRenderFramePolicy framePolicy;
        const ReplayRenderFrameView& replayFrame;
        const RenderToolOverlayView& toolOverlay;
        const Rendering::WorldRenderExtensionRegistration& worldExtension;
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic;
        bool cinematicRequested = false;
    };

    RuntimeRenderer( Rendering::Dx12RenderDevice& renderDevice, Rendering::Dx12FrameOwner& renderFrame,
                     Rendering::Dx12GraphTransientPool& renderGraph, Rendering::Dx12ResourceBuilder& renderResources,
                     Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderGeometry,
                     Rendering::Dx12Diagnostics& renderDiagnostics,
                     std::optional<std::reference_wrapper<Rendering::Dx12RaytracingOwner>> raytracing,
                     const RenderWorldView& world, SceneSessionState& scene );
    ~RuntimeRenderer();

    // Runs after Core FrameBegin and before draw-call counters reset. This
    // reads completed GPU samples and publishes the preceding render counters.
    void BeginProfilerFrame();

    const RenderPresentationSettings& PresentationSettings() const
    {
        return m_presentationSettings;
    }

    // Replaces the complete renderer-owned presentation policy during an
    // explicit scene-reset transaction; ordinary callers use the named commands.
    void RestorePresentationSettings( const RenderPresentationSettings& settings );
    bool VsyncEnabled() const;
    void SetVsyncEnabled( bool enabled );
    bool PipelineSyncEnabled() const;
    void SetPipelineSyncEnabled( bool enabled );
    void ResetSceneRuntimePolicyFromConfig();

    void EnsureFrameResources( const RenderResourceContext& resources );
    bool RenderFrameEntry( const FrameEntryContext& context );
    SkullbonezCore::Core::SbResult ReleaseBackendOwnedRuntimeResources( const BackendResourceReleaseContext& context );
    RenderResourceLifecycle& ResourceLifecycle()
    {
        return m_resources;
    }
    const RenderResourceLifecycle& ResourceLifecycle() const
    {
        return m_resources;
    }

    // Opens the one frame-owned graph before Run chooses world or text-only
    // rendering. The caller must close it exactly once through a finalizer below.
    void BeginFrameGraph();
    Rendering::Dx12FrameOwner& RenderFrame() const
    {
        return m_resources.RenderFrame();
    }
    Rendering::Dx12RenderDevice& RenderDevice() const
    {
        return m_resources.RenderDevice();
    }
    Rendering::Dx12GraphTransientPool& RenderGraph() const
    {
        return m_resources.RenderGraph();
    }
    Rendering::Dx12ResourceBuilder& RenderResources() const
    {
        return m_resources.RenderResources();
    }
    Rendering::Dx12TextureOwner& RenderTextures() const
    {
        return m_resources.RenderTextures();
    }
    Rendering::Dx12GeometryOwner& RenderGeometry() const
    {
        return m_resources.RenderGeometry();
    }
    Rendering::Dx12Diagnostics& RenderDiagnostics() const
    {
        return m_resources.RenderDiagnostics();
    }
    const char* RendererName() const;
    void PrepareUiFrameTarget();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    SkullbonezCore::Core::SbResult RenderDevelopmentUi( DevelopmentTools::ImGuiEditorOwner& editor );
#endif

    // Adds the sole declaration-only Present edge and validates the submitted
    // frame contract before the swap-chain owner presents.
    void FinalizeFrameGraph();

    // Validates a restart frame with no Present edge, then releases every
    // callback/resource borrow before capture automation can replace the scene.
    void FinalizeCaptureOnlyFrameGraph();

    // Runs CPU frame preparation, then registers operation-specific callback
    // ABI records in one graph compile/execute cycle.
    int RenderUiText( RunTimerState& timers, const RuntimeRenderModelFrameView& models, double secondsPerFrame,
                      UiChromeGraphInvocation& chrome, UiOperatorDiagnosticsGraphInvocation& operatorDiagnostics,
                      UiOperatorSettingsGraphInvocation& operatorSettings,
                      UiOperatorInteractionGraphInvocation& operatorInteraction,
                      UiOperatorPresentationGraphInvocation& operatorPresentation,
                      UiOperatorSubmissionGraphInvocation& operatorSubmission, UiReplayGraphInvocation& replay );
    RenderDiagnosticsReadout BuildDiagnosticsReadout() const;
    const Rendering::RenderSceneSnapshot& FrameGraphSnapshot() const
    {
        return m_frameGraphSnapshot;
    }

  private:
    struct CinematicPostFrameOutput
    {
        bool volumetricPassExecuted = false;                                                                                      // Volumetric callback was scheduled for this post chain.
        bool volumetricReady = false;                                                                                             // Volumetric target was produced and can be sampled by tonemap.
        uint32_t volumetricTextureHandle = 0;                                                                                     // Renderer texture handle resolved from the graph-managed transient

        // Shader Resource View (SRV).
        uint32_t volumetricWidth = 0;                                                                                             // Materialized graph transient width for diagnostics.
        uint32_t volumetricHeight = 0;                                                                                            // Materialized graph transient height for diagnostics.
    };
    struct CinematicPostGraphInputs
    {
        const RenderCameraLighting& camera;
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic;
        Rendering::Dx12GeometryOwner& renderGeometry;
        Rendering::Dx12TextureOwner& renderTextures;
        Rendering::Dx12FrameOwner& renderFrame;
        Rendering::Dx12GraphTransientPool& renderGraph;
        Rendering::Dx12Diagnostics& renderDiagnostics;
        Rendering::RenderGpuTimingOwner& gpuTiming;
        int windowWidth = 1;
        int windowHeight = 1;
    };
    struct BackbufferAcquireGraphInputs
    {
        Rendering::Dx12GraphTransientPool& renderGraph;
        Rendering::Dx12FrameOwner& renderFrame;
        bool clearFrameTargets = false;
    };

    // Concept: each world-pass graph wrapper receives one named record so
    // visibility and target-selection flags cannot be swapped positionally.
    // Lifetime: references and pointers below are borrowed only for the
    // wrapper's synchronous compile, dry-run, and callback execution.
    struct ObjectGraphInputs
    {
        const ObjectPassInputs& pass;
        bool useCinematicTarget = false;
    };
    struct TerrainGraphInputs
    {
        const TerrainPassInputs& pass;
        bool useCinematicTarget = false;
    };
    struct WaterGraphInputs
    {
        const WaterPassInputs& pass;
        bool useCinematicTarget = false;
    };
    struct WorldExtensionGraphInputs
    {
        const RenderCameraLighting& camera;
        const Rendering::WorldRenderExtensionRegistration& registration;
        Rendering::Dx12TextureOwner& renderTextures;
        Rendering::Dx12GeometryOwner& renderGeometry;
        Rendering::Dx12Diagnostics& renderDiagnostics;
        Rendering::RenderGpuTimingOwner& gpuTiming;
        bool useCinematicTarget = false;
    };
    struct ReplayGhostGraphInputs
    {
        const RenderCameraLighting& camera;
        const RuntimeRenderModelFrameView& models;
        const Rendering::PrimitiveRenderContext& primitive;
        Textures::TextureCollection& textures;
        const ReplayVisualPacket& replayVisualPacket;
        bool useCinematicTarget = false;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        const Rendering::ShadowFrameData* shadow = nullptr;
    };
    struct DebugOverlayGraphInputs
    {
        const DebugOverlayPassInputs& pass;
        bool useCinematicTarget = false;
    };
    bool RenderPreparedFrame( const FrameEntryContext& context,
                              const SkullbonezCore::Core::CinematicRenderConfig& renderConfig, bool cinematicRender );
    RenderResourceContext BuildRenderResourceContext( bool cinematicRender );
    Rendering::RenderGraph& BeginRenderPassGraph();
    const Rendering::RenderGraphCompileResult& CompileRenderPassGraph( Rendering::RenderGraph& graph );
    void FinalizeFrameGraphInternal( const char* declarationOnlyPassName, bool appendPresent, bool releaseGraphStorage );
    void ExecuteBackbufferAcquireThroughRenderGraph( const BackbufferAcquireGraphInputs& inputs );
    ShadowPassOutput ExecuteShadowThroughRenderGraph( const ShadowPassInputs& pass );
    void ExecuteSkyboxThroughRenderGraph( const RenderCameraLighting& camera, Rendering::Dx12GeometryOwner& renderGeometry,
                                          Rendering::Dx12TextureOwner& renderTextures,
                                          Rendering::Dx12GraphTransientPool& renderGraph );
    ReflectionPassOutput ExecuteReflectionThroughRenderGraph( const ReflectionPassInputs& pass );
    void ExecuteSceneTargetBeginThroughRenderGraph( const RenderCameraLighting& camera, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                                    Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures,
                                                    Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12GraphTransientPool& renderGraph,
                                                    Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::RenderGpuTimingOwner& gpuTiming );
    void ExecuteObjectThroughRenderGraph( const ObjectGraphInputs& inputs );
    void ExecuteTerrainThroughRenderGraph( const TerrainGraphInputs& inputs );
    void ExecuteWaterThroughRenderGraph( const WaterGraphInputs& inputs );
    bool ExecuteWorldExtensionThroughRenderGraph( const WorldExtensionGraphInputs& inputs );
    DebugOverlaySnapshot BuildDebugOverlaySnapshot( const RuntimeRenderModelFrameView& models,
                                                    const RenderToolOverlayView& toolOverlay,
                                                    const RuntimeRenderFramePolicy& policy ) const;
    void ExecuteReplayGhostsThroughRenderGraph( const ReplayGhostGraphInputs& inputs );
    bool ExecuteDebugOverlayThroughRenderGraph( const DebugOverlayGraphInputs& inputs );
    CinematicPostFrameOutput ExecuteCinematicPostThroughRenderGraph( const CinematicPostGraphInputs& inputs );
    void ReleaseBackendOwnedResources( Rendering::Dx12GeometryOwner* renderGeometry );

    // Owner: backend-epoch state and cold setup live behind one resource seam;
    // frame graph and pass scheduling below retain no duplicate backend borrows.
    RenderResourceLifecycle m_resources;
    Environment::CameraCollection& m_cameras;                                                                                     // Active render-camera owner.
    Window& m_window;                                                                                                             // Client dimensions sampled for render targets.

    // Owner: render presentation policy survives backend rebuilds here; physics
    // state remains in its respective owner.
    RenderPresentationSettings m_presentationSettings;
    Environment::WorldEnvironment& m_world;                                                                                       // Fluid surface and gravity owner for pass contexts.
    Physics::CollisionVisualizer& m_collisionVisualizer;
    Physics::BroadphaseVisualizer& m_broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer;
    SkullbonezCore::Core::Profiler* m_profiler = nullptr;                                                                         // Startup-bound diagnostics source; null in non-profile builds.
    std::array<Math::Transformation::Matrix4, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_dxrReflectionTransforms = {}; // Scratch matrices for DXR Top-Level Acceleration Structure (TLAS) instance

    // upload.
    FullscreenQuadPass m_fullscreenQuadPass;                                                                                      // Shared full-screen vertex buffer pass used by sky/post effects.
    SkyPass m_skyPass;                                                                                                            // Background sky pass, reused by reflection and scene target passes.
    SceneTargetPass m_sceneTargetPass;                                                                                            // Cinematic HDR scene-target begin/release pass.
    ShadowPass m_shadowPass;                                                                                                      // Terrain/object shadow-map producer pass.
    ReflectionPass m_reflectionPass;                                                                                              // Water reflection texture producer pass.
    ObjectPass m_objectPass;                                                                                                      // Production body and collision-solid pass.
    TerrainPass m_terrainPass;                                                                                                    // Terrain material/shadow receiver pass.
    WaterPass m_waterPass;                                                                                                        // Calm/ocean water pass.
    DebugOverlayPass m_debugOverlayPass;                                                                                          // Broadphase and physics debug overlay pass.
    VolumetricPass m_volumetricPass;                                                                                              // Half-resolution cinematic light-shaft pass.
    TonemapPass m_tonemapPass;                                                                                                    // HDR-to-backbuffer resolve pass.

    // Runtime allocation policy: one owner scratch graph accumulates the whole
    // frame. Pass labels are borrowed literals and pass/resource lists are
    // bounded, so steady render frames do not create graph heaps.
    Rendering::RenderGraph m_renderPassGraphScratch;
    Rendering::RenderGraphCompileResult m_renderPassCompileScratch;
    Rendering::RenderSceneSnapshot m_frameGraphSnapshot;
    Rendering::Dx12GraphTransientPool* m_frameGraphRenderGraph = nullptr;
    bool m_frameGraphFinalized = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
