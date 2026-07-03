/*
File: SkullbonezSource/Rendering/RenderSceneView.h
Purpose:
  Defines the renderer-facing scene adapter consumed by runtime render passes.

Mental model:
  GameModelCollection remains the compatibility owner during the store
  migration, but render passes talk to this view instead of reaching back into
  the runtime model container.

Glossary:
  Scene view: Renderer-facing adapter for model draws, shadow casters, DXR
  transforms, and debug-scene drawing.
  DXR (DirectX Raytracing): DX12 raytracing path used for water reflections.

Invariants:
  - Render passes may consume this interface only for scene rendering data and
    renderer/debug draw calls.
  - The interface stays in engine terms; it exposes no D3D12 objects or backend
    state.

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
namespace Basics
{
struct CinematicRenderConfig;
}

namespace Assets
{
class AssetSystem;
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

namespace Rendering
{
class IRenderResourceFactory;

class IRenderSceneView
{
  public:
    virtual ~IRenderSceneView() = default;

    virtual int GetRenderModelCount() const = 0;
    virtual int CopyDxrModelMatrices( float* outMatrixFloats, int maxModelCount ) = 0;

    virtual void RenderModels( const Math::Transformation::Matrix4& view,
                               const Math::Transformation::Matrix4& proj,
                               const float lightPos[4],
                               const Basics::CinematicRenderConfig* cinematic = nullptr,
                               const ShadowFrameData* shadow = nullptr,
                               float materialAlpha = 1.0f,
                               const std::vector<uint8_t>* modelMask = nullptr,
                               bool drawMaskedModels = true ) = 0;
    virtual bool GetObjectShadowBounds( const Math::Vector::Vector3& focus,
                                        float maxDistance,
                                        Math::Vector::Vector3& outCenter,
                                        float& outRadius,
                                        float& outHeightRange ) = 0;
    virtual void BuildShadowCasterBatches( ShadowCasterBatches& outBatches ) = 0;
    virtual void RenderShadowCasterBatches( const ShadowCasterBatches& batches,
                                            const Math::Transformation::Matrix4& view,
                                            const Math::Transformation::Matrix4& proj,
                                            const Basics::CinematicRenderConfig* cinematic = nullptr ) = 0;
    virtual void RenderShadowCasters( const Math::Transformation::Matrix4& view,
                                      const Math::Transformation::Matrix4& proj,
                                      const Basics::CinematicRenderConfig* cinematic = nullptr ) = 0;
    virtual void RenderCollisionStateSolids( Physics::CollisionVisualizer& visualizer,
                                             Assets::AssetSystem& assets,
                                             IRenderResourceFactory& renderResources,
                                             const Math::Transformation::Matrix4& view,
                                             const Math::Transformation::Matrix4& proj,
                                             const float lightPos[4],
                                             float alphaOverride ) = 0;
    virtual void RenderPhysicsDebug( Physics::PhysicsDebugVisualizer& visualizer,
                                     const Math::Transformation::Matrix4& viewProjection,
                                     Geometry::Terrain* terrain ) = 0;
    virtual void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj ) = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
