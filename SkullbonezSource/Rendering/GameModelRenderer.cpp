/*
File: SkullbonezSource/Rendering/GameModelRenderer.cpp
Purpose:
  Converts model-order render snapshots into backend draw calls for normal and
  shadow rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts. Draw transforms come from
  RenderInstanceStore and hull geometry comes from ColliderStore so
  physics-owned pose/shape data does not have to be copied back into every
  GameModel merely for rendering.

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

Related:
  - SkullbonezSource/Rendering/GameModelRenderer.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModelRenderer.h"

#include "../Core/Config.h"
#include "../Physics/ColliderStore.h"
#include "Helper.h"
#include "IRenderDiagnostics.h"
#include "RenderInstanceStore.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

using namespace SkullbonezCore::Basics;
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
        batches.spheres.push_back( instance.modelMatrix );
        break;
    case ShadowCasterStream::Box:
        batches.boxes.push_back( instance.modelMatrix );
        break;
    case ShadowCasterStream::Pine:
        batches.pines.push_back( instance.modelMatrix );
        break;
    case ShadowCasterStream::ConvexHull:
        // Lifetime: convex hull casters borrow ColliderStore geometry and are
        // submitted during this shadow pass before collider storage refreshes.
        batches.convexHulls.push_back( { hull, instance.modelMatrix } );
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
            batches.spheres[static_cast<std::size_t>( write.spheres++ )] = instance.modelMatrix;
            break;
        case ShadowCasterStream::Box:
            batches.boxes[static_cast<std::size_t>( write.boxes++ )] = instance.modelMatrix;
            break;
        case ShadowCasterStream::Pine:
            batches.pines[static_cast<std::size_t>( write.pines++ )] = instance.modelMatrix;
            break;
        case ShadowCasterStream::ConvexHull:
            batches.convexHulls[static_cast<std::size_t>( write.convexHulls++ )] = { hull, instance.modelMatrix };
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
                                      const CinematicRenderConfig* cinematic,
                                      const ShadowFrameData* shadow,
                                      float materialAlpha,
                                      const std::vector<uint8_t>* modelMask,
                                      bool drawMaskedModels )
{
    const std::vector<RenderInstanceRecord>& instances = renderStore.Records();

    if ( instances.empty() )
    {
        return;
    }

    const int modelCount = static_cast<int>( instances.size() );
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
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !shouldDrawModel( x ) )
            {
                continue;
            }
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];
            if ( instance.shapeKind == RenderInstanceShapeKind::Sphere )
            {
                RenderMaterial material = MaterialWithContactHighlights( instance, false );
                sphereBatch.DrawModel( instance.modelMatrix, material );
            }
        }
    }

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass, RenderHelper::PrimitiveBatchScope& batch )
    {
        for ( int x = 0; x < modelCount; ++x )
        {
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
        for ( int x = 0; x < modelCount; ++x )
        {
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
        }
    }
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
        throw std::runtime_error( "Shadow caster batch reserve exhausted" );
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
                                                   const CinematicRenderConfig* cinematic )
{
    if ( batches.Empty() )
    {
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Spheres" );
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Spheres" );

        auto sphereBatch = helperContext.helper.BeginShadowDepthSphereBatch( helperContext, view, proj, cinematic );
        for ( const Matrix4& model : batches.spheres )
        {
            sphereBatch.DrawShadowModel( model );
        }
    }

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Boxes" );
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Boxes" );

        auto boxBatch = helperContext.helper.BeginShadowDepthBoxBatch( helperContext, view, proj );
        for ( const Matrix4& model : batches.boxes )
        {
            boxBatch.DrawShadowModel( model );
        }
    }

    if ( !batches.pines.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Pines" );
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "Pines" );

        auto pineBatch = helperContext.helper.BeginShadowDepthPineBatch( helperContext, view, proj );
        for ( const Matrix4& model : batches.pines )
        {
            pineBatch.DrawShadowModel( model );
        }
    }

    if ( !batches.convexHulls.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/ConvexHulls" );
        DRAW_CALL_TRACE_SCOPE( helperContext.renderDiagnostics, "ConvexHulls" );

        for ( const auto& caster : batches.convexHulls )
        {
            if ( caster.hull )
            {
                helperContext.helper.DrawShadowDepthConvexHullModel( helperContext,
                                                                     *caster.hull,
                                                                     caster.model,
                                                                     view,
                                                                     proj );
            }
        }
    }
}


void GameModelRenderer::RenderShadowCasters( const RenderHelperContext& helperContext,
                                             const RenderInstanceStore& renderStore,
                                             const SkullbonezCore::Physics::ColliderStore& colliderStore,
                                             SkullbonezCore::Threading::WorkerPool* workerPool,
                                             bool useShadowParallelPrep,
                                             const Matrix4& view,
                                             const Matrix4& proj,
                                             const CinematicRenderConfig* cinematic )
{
    ShadowCasterBatches batches;
    BuildShadowCasterBatches( renderStore, colliderStore, workerPool, useShadowParallelPrep, batches );
    SubmitShadowCasterBatches( helperContext, batches, view, proj, cinematic );
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


void GameModelRenderer::ResetRenderResources()
{
}
