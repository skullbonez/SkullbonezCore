/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.h
Purpose:
  Declares the runtime renderer owner for ordered render passes.

Summary:
  RuntimeRenderer owns pass objects and backend-resource lifetime. One live
  RenderGraph owns the frame pass order. Five named owner views supply lifetime-stable dependencies;
  immutable per-frame records carry submission facts and completed overlays.
  UiTextPass owns its font batch and presentation capabilities; this class
  contributes only the late graph-scheduling edge for UI text.
  A content-neutral extension scope lets higher composition append one typed
  world pass at the renderer-owned post-water scheduling point.

Glossary:
  RuntimeRenderer: Owner of pass instances and the live frame-graph builder.
  Pass order: The stable sequence of shadows, sky, reflection, objects, terrain,
    water, post effects, and UI/text.
  Lane R result: Recoverable resource setup or GPU-drain failure reported
    through an owner/message result instead of throwing through the render owner.
  Consequence grade: Replay-owned [0,1] scalar copied into one frame to make
    causal overlays dominate the image; RuntimeRenderer stores no fade state.
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
  - UI-text input is one stack record consumed synchronously by UiTextPass.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "RuntimeRenderHost.h"
#include "RuntimeRenderInputs.h"
#include "RuntimeRenderPasses.h"
#include "RuntimeRenderResources.h"
#include "RenderPresentationSettings.h"
#include "../../Assets/TextureCollection.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/RenderGpuTimingOwner.h"
#include "../../Rendering/RenderGraph.h"
#include "../../Rendering/RenderSceneSnapshot.h"
#include "../../Rendering/WorldRenderExtension.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
class SceneWorld;
}
namespace Physics
{
class PhysicsEngine;
}
namespace Threading
{
class WorkerPool;
}
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
namespace Runtime::DevelopmentTools
{
class ImGuiEditorOwner;
}
#endif
namespace Runtime
{
class RuntimeRenderer
{
  public:
    struct BackendResourceReleaseContext
    {
        const char* phaseName = nullptr;
        Rendering::Dx12FrameOwner* renderFrame = nullptr;
        Rendering::Dx12ResourceBuilder* renderResources = nullptr;
        Rendering::Dx12TextureOwner* renderTextures = nullptr;
        Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
        UI::InGameUI& ui;
        RuntimeTools& tools;
    };

    struct FrameEntryContext
    {
        const RuntimeRenderModelFrameView& renderModels;
        UI::InGameUI& ui;
        RuntimeRenderFramePolicy framePolicy;
        const RenderReplayOverlayView& replayOverlay;
        const RenderToolOverlayView& toolOverlay;
        const Rendering::WorldRenderExtensionRegistration& worldExtension;
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic;
        float presentationAlpha = 1.0f;                   // Exact solver state is 1; live frames may use the accumulator fraction.
        bool cinematicRequested = false;
        float consequenceGradeStrength = 0.0f;            // Replay-owned [0,1] fade copied for this frame.
    };

    RuntimeRenderer( Rendering::Dx12RenderDevice* renderDevice,
                     Rendering::Dx12FrameOwner* renderFrame,
                     Rendering::Dx12GraphTransientPool* renderGraph,
                     Rendering::Dx12ResourceBuilder* renderResources,
                     Rendering::Dx12TextureOwner* renderTextures,
                     Rendering::Dx12GeometryOwner* renderGeometry,
                     Rendering::Dx12Diagnostics* renderDiagnostics,
                     Rendering::Dx12RaytracingOwner* renderRayTracing,
                     const RenderWorldView& world,
                     RunSceneState& scene );
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
    // Packages model-owned render/debug views before the frame passes consume them.
    RuntimeRenderModelFrameView BuildModelFrameView( Runtime::SceneWorld& scene,
                                                     Threading::WorkerPool& workerPool,
                                                     const SkullbonezCore::Core::EngineConfig& config ) const;
    bool RenderFrameEntry( const FrameEntryContext& context );
    bool RenderFrame( const RuntimeRenderInputs& renderInputs );
    void ReleaseBackendOwnedResources( Rendering::Dx12TextureOwner* renderTextures,
                                       Rendering::Dx12GeometryOwner* renderGeometry );
    SkullbonezCore::Core::SbResult ReleaseBackendOwnedRuntimeResources( const BackendResourceReleaseContext& context );
    SkullbonezCore::Core::SbResult InitialiseProcessResources( Rendering::Dx12ResourceBuilder& renderResources,
                                                               Rendering::Dx12TextureOwner& renderTextures,
                                                               Rendering::Dx12GeometryOwner& renderGeometry,
                                                               const SkullbonezCore::Core::EngineConfig& config,
                                                               bool dumpTextureAssets );
    // Scene activation asks the renderer to warm its optional ray-tracing
    // geometry. The renderer uses its startup-bound concrete owners; scene code
    // supplies only the active capacity while mesh selection and capability
    // checks stay here.
    SkullbonezCore::Core::SbResult InitialiseSceneRayTracing( int modelCapacity );
    // Projects framebuffer metadata into values safe for the UI to retain for
    // the current draw; no framebuffer or pass-resource ownership escapes.
    RuntimeRenderTargetPreviewSnapshot BuildRenderTargetPreviewSnapshot( bool shadowsAvailable,
                                                                         bool cinematicTargetsAvailable,
                                                                         bool volumetricAvailable ) const;
    Rendering::PrimitiveBatchRenderer& PrimitiveBatches()
    {
        assert( m_primitiveBatches.has_value() );
        return *m_primitiveBatches;
    }
    const Rendering::PrimitiveBatchRenderer& PrimitiveBatches() const
    {
        assert( m_primitiveBatches.has_value() );
        return *m_primitiveBatches;
    }

    SkullbonezCore::Core::SbResult EnsureUiTextResources( Rendering::Dx12ResourceBuilder& renderResources,
                                                          Rendering::Dx12TextureOwner& renderTextures,
                                                          Rendering::Dx12GeometryOwner& renderGeometry,
                                                          const Assets::AssetSystem& assets,
                                                          int screenW,
                                                          int screenH );
    bool ShouldRenderUiText( const UiTextPassState& state, const UI::InGameUI& ui ) const;
    void SetUiTextRayTracingCapability( Rendering::Dx12RaytracingOwner* renderRayTracing );
    // Opens the one frame-owned graph before Run chooses world or text-only
    // rendering. The caller must close it exactly once through a finalizer below.
    void BeginFrameGraph( Rendering::Dx12GraphTransientPool& renderGraph );
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
    // Schedules the cohesive UI-text owner with one stack-only frame record.
    // RuntimeRenderer owns graph order, not HUD/replay/operator composition.
    void RenderUiText( const UiTextPassInputs& inputs );

  private:
    struct CinematicPostFrameOutput
    {
        bool volumetricPassExecuted = false;              // Volumetric callback was scheduled for this post chain.
        bool volumetricReady = false;                     // Volumetric target was produced and can be sampled by tonemap.
        uint32_t volumetricTextureHandle =
            0;                                            // Renderer texture handle resolved from the graph-managed transient Shader Resource View (SRV).
        uint32_t volumetricWidth = 0;                     // Materialized graph transient width for diagnostics.
        uint32_t volumetricHeight = 0;                    // Materialized graph transient height for diagnostics.
    };
    struct CinematicPostGraphInputs
    {
        const RenderFrameContext& frame;                  // Complete immutable render facts borrowed for this post chain.
    };
    struct BackbufferAcquireGraphInputs
    {
        Rendering::Dx12GraphTransientPool& renderGraph;
        Rendering::Dx12FrameOwner& renderFrame;
        bool clearFrameTargets = false;
    };
    struct ShadowGraphInputs
    {
        const RenderFrameContext& frame;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        bool terrainHidden = false;
        bool collisionVisualizerVisible = false;
    };
    struct ReflectionGraphInputs
    {
        const RenderFrameContext& frame;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        const Rendering::ShadowFrameData* objectShadow = nullptr;
        bool collisionStateColorsVisible = false;
        bool transparentBodyPass = false;
        float collisionVisualizerAlphaOverride = -1.0f;
        float bodyAlpha = 1.0f;
        bool waterRayTracingReflection = false;
        bool waterNoReflection = false;
        float simulationTimeSeconds = 0.0f;
    };
    // Concept: each world-pass graph wrapper receives one named record so
    // visibility and target-selection flags cannot be swapped positionally.
    // Lifetime: references and pointers below are borrowed only for the
    // wrapper's synchronous compile, dry-run, and callback execution.
    struct ObjectGraphInputs
    {
        const RenderFrameContext& frame;
        ObjectPassMode mode = ObjectPassMode::Opaque;
        bool useCinematicTarget = false;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        const Rendering::ShadowFrameData* shadow = nullptr;
        bool collisionStateColorsVisible = false;
        float collisionVisualizerAlphaOverride = -1.0f;
        float bodyAlpha = 1.0f;
        const std::vector<uint8_t>* modelMask = nullptr;
        bool drawMaskedModels = true;
    };
    struct TerrainGraphInputs
    {
        const RenderFrameContext& frame;
        bool useCinematicTarget = false;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        const Rendering::ShadowFrameData* shadow = nullptr;
        const Rendering::ShadowFrameData* detailShadow = nullptr;
        bool terrainHidden = false;
    };
    struct WaterGraphInputs
    {
        const RenderFrameContext& frame;
        const ReflectionPassOutput& reflection;
        bool useCinematicTarget = false;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        bool waterHidden = false;
        bool flatWater = false;
        bool noReflection = false;
        bool freezeTime = false;
        float frozenTime = 0.0f;
        float liveWaterTime = 0.0f;
    };
    struct WorldExtensionGraphInputs
    {
        const RenderFrameContext& frame;
        const Rendering::WorldRenderExtensionRegistration& registration;
        bool useCinematicTarget = false;
    };
    struct ReplayGhostGraphInputs
    {
        const RenderFrameContext& frame;
        const ReplayVisualPacket& replayVisualPacket;
        bool useCinematicTarget = false;
        const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
        const Rendering::ShadowFrameData* shadow = nullptr;
    };
    struct DebugOverlayGraphInputs
    {
        const RenderFrameContext& frame;
        const RuntimeRenderServices& services;
        bool useCinematicTarget = false;
    };
    RenderFrameContext BuildRenderFrameContext( const RuntimeRenderInputs& renderInputs,
                                                bool cinematicRender,
                                                const SkullbonezCore::Core::CinematicRenderConfig& renderConfig );
    RenderResourceContext BuildRenderResourceContext( const RuntimeRenderInputs& renderInputs,
                                                      bool cinematicRender ) const;
    Rendering::RenderGraph& BeginRenderPassGraph();
    const Rendering::RenderGraphCompileResult& CompileRenderPassGraph( Rendering::RenderGraph& graph );
    void
    FinalizeFrameGraphInternal( const char* declarationOnlyPassName, bool appendPresent, bool releaseGraphStorage );
    void ExecuteBackbufferAcquireThroughRenderGraph( const BackbufferAcquireGraphInputs& inputs );
    ShadowPassOutput ExecuteShadowThroughRenderGraph( const ShadowGraphInputs& inputs );
    void ExecuteSkyboxThroughRenderGraph( const RenderFrameContext& frame );
    ReflectionPassOutput ExecuteReflectionThroughRenderGraph( const ReflectionGraphInputs& inputs );
    void ExecuteSceneTargetBeginThroughRenderGraph( const RenderFrameContext& frame );
    void ExecuteObjectThroughRenderGraph( const ObjectGraphInputs& inputs );
    void ExecuteTerrainThroughRenderGraph( const TerrainGraphInputs& inputs );
    void ExecuteWaterThroughRenderGraph( const WaterGraphInputs& inputs );
    bool ExecuteWorldExtensionThroughRenderGraph( const WorldExtensionGraphInputs& inputs );
    DebugOverlaySnapshot BuildDebugOverlaySnapshot( const RuntimeRenderServices& services ) const;
    void ExecuteReplayGhostsThroughRenderGraph( const ReplayGhostGraphInputs& inputs );
    bool ExecuteDebugOverlayThroughRenderGraph( const DebugOverlayGraphInputs& inputs );
    CinematicPostFrameOutput ExecuteCinematicPostThroughRenderGraph( const CinematicPostGraphInputs& inputs );
    void ExecuteUiTextThroughRenderGraph( const UiTextPassInputs& inputs );
    // Lifetime: startup binds the exact concrete DX12 owners used by this
    // cohesive renderer. Frame records never republish this authority, and the
    // pointers remain valid until the enclosing Run/backend lifetime ends.
    Rendering::Dx12FrameOwner* m_renderFrame = nullptr;
    Rendering::Dx12GraphTransientPool* m_renderGraph = nullptr;
    Rendering::Dx12ResourceBuilder* m_renderResources = nullptr;
    Rendering::Dx12TextureOwner* m_renderTextures = nullptr;
    Rendering::Dx12GeometryOwner* m_renderGeometry = nullptr;
    Rendering::Dx12Diagnostics* m_renderDiagnostics = nullptr;
    Rendering::Dx12RaytracingOwner* m_renderRayTracing = nullptr;
    RenderResourceLifecycleLog m_lifecycleLog;            // Concrete renderer-owned lifecycle diagnostic writer.
    Assets::AssetSystem& m_assets;                        // Registered render asset/shader lookup owner.
    Textures::TextureCollection m_textures;               // Stable texture handle/select owner.
    Environment::CameraCollection& m_cameras;             // Active render-camera owner.
    SceneTerrain& m_terrain;                              // Scene terrain owner borrowed by terrain/debug passes.
    std::unique_ptr<Geometry::SkyBox> m_skyBox;           // Sky resource lifetime released before backend teardown.
    Window& m_window;                                     // Client dimensions sampled for render targets.
    RuntimeRenderPassResources m_passResources;           // GPU objects owned by named RuntimeRenderer passes.
    SkullbonezCore::Core::EngineConfig& m_config;         // Process config that owns ordinary render style.
    // Owner: render presentation policy survives backend rebuilds here; physics
    // state remains in its respective owner.
    RenderPresentationSettings m_presentationSettings;
    Environment::WorldEnvironment& m_world;               // Fluid surface and gravity owner for pass contexts.
    std::optional<Rendering::PrimitiveBatchRenderer>
        m_primitiveBatches;                               // Backend-lifetime primitive render cache and batch scratch.
    Physics::CollisionVisualizer& m_collisionVisualizer;
    Physics::BroadphaseVisualizer& m_broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer;
    SkullbonezCore::Core::Profiler* m_profiler = nullptr; // Startup-bound diagnostics source; null in non-profile builds.
    Rendering::RenderGpuTimingOwner m_renderGpuTiming;    // Concrete renderer query/event owner; Core receives values only.
    std::array<Math::Transformation::Matrix4, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        m_dxrReflectionTransforms =
            {};                                           // Scratch matrices for DXR Top-Level Acceleration Structure (TLAS) instance upload.
    FullscreenQuadPass m_fullscreenQuadPass;              // Shared full-screen vertex buffer pass used by sky/post effects.
    SkyPass m_skyPass;                                    // Background sky pass, reused by reflection and scene target passes.
    SceneTargetPass m_sceneTargetPass;                    // Cinematic HDR scene-target begin/release pass.
    ShadowPass m_shadowPass;                              // Terrain/object shadow-map producer pass.
    ReflectionPass m_reflectionPass;                      // Water reflection texture producer pass.
    ObjectPass m_objectPass;                              // Production body and collision-solid pass.
    TerrainPass m_terrainPass;                            // Terrain material/shadow receiver pass.
    WaterPass m_waterPass;                                // Calm/ocean water pass.
    DebugOverlayPass m_debugOverlayPass;                  // Broadphase and physics debug overlay pass.
    VolumetricPass m_volumetricPass;                      // Half-resolution cinematic light-shaft pass.
    TonemapPass m_tonemapPass;                            // HDR-to-backbuffer resolve pass.
    UiTextPass m_uiTextPass;                              // Cohesive HUD/UI/text resources and composition owner.
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
