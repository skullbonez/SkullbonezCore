/*
File: SkullbonezSource/Rendering/RenderSceneSnapshot.h
Purpose:
  Captures immutable renderer-facing facts about one executed scene frame.

Mental model:
  Runtime decides what happened this frame, then hands the renderer a compact
  snapshot so render graph diagnostics and future pass scheduling do not reach
  back through live runtime state.

Glossary:
  Snapshot: Value copy of frame facts, safe to read after the runtime pass code
  has moved on.
  DXR (DirectX Raytracing): DX12 raytracing path used for water reflections.
  HDR (High Dynamic Range): Floating-point scene target used before tonemap.

Invariants:
  - This struct stores values only. Do not add borrowed pointers to runtime,
    model collection, pass resources, or backend objects.
  - Fields describe executed frame outcomes, not requested configuration. A
    failed resource allocation should be reflected in these booleans.

Related:
  - SkullbonezSource/Rendering/RenderPipeline.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Rendering
{

struct RenderSceneSnapshot
{
    bool cinematicRender = false;            // True when cinematic rendering was requested for this frame.
    bool useCinematicTarget = false;         // True when HDR scene color/depth targets were actually used.
    bool terrainShadowValid = false;         // Terrain shadow map was produced and available to receivers.
    bool objectShadowValid = false;          // Object shadow map was produced and available to receivers.
    bool reflectionUsedDxr = false;          // Reflection came from the DXR dispatch instead of the raster target.
    bool objectOpaquePass = false;           // Opaque body pass executed before terrain/water.
    bool objectTransparentPass = false;      // Transparent body pass executed after water.
    bool terrainPassRendered = false;        // Terrain pass actually drew terrain this frame.
    bool waterPassRendered = false;          // Water pass actually drew fluid this frame.
    bool waterSamplesReflection = false;     // Water sampled a non-zero reflection texture handle.
    bool sceneTargetCallbackOwned = false;   // Cinematic scene-target begin ran through the executable render graph.
    bool tornadoVisualRendered = false;      // Sparse tornado production visual drew after water with depth write disabled.
    bool tornadoVisualCallbackOwned = false; // Tornado visual scheduling ran through the executable render graph.
    bool debugOverlayCallbackOwned = false;  // Debug overlay scheduling ran through the executable render graph.
    bool volumetricCallbackOwned = false;    // Volumetric command recording ran through the executable render graph.
    bool volumetricReady = false;            // Volumetric light target was produced for tonemap.
    bool tonemapCallbackOwned = false;       // Tonemap command recording ran through the executable render graph.
};

} // namespace Rendering
} // namespace SkullbonezCore
