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
    RenderInstanceRenderer( Rendering::PrimitiveBatchRenderer& primitiveRenderer,
                            Rendering::Dx12Diagnostics& renderDiagnostics,
                            const SkullbonezCore::Core::OrdinaryRenderConfig& lighting,
                            const Rendering::RenderInstanceStore& renderStore, const Physics::ColliderStore& colliderStore,
                            Threading::WorkerPool* workerPool, bool useShadowParallelPrep, bool renderCollisionVolumes )
        : m_primitiveRenderer( primitiveRenderer ), m_renderDiagnostics( renderDiagnostics ), m_lighting( lighting ),
          m_renderStore( renderStore ), m_colliderStore( colliderStore ), m_workerPool( workerPool ),
          m_useShadowParallelPrep( useShadowParallelPrep ), m_renderCollisionVolumes( renderCollisionVolumes )
    {
    }

    // Submits the main view. The optional mask chooses either the marked or
    // unmarked model rows; all borrows end before this call returns.
    void RenderModels( const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                       const Math::Transformation::Matrix4& projection, const float ( &lightPosition )[4],
                       const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                       const Rendering::ShadowFrameData* shadow, float materialAlpha, const std::vector<uint8_t>* modelMask,
                       bool drawMaskedModels );

    // Submits the mirrored view. Reflection clipping is structural, so callers
    // cannot accidentally select main-view visibility or supply a model mask.
    void RenderReflectionModels( const char* shaderBaseName, const Math::Transformation::Matrix4& view,
                                 const Math::Transformation::Matrix4& projection, const float ( &lightPosition )[4],
                                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                 const Rendering::ShadowFrameData* shadow, float materialAlpha );
    void BuildShadowCasterBatches( Core::Profiler* profiler, Rendering::ShadowCasterBatches& outBatches );
    void SubmitShadowCasterBatches( Core::Profiler* profiler, const char* shaderBaseName,
                                    const Rendering::ShadowCasterBatches& batches, const Math::Transformation::Matrix4& view,
                                    const Math::Transformation::Matrix4& proj,
                                    const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                    Rendering::RenderVisibilityView visibilityView );
    bool GetObjectShadowBounds( Core::Profiler* profiler, const Math::Vector::Vector3& focus, float maxDistance,
                                Math::Vector::Vector3& outCenter, float& outRadius, float& outHeightRange );

  private:
    void RenderModelsForView( Rendering::RenderVisibilityView visibilityView, const char* shaderBaseName,
                              const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection,
                              const float ( &lightPosition )[4],
                              const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                              const Rendering::ShadowFrameData* shadow, float materialAlpha,
                              const std::vector<uint8_t>* modelMask, bool drawMaskedModels );

    Rendering::PrimitiveBatchRenderer& m_primitiveRenderer;
    Rendering::Dx12Diagnostics& m_renderDiagnostics;
    const SkullbonezCore::Core::OrdinaryRenderConfig& m_lighting;
    const Rendering::RenderInstanceStore& m_renderStore;
    const Physics::ColliderStore& m_colliderStore;
    Threading::WorkerPool* m_workerPool = nullptr;
    bool m_useShadowParallelPrep = false;
    bool m_renderCollisionVolumes = false;
};
} // namespace Rendering
} // namespace SkullbonezCore
