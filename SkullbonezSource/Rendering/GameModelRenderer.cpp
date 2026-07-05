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
#include "IRenderBackend.h"
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
constexpr bool SHADOW_PARALLEL_PREP_WORKER_ENABLED = false;

bool IsPineVisualMaterial( const RenderMaterial& material )
{
    return material.kind == RenderMaterialKind::Pine ||
           ( material.textureMode > 1.25f &&
             static_cast<int>( std::floor( material.textureMode + 0.5f ) ) == PINE_VISUAL_MATERIAL_MODE );
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
        DRAW_CALL_TRACE_SCOPE( "Spheres" );
        RenderHelper::DrawSphereBatchBegin( helperContext,
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
                RenderHelper::DrawSphereBatchModel( instance.modelMatrix, material );
            }
        }
        RenderHelper::DrawSphereBatchEnd( helperContext );
    }

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
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
                    RenderHelper::DrawPineBatchModel( instance.modelMatrix, material );
                }
                else
                {
                    RenderHelper::DrawBoxBatchModel( instance.modelMatrix, material );
                }
            }
        }
    };

    {
        DRAW_CALL_TRACE_SCOPE( "Boxes" );
        RenderHelper::DrawBoxBatchBegin( helperContext,
                                         view,
                                         proj,
                                         lightPos,
                                         alphaBlendedPass,
                                         cinematic,
                                         shadow,
                                         clampedMaterialAlpha );
        appendBoxLikeModels( false );
        RenderHelper::DrawBoxBatchEnd( helperContext );
    }

    if ( hasPineVisualModels )
    {
        DRAW_CALL_TRACE_SCOPE( "Pines" );
        RenderHelper::DrawPineBatchBegin( helperContext,
                                          view,
                                          proj,
                                          lightPos,
                                          alphaBlendedPass,
                                          cinematic,
                                          shadow,
                                          clampedMaterialAlpha );
        appendBoxLikeModels( true );
        RenderHelper::DrawPineBatchEnd( helperContext );
    }

    {
        DRAW_CALL_TRACE_SCOPE( "ConvexHulls" );
        const std::vector<ColliderRecord>* colliders = nullptr;
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
            RenderHelper::DrawConvexHullModel( helperContext,
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

    auto appendRange = [&]( int begin, int end, ShadowCasterBatches& batches )
    {
        batches.Clear();
        const std::vector<ColliderRecord>* colliders = nullptr;
        for ( int x = begin; x < end; ++x )
        {
            const RenderInstanceRecord& instance = instances[static_cast<std::size_t>( x )];
            if ( instance.shapeKind == RenderInstanceShapeKind::Sphere )
            {
                batches.spheres.push_back( instance.modelMatrix );
                continue;
            }

            if ( instance.shapeKind == RenderInstanceShapeKind::ConvexHull )
            {
                if ( !colliders )
                {
                    colliders = &colliderStore.Records();
                }
                if ( static_cast<std::size_t>( x ) >= colliders->size() )
                {
                    continue;
                }
                const ColliderRecord& collider = ( *colliders )[static_cast<std::size_t>( x )];
                // Lifetime: shadow batches hold pointers into ColliderStore
                // records and are submitted in the same frame before the store
                // is refreshed or compacted.
                const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &collider.shape );
                if ( hull )
                {
                    batches.convexHulls.push_back( { hull, instance.modelMatrix } );
                }
                continue;
            }

            const bool isPineVisual = IsPineVisualMaterial( instance.material );
            if ( isPineVisual )
            {
                batches.pines.push_back( instance.modelMatrix );
            }
            else
            {
                batches.boxes.push_back( instance.modelMatrix );
            }
        }
    };

    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && useShadowParallelPrep && workerPool &&
         modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/OrderedWorkerCollect" );
        std::vector<ShadowCasterBatches> chunkOutputs;
        workerPool->ParallelCollectOrdered<ShadowCasterBatches>(
            0,
            modelCount,
            chunkOutputs,
            [&]( int, int begin, int end, ShadowCasterBatches& local )
            {
                PROFILE_WORKER_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/"
                                       "OrderedWorkerCollect/WorkerBuildBatches" );
                appendRange( begin, end, local );
            },
            [&]( int, const ShadowCasterBatches& local )
            {
                outBatches.spheres.insert( outBatches.spheres.end(), local.spheres.begin(), local.spheres.end() );
                outBatches.boxes.insert( outBatches.boxes.end(), local.boxes.begin(), local.boxes.end() );
                outBatches.pines.insert( outBatches.pines.end(), local.pines.begin(), local.pines.end() );
                outBatches.convexHulls.insert( outBatches.convexHulls.end(),
                                               local.convexHulls.begin(),
                                               local.convexHulls.end() );
            },
            SHADOW_PARALLEL_PREP_MIN_CASTERS );
        return;
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
        DRAW_CALL_TRACE_SCOPE( "Spheres" );

        RenderHelper::DrawShadowDepthSphereBatchBegin( helperContext, view, proj, cinematic );
        for ( const Matrix4& model : batches.spheres )
        {
            RenderHelper::DrawShadowDepthSphereBatchModel( model );
        }
        RenderHelper::DrawShadowDepthSphereBatchEnd( helperContext );
    }

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Boxes" );
        DRAW_CALL_TRACE_SCOPE( "Boxes" );

        RenderHelper::DrawShadowDepthBoxBatchBegin( helperContext, view, proj );
        for ( const Matrix4& model : batches.boxes )
        {
            RenderHelper::DrawShadowDepthBoxBatchModel( model );
        }
        RenderHelper::DrawShadowDepthBoxBatchEnd( helperContext );
    }

    if ( !batches.pines.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Pines" );
        DRAW_CALL_TRACE_SCOPE( "Pines" );

        RenderHelper::DrawShadowDepthPineBatchBegin( helperContext, view, proj );
        for ( const Matrix4& model : batches.pines )
        {
            RenderHelper::DrawShadowDepthPineBatchModel( model );
        }
        RenderHelper::DrawShadowDepthPineBatchEnd( helperContext );
    }

    if ( !batches.convexHulls.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/ConvexHulls" );
        DRAW_CALL_TRACE_SCOPE( "ConvexHulls" );

        for ( const auto& caster : batches.convexHulls )
        {
            if ( caster.hull )
            {
                RenderHelper::DrawShadowDepthConvexHullModel( helperContext, *caster.hull, caster.model, view, proj );
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
        std::vector<BoundsAccumulator> chunkOutputs;
        workerPool->ParallelCollectOrdered<BoundsAccumulator>(
            0,
            modelCount,
            chunkOutputs,
            [&]( int, int begin, int end, BoundsAccumulator& local )
            {
                PROFILE_WORKER_SCOPED(
                    "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/OrderedWorkerCollect/WorkerScanBounds" );
                scanBoundsRange( begin, end, local );
            },
            [&]( int, const BoundsAccumulator& local ) { mergeBounds( bounds, local ); },
            SHADOW_PARALLEL_PREP_MIN_CASTERS );
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
