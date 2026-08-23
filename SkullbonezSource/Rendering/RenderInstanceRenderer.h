/*
File: SkullbonezSource/Rendering/RenderInstanceRenderer.h
Purpose:
  Converts prepared render-instance records into backend draw calls.

Summary:
  RenderInstanceRenderer consumes prepared render-instance and collider rows to
  build normal and shadow submissions; scene stores and backend lifetime remain
  with their concrete owners.

Invariants:
  - Methods consume prepared render/collider stores; they do not own scene
    model storage or backend lifetime.
  - Shadow batch structs are CPU-side preparation data and must be submitted
    through backend-facing helpers.

Related:
  - SkullbonezSource/Rendering/RenderInstanceRenderer.cpp
  - Agentic/Reference/engine-glossary.md
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
struct OrdinaryRenderConfig;
} // namespace Core
namespace Physics
{
class ColliderStore;
}

namespace Rendering
{
class PrimitiveBatchRenderer;
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

    // Submits the main view. The optional mask chooses either the marked or
    // unmarked model rows; all borrows end before this call returns.
    static void RenderModels( Rendering::PrimitiveBatchRenderer& primitiveRenderer,
                              Rendering::Dx12Diagnostics& renderDiagnostics,
                              const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                              const char* shaderBaseName,
                              const Rendering::RenderInstanceStore& renderStore, const Physics::ColliderStore& colliderStore,
                              bool renderCollisionVolumes, const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& projection, const float ( &lightPosition )[4],
                              const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                              const Rendering::ShadowFrameData* shadow, float materialAlpha,
                              const std::vector<uint8_t>* modelMask, bool drawMaskedModels );

    // Submits the mirrored view. Reflection clipping is structural, so callers
    // cannot accidentally select main-view visibility or supply a model mask.
    static void RenderReflectionModels( Rendering::PrimitiveBatchRenderer& primitiveRenderer,
                                        Rendering::Dx12Diagnostics& renderDiagnostics,
                                        const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                                        const char* shaderBaseName,
                                        const Rendering::RenderInstanceStore& renderStore,
                                        const Physics::ColliderStore& colliderStore, bool renderCollisionVolumes,
                                        const Math::Transformation::Matrix4& view,
                                        const Math::Transformation::Matrix4& projection, const float ( &lightPosition )[4],
                                        const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                        const Rendering::ShadowFrameData* shadow, float materialAlpha );
    static void BuildShadowCasterBatches( Core::Profiler* profiler, const Rendering::RenderInstanceStore& renderStore,
                                          const Physics::ColliderStore& colliderStore, Threading::WorkerPool* workerPool,
                                          bool useShadowParallelPrep, Rendering::ShadowCasterBatches& outBatches );
    static void SubmitShadowCasterBatches( Core::Profiler* profiler,
                                           Rendering::PrimitiveBatchRenderer& primitiveRenderer,
                                           Rendering::Dx12Diagnostics& renderDiagnostics,
                                           const char* shaderBaseName,
                                           const Rendering::ShadowCasterBatches& batches,
                                           const Math::Transformation::Matrix4& view,
                                           const Math::Transformation::Matrix4& proj,
                                           const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                           Rendering::RenderVisibilityView visibilityView );
    static void RenderShadowCasters( Core::Profiler* profiler, Rendering::PrimitiveBatchRenderer& primitiveRenderer,
                                     Rendering::Dx12Diagnostics& renderDiagnostics, const char* shaderBaseName,
                                     const Rendering::RenderInstanceStore& renderStore,
                                     const Physics::ColliderStore& colliderStore, Threading::WorkerPool* workerPool,
                                     bool useShadowParallelPrep, const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                     Rendering::RenderVisibilityView visibilityView );
    static bool GetObjectShadowBounds( Core::Profiler* profiler, const Rendering::RenderInstanceStore& renderStore,
                                       Threading::WorkerPool* workerPool, bool useShadowParallelPrep,
                                       const Math::Vector::Vector3& focus, float maxDistance,
                                       Math::Vector::Vector3& outCenter, float& outRadius, float& outHeightRange );
};
} // namespace Rendering
} // namespace SkullbonezCore
