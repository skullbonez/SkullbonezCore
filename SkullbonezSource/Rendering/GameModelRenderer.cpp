/*
File: SkullbonezSource/Rendering/GameModelRenderer.cpp
Purpose:
  Converts model-order render snapshots into backend draw calls for normal and
  shadow rendering.

Summary:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts. Draw transforms come from
  RenderInstanceStore and hull geometry comes from ColliderStore so
  physics-owned pose/shape data does not have to be copied back into every
  legacy object record merely for rendering.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Convex hull: Immutable authored collision geometry rendered through dynamic
    hull vertices instead of the sphere or box instance streams.
  Contact-audio flash: Short render-only white tint applied after a contact
    sound actually submits, independent of physics state.

Invariants:
  - GameModelRenderer consumes prepared render instances and collider rows; the
    scene owner remains responsible for refreshing those stores before drawing.
  - Shadow caster preparation may run worker-side, but draw submission remains
    on the render thread through RenderHelper command/resource contexts.
  - ShadowCasterBatches capacity is reserved before steady rendering; a larger
    model count is a fixed-capacity render invariant failure.

Related:
  - SkullbonezSource/Rendering/GameModelRenderer.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelRenderer.h"

#include "../Core/Config.h"
#include "../Core/FatalError.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"
#include "../Physics/ColliderStore.h"
#include "../Maths/Frustum.h"
#include "Helper.h"
#include "IRenderDiagnostics.h"
#include "RenderInstanceStore.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Rendering::RenderInstanceRecord;
using SkullbonezCore::Rendering::RenderInstanceShapeKind;
using SkullbonezCore::Rendering::RenderInstanceStore;
using SkullbonezCore::Rendering::RenderMaterial;
using SkullbonezCore::Rendering::RenderMaterialKind;
using SkullbonezCore::Rendering::ShadowCasterBatches;
using SkullbonezCore::Rendering::ShadowCasterInstance;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
constexpr int PINE_VISUAL_MATERIAL_MODE = 13;
constexpr int SHADOW_PARALLEL_PREP_MIN_CASTERS = 512;
constexpr bool SHADOW_PARALLEL_PREP_WORKER_ENABLED = true;

enum class ShadowCasterStream : uint8_t
{
    None,
    Sphere,
    Box,
    Pine,
    ConvexHull
};

struct ShadowCasterStreamCounts
{
    int spheres = 0;
    int boxes = 0;
    int pines = 0;
    int convexHulls = 0;
};

bool IsPineVisualMaterial( const RenderMaterial& material )
{
    return material.kind == RenderMaterialKind::Pine ||
           ( material.textureMode > 1.25f &&
             static_cast<int>( std::floor( material.textureMode + 0.5f ) ) == PINE_VISUAL_MATERIAL_MODE );
}

ShadowCasterStream ResolveShadowCasterStream( const RenderInstanceRecord& instance,
                                              int modelIndex,
                                              const ColliderRecordList& colliders,
                                              const ConvexHullShape*& outHull )
{
    outHull = nullptr;
    if ( instance.shapeKind == RenderInstanceShapeKind::Sphere )
    {
        return ShadowCasterStream::Sphere;
    }

    if ( instance.shapeKind == RenderInstanceShapeKind::ConvexHull )
    {
        if ( modelIndex < 0 || static_cast<std::size_t>( modelIndex ) >= colliders.size() )
        {
            return ShadowCasterStream::None;
        }
        const ColliderRecord& collider = colliders[static_cast<std::size_t>( modelIndex )];
        const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &collider.shape );
        if ( hull )
        {
            outHull = hull;
            return ShadowCasterStream::ConvexHull;
        }
        return ShadowCasterStream::None;
    }

    return IsPineVisualMaterial( instance.material ) ? ShadowCasterStream::Pine : ShadowCasterStream::Box;
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

void AppendShadowCasterToBatches( const RenderInstanceRecord& instance,
                                  int modelIndex,
                                  const ColliderRecordList& colliders,
                                  ShadowCasterBatches& batches )
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

void CountShadowCasterRange( const std::vector<RenderInstanceRecord>& instances,
                             const ColliderRecordList& colliders,
                             int begin,
                             int end,
                             ShadowCasterStreamCounts& counts )
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

void FillShadowCasterRange( const std::vector<RenderInstanceRecord>& instances,
                            const ColliderRecordList& colliders,
                            int begin,
                            int end,
                            const ShadowCasterStreamCounts& offsets,
                            ShadowCasterBatches& batches )
{
    ShadowCasterStreamCounts write = offsets;
    for ( int x = begin; x < end; ++x )
    {
        const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];
        const ConvexHullShape* hull = nullptr;
        switch ( ResolveShadowCasterStream( instance, x, colliders, hull ) )
        {
        case ShadowCasterStream::Sphere:
            batches.spheres[static_cast<std::size_t>( write.spheres++ )] = { instance.modelMatrix,
                                                                             instance.boundingRadius };
            break;
        case ShadowCasterStream::Box:
            batches.boxes[static_cast<std::size_t>( write.boxes++ )] = { instance.modelMatrix,
                                                                         instance.boundingRadius };
            break;
        case ShadowCasterStream::Pine:
            batches.pines[static_cast<std::size_t>( write.pines++ )] = { instance.modelMatrix,
                                                                         instance.boundingRadius };
            break;
        case ShadowCasterStream::ConvexHull:
            batches.convexHulls[static_cast<std::size_t>( write.convexHulls++ )] = {
                hull,
                { instance.modelMatrix, instance.boundingRadius } };
            break;
        case ShadowCasterStream::None:
            break;
        }
    }
}

bool BuildShadowCasterBatchesWithWorkers( const std::vector<RenderInstanceRecord>& instances,
                                          const ColliderRecordList& colliders,
                                          SkullbonezCore::Threading::WorkerPool& workerPool,
                                          int modelCount,
                                          ShadowCasterBatches& outBatches )
{
    // Concept: shadow prep uses count/prefix/fill instead of chunk-local
    // vectors. Counting gives each worker a stable output range for every
    // caster stream, so the fill pass can run in parallel without allocating or
    // reordering the serial sphere/box/pine/hull stream order.
    SkullbonezCore::Threading::WorkerChunkRange chunks[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS];
    const int chunkCount =
        workerPool.BuildChunkRangesNoAlloc( 0,
                                            modelCount,
                                            SHADOW_PARALLEL_PREP_MIN_CASTERS,
                                            chunks,
                                            SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS );
    if ( chunkCount <= 1 )
    {
        return false;
    }

    ShadowCasterStreamCounts counts[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS] = {};
    workerPool.ParallelForChunksNoAlloc(
        chunks,
        chunkCount,
        [&]( int chunkIndex, int begin, int end )
        {
            PROFILE_WORKER_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/"
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
    workerPool.ParallelForChunksNoAlloc(
        chunks,
        chunkCount,
        [&]( int chunkIndex, int begin, int end )
        {
            PROFILE_WORKER_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/"
                                   "OrderedWorkerCollect/WorkerFillBatches" );
            FillShadowCasterRange( instances, colliders, begin, end, offsets[chunkIndex], outBatches );
        } );
    return true;
}

RenderMaterial MaterialWithContactHighlights( const RenderInstanceRecord& instance, bool box )
{
    // Why: contact highlights are render-only feedback. They must not mutate
    // the model's stored material, physics release policy, or audio decisions.
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

    const float audioHit = instance.audioContactAlpha;
    if ( audioHit <= 0.0f )
    {
        return material;
    }

    // Concept: audio flash is a shader-side final-color blend, not a texture or
    // material-mode replacement. That makes the object visibly white for the
    // 100ms timer and then fades back to its normal material branch.
    material.contactFlashAlpha = (std::max)( material.contactFlashAlpha, audioHit );
    return material;
}
} // namespace


void GameModelRenderer::RenderModels( const RenderHelperContext& helperContext,
                                      const RenderInstanceStore& renderStore,
                                      const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                      bool renderCollisionVolumes,
                                      const Matrix4& view,
                                      const Matrix4& proj,
                                      const float lightPos[4],
                                      const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                      const ShadowFrameData* shadow,
                                      float materialAlpha,
                                      const std::vector<uint8_t>* modelMask,
                                      bool drawMaskedModels,
                                      Rendering::RenderVisibilityView visibilityView )
{
    const std::vector<RenderInstanceRecord>& instances = renderStore.Records();

    if ( instances.empty() )
    {
        return;
    }

    const int modelCount = static_cast<int>( instances.size() );
    // Invariant: the visible-index scratch array mirrors the scene store's
    // compile-time ceiling. Crossing it would corrupt render-thread stack data.
    if ( modelCount > SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS )
    {
        SB_FATAL( "Rendering/Visibility",
                  "Render instance count exceeds visibility capacity. count=%d capacity=%d",
                  modelCount,
                  SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    }
    int visibleIndices[SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS] = {};
    int visibleCount = 0;
    // Why: the planar reflection has its own mirrored camera volume. Its water
    // half-space removes only instances wholly below the surface; straddlers
    // still reach the shader clip plane for pixel-accurate clipping.
    if ( visibilityView == Rendering::RenderVisibilityView::Main ||
         visibilityView == Rendering::RenderVisibilityView::Reflection )
    {
        const Math::Visibility::Frustum frustum = Math::Visibility::Frustum::FromViewProjection( view, proj );
        const float* reflectionClipPlane = visibilityView == Rendering::RenderVisibilityView::Reflection
                                               ? helperContext.helper.GetClipPlane()
                                               : nullptr;
        for ( int index = 0; index < modelCount; ++index )
        {
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( index )];
            const Vector3 center( instance.modelMatrix.m[12], instance.modelMatrix.m[13], instance.modelMatrix.m[14] );
            const bool insideFrustum = frustum.IntersectsSphere( center, instance.boundingRadius );
            const bool aboveReflectionPlane =
                !reflectionClipPlane ||
                Math::Visibility::Frustum::IntersectsHalfSpace( center, instance.boundingRadius, reflectionClipPlane );
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
            visibleIndices[visibleCount++] = index;
        }
    }

    const float clampedMaterialAlpha = std::clamp( materialAlpha, 0.0f, 1.0f );
    const bool alphaBlendedPass = renderCollisionVolumes || clampedMaterialAlpha < 1.0f;
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
    const int drawCountBefore = helperContext.renderDiagnostics.GetFrameDrawCallCount();

    {
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Spheres" );
        auto sphereBatch = helperContext.helper.BeginSphereBatch( helperContext,
                                                                  view,
                                                                  proj,
                                                                  lightPos,
                                                                  alphaBlendedPass,
                                                                  cinematic,
                                                                  shadow,
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
    auto appendBoxLikeModels = [&]( bool pineVisualPass, RenderHelper::PrimitiveBatchScope& batch )
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
                const bool isPineVisual = IsPineVisualMaterial( material );
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
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Boxes" );
        auto boxBatch = helperContext.helper.BeginBoxBatch( helperContext,
                                                            view,
                                                            proj,
                                                            lightPos,
                                                            alphaBlendedPass,
                                                            cinematic,
                                                            shadow,
                                                            clampedMaterialAlpha );
        appendBoxLikeModels( false, boxBatch );
    }

    if ( hasPineVisualModels )
    {
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Pines" );
        auto pineBatch = helperContext.helper.BeginPineBatch( helperContext,
                                                              view,
                                                              proj,
                                                              lightPos,
                                                              alphaBlendedPass,
                                                              cinematic,
                                                              shadow,
                                                              clampedMaterialAlpha );
        appendBoxLikeModels( true, pineBatch );
    }

    {
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "ConvexHulls" );
        const ColliderRecordList* colliders = nullptr;
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
            if ( !colliders )
            {
                colliders = &colliderStore.Records();
            }
            if ( static_cast<std::size_t>( x ) >= colliders->size() )
            {
                continue;
            }

            const ColliderRecord& collider = ( *colliders )[static_cast<std::size_t>( x )];
            const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &collider.shape );
            if ( !hull )
            {
                continue;
            }

            const RenderMaterial material = MaterialWithContactHighlights( instance, false );
            helperContext.helper.DrawConvexHullModel( helperContext,
                                                      *hull,
                                                      instance.modelMatrix,
                                                      material,
                                                      view,
                                                      proj,
                                                      lightPos,
                                                      alphaBlendedPass,
                                                      cinematic,
                                                      shadow,
                                                      clampedMaterialAlpha );
            ++submittedCount;
        }
    }
    const int drawCountAfter = helperContext.renderDiagnostics.GetFrameDrawCallCount();
    helperContext.renderDiagnostics.RecordVisibility( visibilityView,
                                                      modelCount,
                                                      submittedCount,
                                                      modelCount - visibleCount,
                                                      (std::max)( 0, drawCountAfter - drawCountBefore ) );
}


void GameModelRenderer::BuildShadowCasterBatches( const RenderInstanceStore& renderStore,
                                                  const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                                  SkullbonezCore::Threading::WorkerPool* workerPool,
                                                  bool useShadowParallelPrep,
                                                  ShadowCasterBatches& outBatches )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches" );

    const std::vector<RenderInstanceRecord>& instances = renderStore.Records();
    outBatches.Clear();

    if ( instances.empty() )
    {
        return;
    }

    const int modelCount = static_cast<int>( instances.size() );
    assert( outBatches.HasCapacityForModelCount( modelCount ) );
    if ( !outBatches.HasCapacityForModelCount( modelCount ) )
    {
        SB_FATAL( "GameModelRenderer",
                  "Shadow caster batch reserve exhausted. modelCount=%d sphereCapacity=%zu boxCapacity=%zu "
                  "pineCapacity=%zu hullCapacity=%zu",
                  modelCount,
                  outBatches.spheres.capacity(),
                  outBatches.boxes.capacity(),
                  outBatches.pines.capacity(),
                  outBatches.convexHulls.capacity() );
    }

    const ColliderRecordList& colliders = colliderStore.Records();
    auto appendRange = [&]( int begin, int end, ShadowCasterBatches& batches )
    {
        for ( int x = begin; x < end; ++x )
        {
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];
            AppendShadowCasterToBatches( instance, x, colliders, batches );
        }
    };

    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && useShadowParallelPrep && workerPool &&
         modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/OrderedWorkerCollect" );
        if ( BuildShadowCasterBatchesWithWorkers( instances, colliders, *workerPool, modelCount, outBatches ) )
        {
            return;
        }
    }

    appendRange( 0, modelCount, outBatches );
}


void GameModelRenderer::SubmitShadowCasterBatches( const RenderHelperContext& helperContext,
                                                   const ShadowCasterBatches& batches,
                                                   const Matrix4& view,
                                                   const Matrix4& proj,
                                                   const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                                   Rendering::RenderVisibilityView visibilityView )
{
    if ( batches.Empty() )
    {
        return;
    }

    const int drawCountBefore = helperContext.renderDiagnostics.GetFrameDrawCallCount();
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
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Spheres" );

        auto sphereBatch = helperContext.helper.BeginShadowDepthSphereBatch( helperContext, view, proj, cinematic );
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
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Boxes" );

        auto boxBatch = helperContext.helper.BeginShadowDepthBoxBatch( helperContext, view, proj );
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
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Pines" );

        auto pineBatch = helperContext.helper.BeginShadowDepthPineBatch( helperContext, view, proj );
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
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "ConvexHulls" );

        for ( const auto& caster : batches.convexHulls )
        {
            if ( caster.hull && isVisible( caster.instance ) )
            {
                helperContext.helper.DrawShadowDepthConvexHullModel( helperContext,
                                                                     *caster.hull,
                                                                     caster.instance.model,
                                                                     view,
                                                                     proj );
                ++submitted;
            }
        }
    }
    helperContext.renderDiagnostics.RecordVisibility(
        visibilityView,
        candidates,
        submitted,
        candidates - submitted,
        (std::max)( 0, helperContext.renderDiagnostics.GetFrameDrawCallCount() - drawCountBefore ) );
}


void GameModelRenderer::RenderShadowCasters( const RenderHelperContext& helperContext,
                                             const RenderInstanceStore& renderStore,
                                             const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                             SkullbonezCore::Threading::WorkerPool* workerPool,
                                             bool useShadowParallelPrep,
                                             const Matrix4& view,
                                             const Matrix4& proj,
                                             const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                             Rendering::RenderVisibilityView visibilityView )
{
    ShadowCasterBatches batches;
    BuildShadowCasterBatches( renderStore, colliderStore, workerPool, useShadowParallelPrep, batches );
    SubmitShadowCasterBatches( helperContext, batches, view, proj, cinematic, visibilityView );
}


bool GameModelRenderer::GetObjectShadowBounds( const RenderInstanceStore& renderStore,
                                               SkullbonezCore::Threading::WorkerPool* workerPool,
                                               bool useShadowParallelPrep,
                                               const Vector3& focus,
                                               float maxDistance,
                                               Vector3& outCenter,
                                               float& outRadius,
                                               float& outHeightRange )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds" );

    const std::vector<RenderInstanceRecord>& instances = renderStore.Records();

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
    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && useShadowParallelPrep && workerPool &&
         modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/OrderedWorkerCollect" );
        SkullbonezCore::Threading::WorkerChunkRange chunks[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS];
        const int chunkCount =
            workerPool->BuildChunkRangesNoAlloc( 0,
                                                 modelCount,
                                                 SHADOW_PARALLEL_PREP_MIN_CASTERS,
                                                 chunks,
                                                 SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS );
        if ( chunkCount > 1 )
        {
            BoundsAccumulator chunkOutputs[SkullbonezCore::Threading::WorkerPool::MAX_PARALLEL_TASKS] = {};
            workerPool->ParallelForChunksNoAlloc(
                chunks,
                chunkCount,
                [&]( int chunkIndex, int begin, int end )
                {
                    PROFILE_WORKER_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/"
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

    outCenter = Vector3( ( bounds.minX + bounds.maxX ) * 0.5f,
                         ( bounds.minY + bounds.maxY ) * 0.5f,
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
