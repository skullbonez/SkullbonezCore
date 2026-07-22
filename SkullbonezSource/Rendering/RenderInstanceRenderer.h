/*
File: SkullbonezSource/Rendering/RenderInstanceRenderer.h
Purpose:
  Converts prepared render-instance records into backend draw calls.

Summary:
  RenderInstanceRenderer.h converts prepared render-instance records into backend
  draw calls. As a public header, keep edits anchored on render submission and
  resource lifetime and on the glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Methods consume prepared render/collider stores; they do not own scene
    model storage or backend lifetime.
  - Shadow batch structs are CPU-side preparation data and must be submitted
    through backend-facing helpers.

Related:
  - SkullbonezSource/Rendering/RenderInstanceRenderer.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Matrix4.h"
#include "DX12/Dx12Diagnostics.h"
#include "Shadow.h"
#include "../Maths/Vector3.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
struct CinematicRenderConfig;
} // namespace Core
namespace Physics
{
class ColliderStore;
}

namespace Rendering
{
struct PrimitiveRenderContext;
class RenderInstanceStore;
} // namespace Rendering

namespace Threading
{
class WorkerPool;
}

namespace Rendering
{
class RenderInstanceRenderer
{
  public:
    static void RenderModels( const Rendering::PrimitiveRenderContext& primitiveContext,
                              const Rendering::RenderInstanceStore& renderStore,
                              const Physics::ColliderStore& colliderStore,
                              bool renderCollisionVolumes,
                              const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const float lightPos[4],
                              const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                              const Rendering::ShadowFrameData* shadow,
                              float materialAlpha,
                              const std::vector<uint8_t>* modelMask = nullptr,
                              bool drawMaskedModels = true,
                              Rendering::RenderVisibilityView visibilityView = Rendering::RenderVisibilityView::Main );
    static void BuildShadowCasterBatches( Core::Profiler* profiler,
                                          const Rendering::RenderInstanceStore& renderStore,
                                          const Physics::ColliderStore& colliderStore,
                                          Threading::WorkerPool* workerPool,
                                          bool useShadowParallelPrep,
                                          Rendering::ShadowCasterBatches& outBatches );
    static void SubmitShadowCasterBatches( Core::Profiler* profiler,
                                           const Rendering::PrimitiveRenderContext& primitiveContext,
                                           const Rendering::ShadowCasterBatches& batches,
                                           const Math::Transformation::Matrix4& view,
                                           const Math::Transformation::Matrix4& proj,
                                           const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                           Rendering::RenderVisibilityView visibilityView );
    static void RenderShadowCasters( Core::Profiler* profiler,
                                     const Rendering::PrimitiveRenderContext& primitiveContext,
                                     const Rendering::RenderInstanceStore& renderStore,
                                     const Physics::ColliderStore& colliderStore,
                                     Threading::WorkerPool* workerPool,
                                     bool useShadowParallelPrep,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                     Rendering::RenderVisibilityView visibilityView );
    static bool GetObjectShadowBounds( Core::Profiler* profiler,
                                       const Rendering::RenderInstanceStore& renderStore,
                                       Threading::WorkerPool* workerPool,
                                       bool useShadowParallelPrep,
                                       const Math::Vector::Vector3& focus,
                                       float maxDistance,
                                       Math::Vector::Vector3& outCenter,
                                       float& outRadius,
                                       float& outHeightRange );
};
} // namespace Rendering
} // namespace SkullbonezCore
