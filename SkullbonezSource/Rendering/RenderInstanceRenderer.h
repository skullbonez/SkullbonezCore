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
// Lifetime: these three borrows describe one synchronous camera/light draw
// view. The light parameter keeps its four-component extent at the type
// boundary, so callers cannot pair matrices with a short scalar buffer.
class RenderViewValues
{
  public:
    RenderViewValues( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection,
                      const float ( &lightPosition )[4] ) noexcept
        : m_view( view ), m_projection( projection ), m_lightPosition( lightPosition )
    {
    }

    const Math::Transformation::Matrix4& View() const noexcept
    {
        return m_view;
    }
    const Math::Transformation::Matrix4& Projection() const noexcept
    {
        return m_projection;
    }
    const float ( &LightPosition() const noexcept )[4]
    {
        return m_lightPosition;
    }

  private:
    const Math::Transformation::Matrix4& m_view;
    const Math::Transformation::Matrix4& m_projection;
    const float ( &m_lightPosition )[4];
};

// Encodes the three valid row-selection states. Missing/out-of-range mask rows
// retain the historical unmasked fallback without a second Boolean that can
// contradict the mask's intended polarity.
class RenderModelSelection
{
  public:
    static RenderModelSelection All() noexcept;
    static RenderModelSelection Marked( const std::vector<uint8_t>& mask ) noexcept;
    static RenderModelSelection Unmarked( const std::vector<uint8_t>& mask ) noexcept;
    bool Includes( int modelIndex ) const noexcept;

  private:
    RenderModelSelection( const std::vector<uint8_t>* mask, bool includeMarked ) noexcept
        : m_mask( mask ), m_includeMarked( includeMarked )
    {
    }

    const std::vector<uint8_t>* m_mask = nullptr;
    bool m_includeMarked = true;
};

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
    void RenderModels( const char* shaderBaseName, const RenderViewValues& renderView,
                       const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                       const Rendering::ShadowFrameData* shadow, float materialAlpha, RenderModelSelection selection );

    // Submits the mirrored view. Reflection clipping is structural, so callers
    // cannot accidentally select main-view visibility or supply a model mask.
    void RenderReflectionModels( const char* shaderBaseName, const RenderViewValues& renderView,
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
                              const RenderViewValues& renderView,
                              const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                              const Rendering::ShadowFrameData* shadow, float materialAlpha,
                              RenderModelSelection selection );

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
