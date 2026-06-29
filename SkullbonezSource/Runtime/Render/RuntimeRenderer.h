/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.h
Purpose:
  Declares the runtime renderer owner for ordered render passes.

Mental model:
  RuntimeRenderer is the Phase 2 ownership shell around the existing pass graph.
  It owns pass objects and the frame pass order, while the passes borrow named
  services through RuntimeRenderHost.

Glossary:
  RuntimeRenderer: Owner of pass instances and the frame pass order.
  Pass order: The stable sequence of sky, shadows, reflection, objects, terrain,
  water, post effects, and UI/text.
  Resource context: Creation/rebuild-only view of the renderer factory and
  resize-sensitive dimensions.
  Backend-owned resource: GPU object that must be released before backend
  teardown.

Invariants:
  - RuntimeRenderer owns pass instances; Run owns one RuntimeRenderer.
  - RenderFrame preserves the existing pass order and frame graph snapshot.
  - Backend resource release keeps consumer passes ahead of producer passes.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include "RuntimeRenderHost.h"
#include "RuntimeRenderInputs.h"
#include "RuntimeRenderPasses.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
class RuntimeRenderer
{
  public:
    explicit RuntimeRenderer( RuntimeRenderHost& host );

    void EnsureFrameResources( const RenderResourceContext& resources );
    void RenderFrame( const RuntimeRenderInputs& renderInputs );
    void ReleaseBackendOwnedResources( Rendering::IRenderResourceFactory* renderResources );

    void EnsureUiTextResources();
    bool ShouldRenderUiText() const;
    void SetUiTextRayTracingCapability( Rendering::IRenderRayTracing* renderRayTracing );
    void RenderUiText( Rendering::IRenderDiagnostics& renderDiagnostics, double dSecondsPerFrame );

  private:
    struct CinematicPostGraphResult
    {
        bool volumetricReady = false;         // Volumetric target was produced and can be sampled by tonemap.
        bool volumetricCallbackOwned = false; // Volumetric command recording ran through RenderGraph callback execution.
        bool tonemapCallbackOwned = false;    // Tonemap command recording ran through RenderGraph callback execution.
        uint32_t volumetricTextureHandle = 0; // Renderer texture handle resolved from the graph-owned transient SRV.
        uint32_t volumetricWidth = 0;         // Materialized graph transient width for diagnostics.
        uint32_t volumetricHeight = 0;        // Materialized graph transient height for diagnostics.
    };
    struct GraphPassResult
    {
        bool rendered = false;                // Pass body produced visible output.
        bool callbackOwned = false;           // Pass scheduling ran through RenderGraph callback execution.
    };
    struct ShadowGraphResult
    {
        ShadowPassOutput output;              // Shadow maps produced by the callback-owned shadow pass.
        bool callbackOwned = false;           // Shadow pass scheduling ran through RenderGraph callback execution.
    };
    struct ReflectionGraphResult
    {
        ReflectionPassOutput output;          // Reflection texture produced by the callback-owned reflection pass.
        bool callbackOwned = false;           // Reflection pass scheduling ran through RenderGraph callback execution.
    };

    RenderFrameContext BuildRenderFrameContext( const RuntimeRenderInputs& renderInputs,
                                                bool cinematicRender,
                                                const CinematicRenderConfig& renderConfig ) const;
    RenderResourceContext BuildRenderResourceContext( const RuntimeRenderInputs& renderInputs,
                                                      bool cinematicRender ) const;
    ShadowGraphResult ExecuteShadowThroughRenderGraph( const RenderFrameContext& frame,
                                                       const CinematicRenderConfig* activeShadowConfig );
    bool ExecuteSkyboxThroughRenderGraph( const RenderFrameContext& frame );
    ReflectionGraphResult ExecuteReflectionThroughRenderGraph( const RenderFrameContext& frame,
                                                               const CinematicRenderConfig* activeCinematic,
                                                               const Rendering::ShadowFrameData* objectShadow,
                                                               bool collisionStateColorsVisible,
                                                               bool debugTransparentBodyPass,
                                                               float collisionVisualizerAlphaOverride,
                                                               float bodyAlpha );
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
                                           const Rendering::ShadowFrameData* terrainShadow );
    bool ExecuteWaterThroughRenderGraph( const RenderFrameContext& frame,
                                         const ReflectionPassOutput& reflection,
                                         bool useCinematicTarget,
                                         const CinematicRenderConfig* activeCinematic,
                                         bool waterHidden,
                                         bool flatWater,
                                         bool noReflection,
                                         bool freezeTime,
                                         float frozenTime );
    GraphPassResult ExecuteTornadoVisualThroughRenderGraph( const RenderFrameContext& frame, bool useCinematicTarget );
    bool ExecuteReplayGhostsThroughRenderGraph( const RenderFrameContext& frame,
                                                bool useCinematicTarget,
                                                const CinematicRenderConfig* activeCinematic,
                                                const Rendering::ShadowFrameData* objectShadow );
    bool ExecuteDebugOverlayThroughRenderGraph( const RenderFrameContext& frame, bool useCinematicTarget );
    CinematicPostGraphResult ExecuteCinematicPostThroughRenderGraph( const RenderFrameContext& frame );
    bool ExecuteUiTextThroughRenderGraph( Rendering::IRenderDiagnostics& renderDiagnostics,
                                          Rendering::IRenderRayTracing* renderRayTracing,
                                          double secondsPerFrame );

    RuntimeRenderHost& m_host;
    FullscreenQuadPass m_fullscreenQuadPass;  // Shared full-screen vertex buffer pass used by sky/post effects.
    SkyPass m_skyPass;                        // Background sky pass, reused by reflection and scene target passes.
    SceneTargetPass m_sceneTargetPass;        // Cinematic HDR scene-target begin/release pass.
    ShadowPass m_shadowPass;                  // Terrain/object shadow-map producer pass.
    ReflectionPass m_reflectionPass;          // Water reflection texture producer pass.
    ObjectPass m_objectPass;                  // Production body and collision-solid pass.
    TerrainPass m_terrainPass;                // Terrain material/shadow receiver pass.
    WaterPass m_waterPass;                    // Calm/ocean water pass.
    TornadoVisualPass m_tornadoVisualPass;    // Sparse alpha tornado shell/dust pass.
    DebugOverlayPass m_debugOverlayPass;      // Broadphase and physics debug overlay pass.
    VolumetricPass m_volumetricPass;          // Half-resolution cinematic light-shaft pass.
    TonemapPass m_tonemapPass;                // HDR-to-backbuffer resolve pass.
    UiTextPass m_uiTextPass;                  // HUD/UI/text pass.
    // Lifetime: borrowed only for the next UI pass after world rendering, then
    // refreshed or cleared before backend release and text-only frames.
    Rendering::IRenderRayTracing* m_uiTextRayTracing = nullptr;
};
} // namespace Basics
} // namespace SkullbonezCore
