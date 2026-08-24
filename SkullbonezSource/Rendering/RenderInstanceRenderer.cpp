/*
File: SkullbonezSource/Rendering/RenderInstanceRenderer.cpp
Purpose:
  Converts model-order render snapshots into backend draw calls for normal and
  shadow rendering.

Summary:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts. Draw transforms come from
  RenderInstanceStore and hull geometry comes from ColliderStore so
  physics-owned pose/shape data does not have to be copied back into every
  legacy object record merely for rendering.

Invariants:
  - RenderInstanceRenderer consumes prepared render instances and collider rows; the
    scene owner remains responsible for refreshing those stores before drawing.
  - Shadow caster preparation may run worker-side, but draw submission remains
    on the render thread through the PrimitiveBatchRenderer resource owner.
  - ShadowCasterBatches capacity is reserved before steady rendering; a larger
    model count is a fixed-capacity render invariant failure.

Related:
  - SkullbonezSource/Rendering/RenderInstanceRenderer.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RenderInstanceRenderer.h"

#include "../Core/Config.h"
#include "../Core/FatalError.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"
#include "../Physics/ColliderStore.h"
#include "../Maths/Frustum.h"
#include "PrimitiveBatchRenderer.h"
#include "DX12/Dx12Diagnostics.h"
#include "RenderInstanceStore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Rendering::PrimitiveBatchRenderer;
using SkullbonezCore::Rendering::RenderInstanceRecord;
using SkullbonezCore::Rendering::RenderInstanceShapeKind;
using SkullbonezCore::Rendering::RenderInstanceStore;
using SkullbonezCore::Rendering::RenderMaterial;
using SkullbonezCore::Rendering::RenderMaterialKind;
using SkullbonezCore::Rendering::ShadowCasterBatches;
using SkullbonezCore::Rendering::ShadowCasterInstance;
using SkullbonezCore::Rendering::ShadowCasterStream;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
constexpr int SHADOW_PARALLEL_PREP_MIN_CASTERS = 512;
constexpr bool SHADOW_PARALLEL_PREP_WORKER_ENABLED = true;

struct ShadowCasterStreamCounts
{
    int spheres = 0;
    int boxes = 0;
    int pines = 0;
    int convexHulls = 0;
};

ShadowCasterStream ResolveShadowCasterStream( const RenderInstanceRecord& instance, int modelIndex,
                                              std::span<const ColliderRecord> colliders, const ConvexHullShape*& outHull )
{
    outHull = nullptr;

    // Invariant: one editor visibility bit suppresses every raster path. A
    // hidden main-pass object must not remain as a detached shadow or bound.
    if ( !instance.editorVisible )
    {
        return ShadowCasterStream::None;
    }

    if ( instance.shadowCasterStream != ShadowCasterStream::ConvexHull )
    {
        return instance.shadowCasterStream;
    }

    if ( modelIndex < 0 || static_cast<std::size_t>( modelIndex ) >= colliders.size() )
    {
        return ShadowCasterStream::None;
    }

    const ColliderRecord& collider = colliders[static_cast<std::size_t>( modelIndex )];
    const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &collider.shape );

    if ( !hull )
    {
        return ShadowCasterStream::None;
    }

    outHull = hull;
    return ShadowCasterStream::ConvexHull;
}

void IncrementShadowCasterCount( ShadowCasterStreamCounts& counts, ShadowCasterStream stream )
{
    switch ( stream )
    {
    case ShadowCasterStream::Sphere:
        ++counts.spheres;
        break;
    case ShadowCasterStream::Box:
        ++counts.boxes;
        break;
    case ShadowCasterStream::Pine:
        ++counts.pines;
        break;
    case ShadowCasterStream::ConvexHull:
        ++counts.convexHulls;
        break;
    case ShadowCasterStream::None:
        break;
    }
}

void AppendShadowCasterToBatches( const RenderInstanceRecord& instance, int modelIndex,
                                  std::span<const ColliderRecord> colliders, ShadowCasterBatches& batches )
{
    const ConvexHullShape* hull = nullptr;

    switch ( ResolveShadowCasterStream( instance, modelIndex, colliders, hull ) )
    {
    case ShadowCasterStream::Sphere:
        batches.spheres.push_back( { instance.modelMatrix, instance.boundingRadius } );
        break;
    case ShadowCasterStream::Box:
        batches.boxes.push_back( { instance.modelMatrix, instance.boundingRadius } );
        break;
    case ShadowCasterStream::Pine:
        batches.pines.push_back( { instance.modelMatrix, instance.boundingRadius } );
        break;
    case ShadowCasterStream::ConvexHull:

        // Lifetime: convex hull casters borrow ColliderStore geometry and are
        // submitted during this shadow pass before collider storage refreshes.
        batches.convexHulls.push_back( { hull, { instance.modelMatrix, instance.boundingRadius } } );
        break;
    case ShadowCasterStream::None:
        break;
    }
}

void CountShadowCasterRange( std::span<const RenderInstanceRecord> instances, std::span<const ColliderRecord> colliders,
                             int begin, int end, ShadowCasterStreamCounts& counts )
{
    counts = ShadowCasterStreamCounts();

    for ( int x = begin; x < end; ++x )
    {
        const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];
        const ConvexHullShape* hull = nullptr;
        IncrementShadowCasterCount( counts, ResolveShadowCasterStream( instance, x, colliders, hull ) );
    }
}

void AddShadowCasterCounts( ShadowCasterStreamCounts& total, const ShadowCasterStreamCounts& add )
{
    total.spheres += add.spheres;
    total.boxes += add.boxes;
    total.pines += add.pines;
    total.convexHulls += add.convexHulls;
}

void ResizeShadowCasterBatchesNoAlloc( ShadowCasterBatches& batches, const ShadowCasterStreamCounts& totals )
{
    batches.spheres.resize( static_cast<std::size_t>( totals.spheres ) );
    batches.boxes.resize( static_cast<std::size_t>( totals.boxes ) );
    batches.pines.resize( static_cast<std::size_t>( totals.pines ) );
    batches.convexHulls.resize( static_cast<std::size_t>( totals.convexHulls ) );
}

void FillShadowCasterRange( std::span<const RenderInstanceRecord> instances, std::span<const ColliderRecord> colliders,
                            int begin, int end, const ShadowCasterStreamCounts& offsets, ShadowCasterBatches& batches )
{
    ShadowCasterStreamCounts write = offsets;

    for ( int x = begin; x < end; ++x )
    {
        const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];
        const ConvexHullShape* hull = nullptr;

        switch ( ResolveShadowCasterStream( instance, x, colliders, hull ) )
        {
        case ShadowCasterStream::Sphere:
            batches.spheres[static_cast<std::size_t>( write.spheres++ )] = { instance.modelMatrix, instance.boundingRadius };

            break;
        case ShadowCasterStream::Box:
            batches.boxes[static_cast<std::size_t>( write.boxes++ )] = { instance.modelMatrix, instance.boundingRadius };

            break;
        case ShadowCasterStream::Pine:
            batches.pines[static_cast<std::size_t>( write.pines++ )] = { instance.modelMatrix, instance.boundingRadius };

            break;
        case ShadowCasterStream::ConvexHull:
            batches.convexHulls[static_cast<std::size_t>( write.convexHulls++ )] = { hull,
                                                                                     { instance.modelMatrix,
                                                                                       instance.boundingRadius } };

            break;
        case ShadowCasterStream::None:
            break;
        }
    }
}

bool BuildShadowCasterBatchesWithWorkers( SkullbonezCore::Core::Profiler* profiler,
                                          std::span<const RenderInstanceRecord> instances,
                                          std::span<const ColliderRecord> colliders,
                                          SkullbonezCore::Threading::WorkerPool& workerPool, int modelCount,
                                          ShadowCasterBatches& outBatches )
{
    // Concept: shadow prep uses count/prefix/fill instead of chunk-local
    // vectors. Counting gives each worker a stable output range for every
    // caster stream, so the fill pass can run in parallel without allocating or
    // reordering the serial sphere/box/pine/hull stream order.
    SkullbonezCore::Threading::WorkerChunkRange chunks[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS];
    const int chunkCount = workerPool.BuildChunkRangesNoAlloc( 0, modelCount, SHADOW_PARALLEL_PREP_MIN_CASTERS, chunks,
                                                               SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS );

    if ( chunkCount <= 1 )
    {
        return false;
    }

    ShadowCasterStreamCounts counts[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS] = {};
    workerPool
        .ParallelForChunksNoAlloc( chunks, chunkCount,
                                   [&]( int chunkIndex, int begin, int end )
                                   {
                                       PROFILE_WORKER_SCOPED( profiler,
                                                              "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/"
                                                              "OrderedWorkerCollect/WorkerBuildBatches" );

                                       CountShadowCasterRange( instances, colliders, begin, end, counts[chunkIndex] );
                                   } );

    ShadowCasterStreamCounts offsets[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS] = {};
    ShadowCasterStreamCounts totals;

    for ( int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
    {
        offsets[chunkIndex] = totals;
        AddShadowCasterCounts( totals, counts[chunkIndex] );
    }

    ResizeShadowCasterBatchesNoAlloc( outBatches, totals );

    // Invariant: the output vectors are fully sized before this worker fill.
    // Each chunk writes a disjoint prefix-summed range, so no worker may call a
    // vector growth API or touch another chunk's elements.
    workerPool
        .ParallelForChunksNoAlloc( chunks, chunkCount,
                                   [&]( int chunkIndex, int begin, int end )
                                   {
                                       PROFILE_WORKER_SCOPED( profiler,
                                                              "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/"
                                                              "OrderedWorkerCollect/WorkerFillBatches" );

                                       FillShadowCasterRange( instances, colliders, begin, end, offsets[chunkIndex],
                                                              outBatches );
                                   } );
    return true;
}

RenderMaterial MaterialWithContactHighlights( const RenderInstanceRecord& instance, bool box )
{
    // Why: contact highlights are render-only feedback. They must not mutate
    // the model's stored material or physics release policy.
    RenderMaterial material = instance.material;
    const float hit = instance.isFixed ? instance.fixedContactAlpha : 0.0f;

    if ( hit > 0.0f )
    {
        if ( box && material.textureMode <= 0.5f && material.textureMode >= -0.5f )
        {
            constexpr float fixedBase = 241.0f / 255.0f;
            material.baseColor[0] = fixedBase + ( 1.0f - fixedBase ) * hit;
            material.baseColor[1] = fixedBase * ( 1.0f - hit );
            material.baseColor[2] = fixedBase * ( 1.0f - hit );
            material.kind = RenderMaterialKind::Matte;
            material.textureMode = 1.0f;
        }
        else
        {
            material.baseColor[0] = material.baseColor[0] + ( 1.0f - material.baseColor[0] ) * hit;
            material.baseColor[1] = material.baseColor[1] * ( 1.0f - hit );
            material.baseColor[2] = material.baseColor[2] * ( 1.0f - hit );
        }
    }

    return material;
}
} // namespace


// Concept: one stack-scoped submission owner carries the renderer, diagnostics,
// configuration, and prepared model borrows across every object pass in a frame.
void RenderInstanceRenderer::RenderModelsForView(
    RenderVisibilityView visibilityView, const char* shaderBaseName, const Matrix4& view, const Matrix4& proj,
    const float ( &lightPos )[4], const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
    const ShadowFrameData* shadow, float materialAlpha, const std::vector<uint8_t>* modelMask, bool drawMaskedModels )
{
    const auto instances = m_renderStore.Records();

    if ( instances.empty() )
    {
        return;
    }

    const int modelCount = static_cast<int>( instances.size() );

    // Invariant: the visible-index scratch array mirrors the scene store's
    // compile-time ceiling. Crossing it would corrupt render-thread stack data.
    if ( modelCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Rendering/Visibility", "Render instance count exceeds visibility capacity. count=%d capacity=%d",
                  modelCount, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    int visibleIndices[SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS] = {};

    int visibleCount = 0;

    // Why: the planar reflection has its own mirrored camera volume. Its water
    // half-space removes only instances wholly below the surface; straddlers
    // still reach the shader clip plane for pixel-accurate clipping.
    if ( visibilityView == RenderVisibilityView::Main || visibilityView == RenderVisibilityView::Reflection )
    {
        const SkullbonezCore::Math::Visibility::Frustum
            frustum = SkullbonezCore::Math::Visibility::Frustum::FromViewProjection( view, proj );

        const float* reflectionClipPlane = visibilityView == RenderVisibilityView::Reflection
                                               ? m_primitiveRenderer.GetClipPlane()
                                               : nullptr;

        for ( int index = 0; index < modelCount; ++index )
        {
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( index )];

            if ( !instance.editorVisible )
            {
                continue;
            }

            const Vector3 center( instance.modelMatrix.m[12], instance.modelMatrix.m[13], instance.modelMatrix.m[14] );
            const bool insideFrustum = frustum.IntersectsSphere( center, instance.boundingRadius );
            const bool
                aboveReflectionPlane = !reflectionClipPlane ||
                                       SkullbonezCore::Math::Visibility::Frustum::IntersectsHalfSpace( center,
                                                                                                       instance
                                                                                                           .boundingRadius,
                                                                                                       reflectionClipPlane );

            if ( insideFrustum && aboveReflectionPlane )
            {
                visibleIndices[visibleCount++] = index;
            }
        }
    }
    else
    {
        for ( int index = 0; index < modelCount; ++index )
        {
            if ( instances[static_cast<std::size_t>( index )].editorVisible )
            {
                visibleIndices[visibleCount++] = index;
            }
        }
    }

    const float clampedMaterialAlpha = std::clamp( materialAlpha, 0.0f, 1.0f );
    const bool alphaBlendedPass = m_renderCollisionVolumes || clampedMaterialAlpha < 1.0f;
    const auto shouldDrawModel = [&]( int index ) -> bool
    {
        if ( !modelMask )
        {
            return true;
        }

        if ( index < 0 || static_cast<std::size_t>( index ) >= modelMask->size() )
        {
            return !drawMaskedModels;
        }

        const bool masked = ( *modelMask )[static_cast<std::size_t>( index )] != 0;
        return drawMaskedModels ? masked : !masked;
    };

    int submittedCount = 0;
    const int drawCountBefore = m_renderDiagnostics.GetFrameDrawCallCount();

    {
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "Spheres" );
        auto sphereBatch = m_primitiveRenderer.BeginSphereBatch( m_lighting, shaderBaseName, view, proj, lightPos,
                                                                 alphaBlendedPass, cinematic, shadow,
                                                                 clampedMaterialAlpha );

        for ( int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex )
        {
            const int x = visibleIndices[visibleIndex];

            if ( !shouldDrawModel( x ) )
            {
                continue;
            }

            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];

            if ( instance.shapeKind == RenderInstanceShapeKind::Sphere )
            {
                RenderMaterial material = MaterialWithContactHighlights( instance, false );
                sphereBatch.DrawModel( instance.modelMatrix, material );
                ++submittedCount;
            }
        }
    }

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass, PrimitiveBatchRenderer::PrimitiveBatchScope& batch )
    {
        for ( int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex )
        {
            const int x = visibleIndices[visibleIndex];

            if ( !shouldDrawModel( x ) )
            {
                continue;
            }

            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];

            if ( instance.shapeKind == RenderInstanceShapeKind::Box )
            {
                RenderMaterial material = MaterialWithContactHighlights( instance, true );
                const bool isPineVisual = instance.shadowCasterStream == ShadowCasterStream::Pine;

                if ( isPineVisual )
                {
                    hasPineVisualModels = true;
                }

                if ( isPineVisual != pineVisualPass )
                {
                    continue;
                }

                if ( pineVisualPass )
                {
                    batch.DrawModel( instance.modelMatrix, material );
                }
                else
                {
                    batch.DrawModel( instance.modelMatrix, material );
                }

                ++submittedCount;
            }
        }
    };

    {
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "Boxes" );
        auto boxBatch = m_primitiveRenderer.BeginBoxBatch( m_lighting, shaderBaseName, view, proj, lightPos,
                                                           alphaBlendedPass, cinematic, shadow, clampedMaterialAlpha );

        appendBoxLikeModels( false, boxBatch );
    }

    if ( hasPineVisualModels )
    {
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "Pines" );
        auto pineBatch = m_primitiveRenderer.BeginPineBatch( m_lighting, shaderBaseName, view, proj, lightPos,
                                                             alphaBlendedPass, cinematic, shadow,
                                                             clampedMaterialAlpha );

        appendBoxLikeModels( true, pineBatch );
    }

    {
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "ConvexHulls" );
        std::span<const ColliderRecord> colliders;
        m_primitiveRenderer.BeginConvexHullBatch( m_lighting, shaderBaseName, view, proj, lightPos, alphaBlendedPass,
                                                  cinematic, shadow, clampedMaterialAlpha );

        for ( int visibleIndex = 0; visibleIndex < visibleCount; ++visibleIndex )
        {
            const int x = visibleIndices[visibleIndex];

            if ( !shouldDrawModel( x ) )
            {
                continue;
            }

            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];

            if ( instance.shapeKind != RenderInstanceShapeKind::ConvexHull )
            {
                continue;
            }

            if ( colliders.empty() )
            {
                colliders = m_colliderStore.Records();
            }

            if ( static_cast<std::size_t>( x ) >= colliders.size() )
            {
                continue;
            }

            const ColliderRecord& collider = colliders[static_cast<std::size_t>( x )];
            const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &collider.shape );

            if ( !hull )
            {
                continue;
            }

            const RenderMaterial material = MaterialWithContactHighlights( instance, false );
            m_primitiveRenderer.DrawConvexHullModel( *hull, instance.modelMatrix, material );

            ++submittedCount;
        }

        m_primitiveRenderer.EndConvexHullBatch();
    }
    const int drawCountAfter = m_renderDiagnostics.GetFrameDrawCallCount();
    m_renderDiagnostics.RecordVisibility( visibilityView, modelCount, submittedCount, modelCount - visibleCount,
                                          (std::max)( 0, drawCountAfter - drawCountBefore ) );
}


void RenderInstanceRenderer::RenderModels( const char* shaderBaseName, const Matrix4& view, const Matrix4& projection,
                                           const float ( &lightPosition )[4],
                                           const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                           const ShadowFrameData* shadow, float materialAlpha,
                                           const std::vector<uint8_t>* modelMask, bool drawMaskedModels )
{
    RenderModelsForView( RenderVisibilityView::Main, shaderBaseName, view, projection, lightPosition, cinematic, shadow,
                         materialAlpha, modelMask, drawMaskedModels );
}


void RenderInstanceRenderer::RenderReflectionModels( const char* shaderBaseName, const Matrix4& view,
                                                     const Matrix4& projection, const float ( &lightPosition )[4],
                                                     const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                                     const ShadowFrameData* shadow, float materialAlpha )
{
    RenderModelsForView( RenderVisibilityView::Reflection, shaderBaseName, view, projection, lightPosition, cinematic,
                         shadow, materialAlpha, nullptr, true );
}


void RenderInstanceRenderer::BuildShadowCasterBatches( SkullbonezCore::Core::Profiler* profiler,
                                                       ShadowCasterBatches& outBatches )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches" );

    const auto instances = m_renderStore.Records();
    outBatches.Clear();

    if ( instances.empty() )
    {
        return;
    }

    const int modelCount = static_cast<int>( instances.size() );
    assert( outBatches.HasCapacityForModelCount( modelCount ) );

    if ( !outBatches.HasCapacityForModelCount( modelCount ) )
    {
        SB_FATAL( "RenderInstanceRenderer",
                  "Shadow caster batch reserve exhausted. modelCount=%d sphereCapacity=%zu boxCapacity=%zu "
                  "pineCapacity=%zu hullCapacity=%zu",
                  modelCount, outBatches.spheres.capacity(), outBatches.boxes.capacity(), outBatches.pines.capacity(),
                  outBatches.convexHulls.capacity() );
    }

    const auto colliders = m_colliderStore.Records();
    auto appendRange = [&]( int begin, int end, ShadowCasterBatches& batches )
    {
        for ( int x = begin; x < end; ++x )
        {
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];

            AppendShadowCasterToBatches( instance, x, colliders, batches );
        }
    };

    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && m_useShadowParallelPrep && m_workerPool &&
         modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/OrderedWorkerCollect" );

        if ( BuildShadowCasterBatchesWithWorkers( profiler, instances, colliders, *m_workerPool, modelCount,
                                                  outBatches ) )
        {
            return;
        }
    }

    appendRange( 0, modelCount, outBatches );
}


void RenderInstanceRenderer::SubmitShadowCasterBatches( SkullbonezCore::Core::Profiler*,
                                                        const char* shaderBaseName,
                                                        const ShadowCasterBatches& batches, const Matrix4& view,
                                                        const Matrix4& proj,
                                                        const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                                        Rendering::RenderVisibilityView visibilityView )
{
    if ( batches.Empty() )
    {
        return;
    }

    const int drawCountBefore = m_renderDiagnostics.GetFrameDrawCallCount();
    const Math::Visibility::Frustum lightFrustum = Math::Visibility::Frustum::FromViewProjection( view, proj );
    const auto isVisible = [&]( const ShadowCasterInstance& caster )
    {
        const Vector3 center( caster.model.m[12], caster.model.m[13], caster.model.m[14] );

        // Why: the orthographic light frustum, including its deliberately broad
        // near/far span, retains off-camera objects whose shadows can reach a
        // receiver. A one-unit margin absorbs light-matrix and bound rounding.
        return lightFrustum.IntersectsSphere( center, caster.boundingRadius, 1.0f );
    };

    const int candidates = static_cast<int>( batches.spheres.size() + batches.boxes.size() + batches.pines.size() +
                                             batches.convexHulls.size() );

    int submitted = 0;

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Spheres" );
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "Spheres" );

        auto sphereBatch = m_primitiveRenderer.BeginShadowDepthSphereBatch( shaderBaseName, view, proj, cinematic );

        for ( const ShadowCasterInstance& caster : batches.spheres )
        {
            if ( isVisible( caster ) )
            {
                sphereBatch.DrawShadowModel( caster.model );
                ++submitted;
            }
        }
    }

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Boxes" );
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "Boxes" );

        auto boxBatch = m_primitiveRenderer.BeginShadowDepthBoxBatch( shaderBaseName, view, proj );

        for ( const ShadowCasterInstance& caster : batches.boxes )
        {
            if ( isVisible( caster ) )
            {
                boxBatch.DrawShadowModel( caster.model );
                ++submitted;
            }
        }
    }

    if ( !batches.pines.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Pines" );
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "Pines" );

        auto pineBatch = m_primitiveRenderer.BeginShadowDepthPineBatch( shaderBaseName, view, proj );

        for ( const ShadowCasterInstance& caster : batches.pines )
        {
            if ( isVisible( caster ) )
            {
                pineBatch.DrawShadowModel( caster.model );
                ++submitted;
            }
        }
    }

    if ( !batches.convexHulls.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/ConvexHulls" );
        DRAW_CALL_TRACE_SCOPE( m_renderDiagnostics, "ConvexHulls" );

        for ( const auto& caster : batches.convexHulls )
        {
            if ( caster.hull && isVisible( caster.instance ) )
            {
                m_primitiveRenderer.DrawShadowDepthConvexHullModel( shaderBaseName, *caster.hull, caster.instance.model,
                                                                    view, proj );

                ++submitted;
            }
        }
    }

    m_renderDiagnostics.RecordVisibility(
        visibilityView, candidates, submitted, candidates - submitted,
        (std::max)( 0, m_renderDiagnostics.GetFrameDrawCallCount() - drawCountBefore ) );
}


void RenderInstanceRenderer::RenderShadowCasters( SkullbonezCore::Core::Profiler* profiler,
                                                  const char* shaderBaseName, const Matrix4& view, const Matrix4& proj,
                                                  const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                                  Rendering::RenderVisibilityView visibilityView )
{
    ShadowCasterBatches batches;
    BuildShadowCasterBatches( profiler, batches );
    SubmitShadowCasterBatches( profiler, shaderBaseName, batches, view, proj, cinematic, visibilityView );
}


bool RenderInstanceRenderer::GetObjectShadowBounds( SkullbonezCore::Core::Profiler* profiler,
                                                    const Vector3& focus, float maxDistance,
                                                    Vector3& outCenter, float& outRadius, float& outHeightRange )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds" );

    const auto instances = m_renderStore.Records();

    if ( instances.empty() )
    {
        return false;
    }

    const float queryDistance = (std::max)( maxDistance, 1.0f );
    const int modelCount = static_cast<int>( instances.size() );
    struct BoundsAccumulator
    {
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float minZ = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;
        float maxZ = -FLT_MAX;
        bool found = false;
    };

    auto scanBoundsRange = [&]( int begin, int end, BoundsAccumulator& bounds )
    {
        bounds = BoundsAccumulator();

        for ( int i = begin; i < end; ++i )
        {
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( i )];

            if ( !instance.editorVisible )
            {
                continue;
            }

            const Matrix4& model = instance.modelMatrix;
            const Vector3 pos( model.m[12], model.m[13], model.m[14] );
            const float radius = instance.boundingRadius;
            const float includeDistance = queryDistance + radius;
            const float dx = pos.x - focus.x;
            const float dz = pos.z - focus.z;

            if ( dx * dx + dz * dz > includeDistance * includeDistance )
            {
                continue;
            }

            bounds.minX = (std::min)( bounds.minX, pos.x - radius );
            bounds.minY = (std::min)( bounds.minY, pos.y - radius );
            bounds.minZ = (std::min)( bounds.minZ, pos.z - radius );
            bounds.maxX = (std::max)( bounds.maxX, pos.x + radius );
            bounds.maxY = (std::max)( bounds.maxY, pos.y + radius );
            bounds.maxZ = (std::max)( bounds.maxZ, pos.z + radius );
            bounds.found = true;
        }
    };

    auto mergeBounds = []( BoundsAccumulator& dst, const BoundsAccumulator& src )
    {
        if ( !src.found )
        {
            return;
        }

        dst.minX = (std::min)( dst.minX, src.minX );
        dst.minY = (std::min)( dst.minY, src.minY );
        dst.minZ = (std::min)( dst.minZ, src.minZ );
        dst.maxX = (std::max)( dst.maxX, src.maxX );
        dst.maxY = (std::max)( dst.maxY, src.maxY );
        dst.maxZ = (std::max)( dst.maxZ, src.maxZ );
        dst.found = true;
    };

    BoundsAccumulator bounds;

    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && m_useShadowParallelPrep && m_workerPool &&
         modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/OrderedWorkerCollect" );
        SkullbonezCore::Threading::WorkerChunkRange chunks[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS];
        const int chunkCount = m_workerPool
                                   ->BuildChunkRangesNoAlloc( 0, modelCount, SHADOW_PARALLEL_PREP_MIN_CASTERS, chunks,
                                                              SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS );

        if ( chunkCount > 1 )
        {
            BoundsAccumulator chunkOutputs[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS] = {};

            m_workerPool->ParallelForChunksNoAlloc( chunks, chunkCount,
                                                    [&]( int chunkIndex, int begin, int end )
                                                    {
                                                        PROFILE_WORKER_SCOPED(
                                                            profiler, "Frame/Shadows/ShadowMap/BuildObjectFrame/"
                                                                      "ObjectBounds/"
                                                                      "OrderedWorkerCollect/WorkerScanBounds" );

                                                        scanBoundsRange( begin, end, chunkOutputs[chunkIndex] );
                                                    } );

            for ( int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
            {
                mergeBounds( bounds, chunkOutputs[chunkIndex] );
            }
        }
        else
        {
            scanBoundsRange( 0, modelCount, bounds );
        }
    }
    else
    {
        scanBoundsRange( 0, modelCount, bounds );
    }

    if ( !bounds.found )
    {
        return false;
    }

    outCenter = Vector3( ( bounds.minX + bounds.maxX ) * 0.5f, ( bounds.minY + bounds.maxY ) * 0.5f,
                         ( bounds.minZ + bounds.maxZ ) * 0.5f );

    const float halfX = ( bounds.maxX - bounds.minX ) * 0.5f;
    const float halfY = ( bounds.maxY - bounds.minY ) * 0.5f;
    const float halfZ = ( bounds.maxZ - bounds.minZ ) * 0.5f;
    const float clusterRadius = sqrtf( halfX * halfX + halfY * halfY + halfZ * halfZ );
    const float padding = 36.0f;

    outRadius = std::clamp( clusterRadius + padding, 48.0f, queryDistance + padding );
    outHeightRange = (std::max)( bounds.maxY - bounds.minY + padding * 2.0f, 64.0f );
    return true;
}
