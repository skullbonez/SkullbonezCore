/*
File: SkullbonezSource/Rendering/GameModelRenderer.h
Purpose:
  Converts GameModel data into backend draw calls for normal and shadow rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Asset system: Runtime-owned registry borrowed so render helpers can resolve
  shader source without active-global lookup.
  Command/resource/diagnostic facets: Borrowed renderer capabilities forwarded
  to RenderHelper and trace scopes so model draws never reacquire the global
  backend.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Methods translate model collections into render work; they do not own model
    storage or backend lifetime.
  - Shadow batch structs are CPU-side preparation data and must be submitted
    through backend-facing helpers.
  - Renderer capability references are call-scoped; cached helper handles remain
    opaque backend ids and are reset by backend lifecycle code.

Related:
  - SkullbonezSource/Rendering/GameModelRenderer.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Matrix4.h"
#include "Shadow.h"
#include "../Maths/Vector3.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
} // namespace Assets

namespace Basics
{
struct CinematicRenderConfig;
struct OrdinaryRenderConfig;
struct RuntimeRenderFlags;
}
namespace Threading
{
class WorkerPool;
}
namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderResourceFactory;
} // namespace Rendering

namespace GameObjects
{
class GameModelCollection;

class GameModelRenderer
{
  public:
    static void RenderModels( Rendering::IRenderCommandContext& renderCommands,
                              Rendering::IRenderResourceFactory& renderResources,
                              Rendering::IRenderDiagnostics& renderDiagnostics,
                              const Assets::AssetSystem& assets,
                              GameModelCollection& collection,
                              const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const float lightPos[4],
                              const Basics::RuntimeRenderFlags& runtimeRender,
                              const Basics::OrdinaryRenderConfig& ordinaryRender,
                              const Basics::CinematicRenderConfig* cinematic,
                              const Rendering::ShadowFrameData* shadow,
                              float materialAlpha,
                              const std::vector<uint8_t>* modelMask = nullptr,
                              bool drawMaskedModels = true );
    static void BuildShadowCasterBatches( GameModelCollection& collection,
                                          Rendering::ShadowCasterBatches& outBatches,
                                          bool shadowParallelPrep,
                                          Threading::WorkerPool& workerPool );
    static void SubmitShadowCasterBatches( Rendering::IRenderCommandContext& renderCommands,
                                           Rendering::IRenderResourceFactory& renderResources,
                                           Rendering::IRenderDiagnostics& renderDiagnostics,
                                           const Assets::AssetSystem& assets,
                                           const Rendering::ShadowCasterBatches& batches,
                                           const Math::Transformation::Matrix4& view,
                                           const Math::Transformation::Matrix4& proj,
                                           const Basics::CinematicRenderConfig* cinematic );
    static void RenderShadowCasters( Rendering::IRenderCommandContext& renderCommands,
                                     Rendering::IRenderResourceFactory& renderResources,
                                     Rendering::IRenderDiagnostics& renderDiagnostics,
                                     const Assets::AssetSystem& assets,
                                     GameModelCollection& collection,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     bool shadowParallelPrep,
                                     Threading::WorkerPool& workerPool,
                                     const Basics::CinematicRenderConfig* cinematic );
    static bool GetObjectShadowBounds( GameModelCollection& collection,
                                       const Math::Vector::Vector3& focus,
                                       float maxDistance,
                                       bool shadowParallelPrep,
                                       Threading::WorkerPool& workerPool,
                                       Math::Vector::Vector3& outCenter,
                                       float& outRadius,
                                       float& outHeightRange );
    static void ResetRenderResources();
};
} // namespace GameObjects
} // namespace SkullbonezCore
