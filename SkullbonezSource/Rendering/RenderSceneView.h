/*
File: SkullbonezSource/Rendering/RenderSceneView.h
Purpose:
  Defines the renderer-facing scene adapter consumed by runtime render passes.

Mental model:
  GameModelCollection remains the compatibility owner during the store
  migration, but render passes talk to this view instead of reaching back into
  the runtime model container.

Glossary:
  Asset system: Runtime-owned registry borrowed by render scene helpers that
  lazily create shaders.
  Scene view: Renderer-facing adapter for model draws, shadow casters, DXR
  transforms, and debug-scene drawing.
  Command context: Borrowed per-frame render interface for state changes and
    immediate debug draw submission.
  Resource factory: Borrowed backend lifetime interface for helper-owned
    primitive caches that are still lazily created by model draw setup.
  Render diagnostics: Per-frame tracing/capability surface used by scene draws
    without reacquiring the process-global backend.
  DXR (DirectX Raytracing): DX12 raytracing path used for water reflections.

Invariants:
  - Render passes may consume this interface only for scene rendering data and
    renderer/debug draw calls.
  - The interface stays in engine terms; it exposes no D3D12 objects or backend
    state.
  - Resource-factory and diagnostic borrows must remain tied to the same
    frame/backend as the command context because helper caches and trace scopes
    store backend-owned handles/state.

Related:
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - SkullbonezSource/Runtime/RunPasses.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"
#include "Shadow.h"

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

namespace Geometry
{
class Terrain;
}

namespace Physics
{
class CollisionVisualizer;
class PhysicsDebugVisualizer;
} // namespace Physics
namespace Threading
{
class WorkerPool;
}

namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderResourceFactory;

class IRenderSceneView
{
  public:
    virtual ~IRenderSceneView() = default;

    virtual int GetRenderModelCount() const = 0;
    virtual int CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount ) = 0;

    virtual void RenderModels( IRenderCommandContext& renderCommands,
                               IRenderResourceFactory& renderResources,
                               IRenderDiagnostics& renderDiagnostics,
                               const Assets::AssetSystem& assets,
                               const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj,
                               const float lightPos[4],
                               const Basics::RuntimeRenderFlags& runtimeRender,
                               const Basics::OrdinaryRenderConfig& ordinaryRender,
                               const Basics::CinematicRenderConfig* cinematic = nullptr,
                               const ShadowFrameData* shadow = nullptr,
                               float materialAlpha = 1.0f,
                               const std::vector<uint8_t>* modelMask = nullptr,
                               bool drawMaskedModels = true ) = 0;
    virtual bool GetObjectShadowBounds( const Math::Vector::Vector3& focus,
                                        float maxDistance,
                                        bool shadowParallelPrep,
                                        Threading::WorkerPool& workerPool,
                                        Math::Vector::Vector3& outCenter,
                                        float& outRadius,
                                        float& outHeightRange ) = 0;
    virtual void BuildShadowCasterBatches( ShadowCasterBatches& outBatches,
                                           bool shadowParallelPrep,
                                           Threading::WorkerPool& workerPool ) = 0;
    virtual void RenderShadowCasterBatches( IRenderCommandContext& renderCommands,
                                            IRenderResourceFactory& renderResources,
                                            IRenderDiagnostics& renderDiagnostics,
                                            const Assets::AssetSystem& assets,
                                            const ShadowCasterBatches& batches,
                                            const Math::Transformation::Matrix4& view,
                                            const Math::Transformation::Matrix4& proj,
                                            const Basics::CinematicRenderConfig* cinematic = nullptr ) = 0;
    virtual void RenderShadowCasters( IRenderCommandContext& renderCommands,
                                      IRenderResourceFactory& renderResources,
                                      IRenderDiagnostics& renderDiagnostics,
                                      const Assets::AssetSystem& assets,
                                      const Math::Transformation::Matrix4& view,
                                      const Math::Transformation::Matrix4& proj,
                                      bool shadowParallelPrep,
                                      Threading::WorkerPool& workerPool,
                                      const Basics::CinematicRenderConfig* cinematic = nullptr ) = 0;
    virtual void RenderCollisionStateSolids( IRenderCommandContext& renderCommands,
                                             Physics::CollisionVisualizer& visualizer,
                                             const Math::Transformation::Matrix4& view,
                                             const Math::Transformation::Matrix4& proj,
                                             const float lightPos[4],
                                             float alphaOverride ) = 0;
    virtual void RenderPhysicsDebug( IRenderCommandContext& renderCommands,
                                     Physics::PhysicsDebugVisualizer& visualizer,
                                     const Math::Transformation::Matrix4& viewProjection,
                                     Geometry::Terrain* terrain ) = 0;
    virtual void RenderTornadoFieldVectors( IRenderCommandContext& renderCommands,
                                            const Math::Transformation::Matrix4& viewProj ) = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
