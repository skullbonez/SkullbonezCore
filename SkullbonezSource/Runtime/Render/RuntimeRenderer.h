/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.h
Purpose:
  Declares the runtime renderer owner for ordered render passes.

Mental model:
  RuntimeRenderer owns pass objects, backend-resource lifetime, and the frame
  pass order. Five named owner views supply lifetime-stable dependencies;
  immutable per-frame records carry submission facts and completed overlays.

Glossary:
  RuntimeRenderer: Owner of pass instances and the frame pass order.
  Pass order: The stable sequence of sky, shadows, reflection, objects, terrain,
    water, post effects, and UI/text.
  Lane R result: Recoverable resource setup or GPU-drain failure reported
    through an owner/message result instead of throwing through the render owner.
  Consequence grade: Frame-local dark/cool presentation override used when
    replay prediction wants causal overlays to dominate the image.
  Resource context: Creation/rebuild-only view of the renderer factory and
  resize-sensitive dimensions.
  Backend-owned resource: GPU object that must be released before backend
  teardown.

Invariants:
  - RuntimeRenderer owns pass instances; Run owns one RuntimeRenderer.
  - RenderFrame preserves the existing pass order and frame graph snapshot.
  - Backend resource release begins only after a successful GPU drain, then
    keeps consumer passes ahead of producer passes.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "RuntimeRenderHost.h"
#include "RuntimeRenderInputs.h"
#include "RuntimeRenderPasses.h"
#include "../../Rendering/Helper.h"
#include "../../Rendering/RenderGraph.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}
namespace Physics
{
class PhysicsEngine;
}
namespace Basics
{
class RuntimeRenderer
{
  public:
    struct BackendResourceReleaseContext
    {
        const char* phaseName = nullptr;
        Rendering::IRenderDeviceLifecycle* deviceLifecycle = nullptr;
        Rendering::IRenderResourceFactory* renderResources = nullptr;
        GameObjects::GameModelCollection& models;
        UI::InGameUI& ui;
        RuntimeTools& tools;
    };

    struct RegisteredResourceRebuildContext
    {
        Rendering::IRenderResourceFactory* renderResources = nullptr;
        Assets::AssetSystem& assets;
        Textures::TextureCollection& textures;
        const EngineConfig& config;
    };

    struct FrameEntryContext
    {
        RuntimeRenderBackendView backend;
        const RuntimeRenderModelFrameView& renderModels;
        // Owner: RuntimeRenderer. Reason: render-instance preparation must stay
        // after backend readiness and before replay render overrides until model
        // presentation prep has its own renderer snapshot owner. Deletion
        // condition: remove this borrow when model prep moves behind that owner.
        // Checker budget: RenderFrameEntry may use it only for PrepareRenderInstances().
        GameObjects::GameModelCollection& renderModelOwner;
        Physics::PhysicsEngine& physics;
        UI::InGameUI& ui;
        const RenderReplayOverlayView& replayOverlay;
        const RenderToolOverlayView& toolOverlay;
        const CinematicRenderConfig& cinematic;
        bool cinematicRequested = false;
        bool consequenceGradeRequested = false;    // True while replay prediction should fade into the causality look.
    };

    RuntimeRenderer( RuntimeRenderBackendView backend,
                     const RenderWorldView& world,
                     const RenderSceneView& scene,
                     const RenderReplayOverlayView& replayOverlay,
                     const RenderToolOverlayView& toolOverlay,
                     const RenderUiView& ui );

    void EnsureFrameResources( const RenderResourceContext& resources );
    // Packages model-owned render/debug views before the frame passes consume them.
    RuntimeRenderModelFrameView BuildModelFrameView( GameObjects::GameModelCollection& models,
                                                     Physics::PhysicsEngine& physics ) const;
    void RenderFrameEntry( const FrameEntryContext& context );
    void RenderFrame( const RuntimeRenderInputs& renderInputs );
    void ReleaseBackendOwnedResources( Rendering::IRenderResourceFactory* renderResources );
    SbResult ReleaseBackendOwnedRuntimeResources( const BackendResourceReleaseContext& context );
    SbResult RebuildRegisteredRenderResources( const RegisteredResourceRebuildContext& context );
    RenderHelper& Helper()
    {
        assert( m_renderHelper.has_value() );
        return *m_renderHelper;
    }
    const RenderHelper& Helper() const
    {
        assert( m_renderHelper.has_value() );
        return *m_renderHelper;
    }

    SbResult EnsureUiTextResources( Rendering::IRenderResourceFactory& renderResources,
                                    const Assets::AssetSystem& assets,
                                    int screenW,
                                    int screenH );
    bool ShouldRenderUiText( const UiTextPassState& state ) const;
    void SetUiTextRayTracingCapability( Rendering::IRenderRayTracing* renderRayTracing );
    void RenderUiText( Rendering::IRenderDiagnostics& renderDiagnostics,
                       const UI::UIRenderContext& uiRender,
                       const UiTextPassState& state,
                       const RuntimeRenderModelFrameView& models,
                       DiagnosticsRuntime& diagnosticsRuntime,
                       ReplayRuntime& replayRuntime,
                       const ReplayOverlayFrameState& replayOverlay,
                       const CinematicRenderConfig& cinematic,
                       bool cinematicRendering,
                       double dSecondsPerFrame );

  private:
    // Concept: callback-owned results record which render passes executed
    // through the temporary RenderGraph callback path this frame.
    //
    // Why: diagnostics still compare direct pass execution with callback-owned
    // graph execution while the graph API carries C-style userData. RuntimeRenderer
    // owns these flags because pass scheduling is its responsibility.
    //
    // Deletion condition: remove these flags with the callback payload structs
    // when render passes become typed graph nodes. Checker budget: callback
    // accounting stays renderer-local and may not add concrete scene-container
    // or Run borrows to Runtime/Render contracts.
    struct CinematicPostGraphResult
    {
        bool volumetricReady = false;              // Volumetric target was produced and can be sampled by tonemap.
        bool volumetricCallbackOwned = false;      // Volumetric command recording ran through RenderGraph callback execution.
        bool tonemapCallbackOwned = false;         // Tonemap command recording ran through RenderGraph callback execution.
        uint32_t volumetricTextureHandle = 0;      // Renderer texture handle resolved from the graph-managed transient SRV.
        uint32_t volumetricWidth = 0;              // Materialized graph transient width for diagnostics.
        uint32_t volumetricHeight = 0;             // Materialized graph transient height for diagnostics.
    };
    struct GraphPassResult
    {
        bool rendered = false;                     // Pass body produced visible output.
        bool callbackOwned = false;                // Pass scheduling ran through RenderGraph callback execution.
    };
    struct ShadowGraphResult
    {
        ShadowPassOutput output;                   // Shadow maps produced by the callback-owned shadow pass.
        bool callbackOwned = false;                // Shadow pass scheduling ran through RenderGraph callback execution.
    };
    struct ReflectionGraphResult
    {
        ReflectionPassOutput output;               // Reflection texture produced by the callback-owned reflection pass.
        bool callbackOwned = false;                // Reflection pass scheduling ran through RenderGraph callback execution.
    };

    RenderFrameContext BuildRenderFrameContext( const RuntimeRenderInputs& renderInputs,
                                                bool cinematicRender,
                                                const CinematicRenderConfig& renderConfig );
    RenderResourceContext BuildRenderResourceContext( const RuntimeRenderInputs& renderInputs,
                                                      bool cinematicRender ) const;
    Rendering::RenderGraph& BeginRenderPassGraph();
    const Rendering::RenderGraphCompileResult& CompileRenderPassGraph( Rendering::RenderGraph& graph );
    ShadowGraphResult ExecuteShadowThroughRenderGraph( const RenderFrameContext& frame,
                                                       const CinematicRenderConfig* activeShadowConfig,
                                                       bool terrainHidden,
                                                       bool collisionVisualizerVisible );
    bool ExecuteSkyboxThroughRenderGraph( const RenderFrameContext& frame );
    ReflectionGraphResult ExecuteReflectionThroughRenderGraph( const RenderFrameContext& frame,
                                                               const CinematicRenderConfig* activeCinematic,
                                                               const Rendering::ShadowFrameData* objectShadow,
                                                               bool collisionStateColorsVisible,
                                                               bool debugTransparentBodyPass,
                                                               float collisionVisualizerAlphaOverride,
                                                               float bodyAlpha,
                                                               bool waterRayTracingReflection,
                                                               bool waterNoReflection,
                                                               float simulationTimeSeconds );
    bool ExecuteSceneTargetBeginThroughRenderGraph( const RenderFrameContext& frame );
    bool ExecuteObjectThroughRenderGraph( const RenderFrameContext& frame,
                                          ObjectPassMode mode,
                                          bool useCinematicTarget,
                                          const CinematicRenderConfig* activeCinematic,
                                          const Rendering::ShadowFrameData* objectShadow,
                                          bool collisionStateColorsVisible,
                                          float collisionVisualizerAlphaOverride,
                                          float bodyAlpha,
                                          const std::vector<uint8_t>* replayFocusModelMask,
                                          bool drawMaskedModels );
    bool ExecuteTerrainThroughRenderGraph( const RenderFrameContext& frame,
                                           bool useCinematicTarget,
                                           const CinematicRenderConfig* activeCinematic,
                                           const Rendering::ShadowFrameData* terrainShadow,
                                           bool terrainHidden );
    bool ExecuteWaterThroughRenderGraph( const RenderFrameContext& frame,
                                         const ReflectionPassOutput& reflection,
                                         bool useCinematicTarget,
                                         const CinematicRenderConfig* activeCinematic,
                                         bool waterHidden,
                                         bool flatWater,
                                         bool noReflection,
                                         bool freezeTime,
                                         float frozenTime,
                                         float liveWaterTime );
    TornadoVisualSnapshot BuildTornadoVisualSnapshot() const;
    GraphPassResult ExecuteTornadoVisualThroughRenderGraph( const RenderFrameContext& frame,
                                                            bool useCinematicTarget,
                                                            const TornadoVisualSnapshot& snapshot );
    DebugOverlaySnapshot BuildDebugOverlaySnapshot( const RenderFrameContext& frame ) const;
    bool ExecuteReplayGhostsThroughRenderGraph( const RenderFrameContext& frame,
                                                bool useCinematicTarget,
                                                const CinematicRenderConfig* activeCinematic,
                                                const Rendering::ShadowFrameData* objectShadow );
    bool ExecuteDebugOverlayThroughRenderGraph( const RenderFrameContext& frame, bool useCinematicTarget );
    CinematicPostGraphResult ExecuteCinematicPostThroughRenderGraph( const RenderFrameContext& frame );
    bool ExecuteUiTextThroughRenderGraph( Rendering::IRenderDiagnostics& renderDiagnostics,
                                          const UI::UIRenderContext& uiRender,
                                          const UiTextPassState& state,
                                          const RuntimeRenderModelFrameView& models,
                                          DiagnosticsRuntime& diagnosticsRuntime,
                                          ReplayRuntime& replayRuntime,
                                          const ReplayOverlayFrameState& replayOverlay,
                                          const CinematicRenderConfig& cinematic,
                                          bool cinematicRendering,
                                          Rendering::IRenderRayTracing* renderRayTracing,
                                          double secondsPerFrame );
    RenderResourceLifecycleLog m_lifecycleLog;     // Concrete renderer-owned lifecycle diagnostic writer.
    Assets::AssetSystem& m_assets;                 // Registered render asset/shader lookup owner.
    Textures::TextureCollection& m_textures;       // Stable texture handle/select owner.
    Environment::CameraCollection& m_cameras;      // Active render-camera owner.
    std::unique_ptr<Geometry::Terrain>& m_terrain; // World terrain lifetime borrowed by terrain/debug passes.
    std::unique_ptr<Geometry::SkyBox>& m_skyBox;   // Sky resource lifetime released before backend teardown.
    Window& m_window;                              // Client dimensions sampled for render targets.
    RunRenderPassResources& m_passResources;       // GPU objects owned by named RuntimeRenderer passes.
    RunDebugState& m_debug;                        // Frame/debug toggles sampled by render scheduling.
    RunTimerState& m_timers;                       // Simulation clock used by visual/replay overlays.
    EngineConfig& m_config;                        // Process config that owns ordinary render style.
    RunRuntimeSettings& m_runtimeSettings;         // Runtime-toggled render/physics presentation settings.
    Environment::WorldEnvironment& m_world;        // Fluid surface and gravity owner for pass contexts.
    std::optional<RenderHelper> m_renderHelper;    // Backend-lifetime primitive render cache and batch scratch.
    Physics::CollisionVisualizer& m_collisionVisualizer;
    Physics::BroadphaseVisualizer& m_broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer;
    RuntimeTools& m_runtimeTools;                  // Tool overlay owner used outside hot render loops.
    RunEditorPlacementState& m_editor;             // Editor overlay state sampled once per frame.
    RunCameraState& m_camera;                      // Current camera mode needed by tool overlay wake-up checks.
    Profiler* m_profiler = nullptr;                // Startup-bound diagnostics source; null in non-profile builds.
    ReplayRuntime& m_replayRuntime;                // Replay presentation owner for ghost/focus overlays.
    int m_toolOverlaySceneFrame = 0;               // Scene frame paired with the prepared fixed-capacity overlay records.
    uint64_t m_toolOverlayGrowthEventCount = 0;    // Replay reserve counter sampled by the composition root.
    float m_consequenceGradeStrength = 0.0f;       // Render-owned fade strength for the frame-local consequence grade.
    std::chrono::steady_clock::time_point
        m_consequenceGradeLastTick;                // Wall-clock anchor for the grade crossfade; zero means uninitialized.
    std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms =
        {};                                        // Scratch matrices for DXR TLAS instance upload.
    FullscreenQuadPass m_fullscreenQuadPass;       // Shared full-screen vertex buffer pass used by sky/post effects.
    SkyPass m_skyPass;                             // Background sky pass, reused by reflection and scene target passes.
    SceneTargetPass m_sceneTargetPass;             // Cinematic HDR scene-target begin/release pass.
    ShadowPass m_shadowPass;                       // Terrain/object shadow-map producer pass.
    ReflectionPass m_reflectionPass;               // Water reflection texture producer pass.
    ObjectPass m_objectPass;                       // Production body and collision-solid pass.
    TerrainPass m_terrainPass;                     // Terrain material/shadow receiver pass.
    WaterPass m_waterPass;                         // Calm/ocean water pass.
    TornadoVisualPass m_tornadoVisualPass;         // Sparse alpha tornado shell/dust pass.
    DebugOverlayPass m_debugOverlayPass;           // Broadphase and physics debug overlay pass.
    VolumetricPass m_volumetricPass;               // Half-resolution cinematic light-shaft pass.
    TonemapPass m_tonemapPass;                     // HDR-to-backbuffer resolve pass.
    UiTextPass m_uiTextPass;                       // HUD/UI/text pass.
    // Runtime allocation policy: graph wrapper passes reuse this owner scratch
    // storage. Pass labels are borrowed literals and per-pass reads/writes are
    // bounded, so steady render frames do not create per-wrapper graph heaps.
    Rendering::RenderGraph m_renderPassGraphScratch;
    Rendering::RenderGraphCompileResult m_renderPassCompileScratch;
    // Lifetime: borrowed only for the next UI pass after world rendering, then
    // refreshed or cleared before backend release and text-only frames.
    Rendering::IRenderRayTracing* m_uiTextRayTracing = nullptr;
};
} // namespace Basics
} // namespace SkullbonezCore
