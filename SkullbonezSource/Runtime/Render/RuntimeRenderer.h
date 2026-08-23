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
  Backend-owned resource: GPU object that must be released before backend
    teardown.
  World extension: One synchronous callback-owned graphics pass registered by
    higher composition without exposing its content owner to this renderer.

Invariants:
  - RuntimeRenderer owns pass instances; Run engages one after backend binding.
  - One graph accumulates world, late UI, development UI, and Present rows in
    execution order; wrappers never clear or reconstruct it mid-frame.
  - Backend resource release begins only after a successful GPU drain, then
    keeps consumer passes ahead of producer passes.
  - The world-extension registration is consumed before its stack scope ends.
  - UI-text callers compose product UI before submission. RuntimeRenderer sees
    only foundation draw commands and the parallel renderer-owned preview
    snapshot; graph callback ABI records and backend injection remain private.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RuntimeRenderHost.h"
#include "RuntimeRenderFrameValues.h"
#include "RuntimeRenderPasses.h"
#include "BroadphaseVisualizer.h"
#include "CollisionVisualizer.h"
#include "PhysicsDebugVisualizer.h"
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

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
struct ImDrawData;
struct ImGuiContext;
#endif

namespace SkullbonezCore
{
namespace Rendering
{
class RenderBackendDX12;
}
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
namespace Rendering
{
class Dx12ImGuiRendererOwner;
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

class RuntimeRenderer
{
  public:
    struct BackendResourceReleaseContext
    {
        const char* phaseName = nullptr;
        Geometry::Terrain* terrain = nullptr;
    };

    struct FrameEntryContext
    {
        const RuntimeRenderModelFrameView& renderModels;
        const RenderCameraLighting& camera;
        Geometry::Terrain* terrain = nullptr;
        RuntimeRenderFramePolicy framePolicy;
        const ReplayRenderFrameView& replayFrame;
        const Rendering::RetainedGeometryPacket& retainedOverlay;
        const RenderToolOverlayView& toolOverlay;
        const Rendering::WorldRenderExtensionRegistration& worldExtension;
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic;
        bool cinematicRequested = false;
    };

    RuntimeRenderer( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                     Rendering::RenderBackendDX12& backend, const RenderWorldView& world,
                     int sceneIndex, int sceneLoadCount );
    ~RuntimeRenderer();

    // Runs after Core FrameBegin and before draw-call counters reset. This
    // reads completed GPU samples and publishes the preceding render counters.
    void BeginProfilerFrame();
    void UpdateDebugVisualizers( float secondsPerFrame, const RuntimeRenderModelFrameView& models,
                                 const RuntimeRenderFramePolicy& policy );

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
    void SetSceneIdentity( int sceneIndex, int sceneLoadCount )
    {
        m_resources.Log().SetSceneIdentity( sceneIndex, sceneLoadCount );
    }

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
    SkullbonezCore::Core::SbResult RenderDevelopmentUi( ImGuiContext* context, ImDrawData* drawData );
#endif

    // Adds the sole declaration-only Present edge and validates the submitted
    // frame contract before the swap-chain owner presents.
    void FinalizeFrameGraph();

    // Validates a restart frame with no Present edge, then releases every
    // callback/resource borrow before capture automation can replace the scene.
    void FinalizeCaptureOnlyFrameGraph();

    // Each operation consumes only its focused inputs. Renderer-local callback
    // records borrow those inputs until that operation's graph completes.
    int BeginUiTextFrame( const UiTextViewport& viewport );
    void SubmitUiChrome( const UiTextViewport& viewport, const UiChromeStatusValues& status,
                         const UiChromeTailValues& tail );
    void PrepareOperatorUiSubmission( const UiTextViewport& viewport, bool drawTestPattern );
    void AppendDxrReflectionPreview( RuntimeRenderTargetPreviewSnapshot& previews, const UiTextViewport& viewport,
                                     bool available ) const;
    void SubmitOperatorUiDrawList( const UI::UIDrawList& drawList,
                                   const RuntimeRenderTargetPreviewSnapshot& previews, Assets::AssetSystem& assets,
                                   const UiTextViewport& viewport );
    void SubmitUiOverlay( const UiTextViewport& viewport, UiOverlayMode mode, int modelCount, float rollingFpsTime,
                          float sceneEnergyForDisplay );
    void SubmitUiDrawList( const UI::UIDrawList& drawList, const UiTextViewport& viewport );
    void FinalizeUiOverlay( UiOverlayMode mode );
    int EndUiTextFrame( int drawCallStart );
    RenderDiagnosticsReadout BuildDiagnosticsReadout() const;
    const Rendering::RenderSceneSnapshot& FrameGraphSnapshot() const
    {
        return m_frameGraphSnapshot;
    }

  private:
    struct CinematicPostFrameOutput
    {
        bool volumetricPassExecuted = false;  // Volumetric callback was scheduled for this post chain.
        bool volumetricReady = false;         // Volumetric target was produced and can be sampled by tonemap.
        uint32_t volumetricTextureHandle = 0; // Renderer texture handle resolved from the graph-managed transient

        // Shader Resource View (SRV).
        uint32_t volumetricWidth = 0;  // Materialized graph transient width for diagnostics.
        uint32_t volumetricHeight = 0; // Materialized graph transient height for diagnostics.
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
        Geometry::Terrain* terrain = nullptr;
        bool useCinematicTarget = false;
    };
    struct ReplayGhostGraphInputs
    {
        const RenderCameraLighting& camera;
        const RuntimeRenderModelFrameView& models;
        Rendering::PrimitiveBatchRenderer& primitiveRenderer;
        const SkullbonezCore::Core::OrdinaryRenderConfig& ordinaryLighting;
        const char* primitiveShaderBaseName;
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
    void ExecuteSceneTargetBeginThroughRenderGraph(
        const RenderCameraLighting& camera, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
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
    void ReleaseBackendOwnedResources( Rendering::Dx12GeometryOwner* renderGeometry, Geometry::Terrain* terrain );

    // Owner: backend-epoch state and cold setup live behind one resource seam;
    // frame graph and pass scheduling below retain no duplicate backend borrows.
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    RenderResourceLifecycle m_resources;
    Window& m_window; // Client dimensions sampled for render targets.

    // Owner: render presentation policy survives backend rebuilds here; physics
    // state remains in its respective owner.
    RenderPresentationSettings m_presentationSettings;
    Environment::WorldEnvironment& m_world; // Fluid surface and gravity owner for pass contexts.
    CollisionVisualizer m_collisionVisualizer;
    BroadphaseVisualizer m_broadphaseVisualizer;
    PhysicsDebugVisualizer m_physicsDebugVisualizer;
    SkullbonezCore::Core::Profiler* m_profiler = nullptr; // Startup-bound diagnostics source; null in non-profile builds.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    Rendering::Dx12ImGuiRendererOwner* m_developmentUiRenderer = nullptr;
#endif
    std::array<Math::Transformation::Matrix4, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> m_dxrReflectionTransforms =
        {}; // Scratch matrices for DXR Top-Level Acceleration Structure (TLAS) instance

    // upload.
    FullscreenQuadPass m_fullscreenQuadPass; // Shared full-screen vertex buffer pass used by sky/post effects.
    SkyPass m_skyPass;                       // Background sky pass, reused by reflection and scene target passes.
    SceneTargetPass m_sceneTargetPass;       // Cinematic HDR scene-target begin/release pass.
    ShadowPass m_shadowPass;                 // Terrain/object shadow-map producer pass.
    ReflectionPass m_reflectionPass;         // Water reflection texture producer pass.
    ObjectPass m_objectPass;                 // Production body and collision-solid pass.
    TerrainPass m_terrainPass;               // Terrain material/shadow receiver pass.
    WaterPass m_waterPass;                   // Calm/ocean water pass.
    DebugOverlayPass m_debugOverlayPass;     // Broadphase and physics debug overlay pass.
    VolumetricPass m_volumetricPass;         // Half-resolution cinematic light-shaft pass.
    TonemapPass m_tonemapPass;               // HDR-to-backbuffer resolve pass.

    // Runtime allocation policy: one owner scratch graph accumulates the whole
    // frame. Pass labels are borrowed literals and pass/resource lists are
    // bounded, so steady render frames do not create graph heaps.
    Rendering::RenderGraph m_renderPassGraphScratch;
    Rendering::RenderGraphCompileResult m_renderPassCompileScratch;
    Rendering::RenderSceneSnapshot m_frameGraphSnapshot;
    Rendering::Dx12GraphTransientPool* m_frameGraphRenderGraph = nullptr;
    bool m_frameGraphFinalized = false;
};

enum class RuntimeFrameResourcePass : uint8_t
{
    Sky,
    FullscreenQuad,
    SceneTarget,
    Volumetric,
    Tonemap,
};

// Invariant: SkyPass is required by the ordinary cube-map path, while the
// other resource owners exist only for the cinematic post chain. Keeping this
// decision beside RuntimeRenderer lets a focused test pin pass selection;
// Automation and graphics stress exercise the production wiring.
constexpr bool RuntimeFrameResourcePassRequired( RuntimeFrameResourcePass pass, bool cinematicEnabled )
{
    switch ( pass )
    {
    case RuntimeFrameResourcePass::Sky:
        return true;
    case RuntimeFrameResourcePass::FullscreenQuad:
    case RuntimeFrameResourcePass::SceneTarget:
    case RuntimeFrameResourcePass::Volumetric:
    case RuntimeFrameResourcePass::Tonemap:
        return cinematicEnabled;
    }

    return false;
}
} // namespace Runtime
} // namespace SkullbonezCore
