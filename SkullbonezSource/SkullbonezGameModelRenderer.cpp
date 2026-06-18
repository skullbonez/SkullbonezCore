/*
File: SkullbonezSource/SkullbonezGameModelRenderer.cpp
Purpose:
  Converts GameModel data into backend draw calls for normal and shadow rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezGameModelRenderer.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezGameModelRenderer.h"

#include "SkullbonezConfig.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezHelper.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezWorkerPool.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::RenderMaterial;
using SkullbonezCore::Rendering::RenderMaterialKind;
using SkullbonezCore::Rendering::ShadowCasterBatches;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
constexpr int PINE_VISUAL_MATERIAL_MODE = 13;
constexpr int SHADOW_PARALLEL_PREP_MIN_CASTERS = 512;
constexpr bool SHADOW_PARALLEL_PREP_WORKER_ENABLED = true;

bool IsPineVisualMaterial( const RenderMaterial& material )
{
    return material.kind == RenderMaterialKind::Pine ||
           ( material.textureMode > 1.25f && static_cast<int>( std::floor( material.textureMode + 0.5f ) ) == PINE_VISUAL_MATERIAL_MODE );
}

RenderMaterial MaterialWithFixedContactHighlight( const GameModel& model, bool box )
{
    RenderMaterial material = model.GetRenderMaterial();
    const float hit = model.GetFixedContactHighlightAlpha();
    if ( hit <= 0.0f )
    {
        return material;
    }

    if ( box && material.textureMode <= 0.5f && material.textureMode >= -0.5f )
    {
        constexpr float fixedBase = 241.0f / 255.0f;
        material.baseColor[0] = fixedBase + ( 1.0f - fixedBase ) * hit;
        material.baseColor[1] = fixedBase * ( 1.0f - hit );
        material.baseColor[2] = fixedBase * ( 1.0f - hit );
        material.kind = RenderMaterialKind::Matte;
        material.textureMode = 1.0f;
        return material;
    }

    material.baseColor[0] = material.baseColor[0] + ( 1.0f - material.baseColor[0] ) * hit;
    material.baseColor[1] = material.baseColor[1] * ( 1.0f - hit );
    material.baseColor[2] = material.baseColor[2] * ( 1.0f - hit );
    return material;
}
} // namespace


void GameModelRenderer::RenderModels( GameModelCollection& collection, const Matrix4& view, const Matrix4& proj, const float lightPos[4], const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow, float materialAlpha )
{
    const std::vector<GameModel>& models = collection.Models();

    if ( models.empty() )
    {
        return;
    }

    const GameModelRenderStream renderStream = collection.GetRenderStream();
    const int modelCount = renderStream.count;
    const float clampedMaterialAlpha = std::clamp( materialAlpha, 0.0f, 1.0f );
    const bool transparentMaterial = Cfg().runtimeRender.renderCollisionVolumes || clampedMaterialAlpha < 1.0f;

    {
        DRAW_CALL_TRACE_SCOPE( "Spheres" );
        SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( models[x].IsSphere() )
            {
                RenderMaterial material = renderStream.isFixed[x] ? MaterialWithFixedContactHighlight( models[x], false ) : models[x].GetRenderMaterial();
                SkullbonezHelper::DrawSphereBatchModel( renderStream.modelMatrices[x], material );
            }
        }
        SkullbonezHelper::DrawSphereBatchEnd();
    }

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( renderStream.isBox[x] )
            {
                RenderMaterial material = renderStream.isFixed[x] ? MaterialWithFixedContactHighlight( models[x], true ) : models[x].GetRenderMaterial();
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
                    SkullbonezHelper::DrawPineBatchModel( renderStream.modelMatrices[x], material );
                }
                else
                {
                    SkullbonezHelper::DrawBoxBatchModel( renderStream.modelMatrices[x], material );
                }
            }
        }
    };

    {
        DRAW_CALL_TRACE_SCOPE( "Boxes" );
        SkullbonezHelper::DrawBoxBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
        appendBoxLikeModels( false );
        SkullbonezHelper::DrawBoxBatchEnd();
    }

    if ( hasPineVisualModels )
    {
        DRAW_CALL_TRACE_SCOPE( "Pines" );
        SkullbonezHelper::DrawPineBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
        appendBoxLikeModels( true );
        SkullbonezHelper::DrawPineBatchEnd();
    }

    {
        DRAW_CALL_TRACE_SCOPE( "ConvexHulls" );
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !models[x].IsConvexHull() )
            {
                continue;
            }

            const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &models[x].GetCollisionShape() );
            if ( !hull )
            {
                continue;
            }

            const Matrix4 bodyModel = Matrix4::Translate( models[x].GetPosition() ) * Matrix4::FromQuaternion( models[x].GetOrientation() );
            const RenderMaterial material = renderStream.isFixed[x] ? MaterialWithFixedContactHighlight( models[x], false ) : models[x].GetRenderMaterial();
            SkullbonezHelper::DrawConvexHullModel( *hull, bodyModel, material, view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
        }
    }
}


void GameModelRenderer::BuildShadowCasterBatches( GameModelCollection& collection, ShadowCasterBatches& outBatches )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches" );

    const std::vector<GameModel>& models = collection.Models();
    outBatches.Clear();

    if ( models.empty() )
    {
        return;
    }

    const GameModelRenderStream renderStream = collection.GetRenderStream();
    const int modelCount = renderStream.count;

    auto appendRange = [&]( int begin, int end, ShadowCasterBatches& batches )
    {
        batches.Clear();
        batches.spheres.reserve( static_cast<size_t>( end - begin ) );
        batches.boxes.reserve( static_cast<size_t>( end - begin ) );
        batches.pines.reserve( static_cast<size_t>( end - begin ) );
        for ( int x = begin; x < end; ++x )
        {
            if ( models[x].IsSphere() )
            {
                batches.spheres.push_back( renderStream.modelMatrices[x] );
                continue;
            }

            if ( models[x].IsConvexHull() )
            {
                continue;
            }

            const bool isPineVisual = IsPineVisualMaterial( models[x].GetRenderMaterial() );
            if ( isPineVisual )
            {
                batches.pines.push_back( renderStream.modelMatrices[x] );
            }
            else
            {
                batches.boxes.push_back( renderStream.modelMatrices[x] );
            }
        }
    };

    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && Cfg().shadowParallelPrep && modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/OrderedWorkerCollect" );
        std::vector<ShadowCasterBatches> chunkOutputs;
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelCollectOrdered<ShadowCasterBatches>(
            0,
            modelCount,
            chunkOutputs,
            [&]( int, int begin, int end, ShadowCasterBatches& local )
            {
                PROFILE_WORKER_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/OrderedWorkerCollect/WorkerBuildBatches" );
                appendRange( begin, end, local );
            },
            [&]( int, const ShadowCasterBatches& local )
            {
                outBatches.spheres.insert( outBatches.spheres.end(), local.spheres.begin(), local.spheres.end() );
                outBatches.boxes.insert( outBatches.boxes.end(), local.boxes.begin(), local.boxes.end() );
                outBatches.pines.insert( outBatches.pines.end(), local.pines.begin(), local.pines.end() );
            },
            SHADOW_PARALLEL_PREP_MIN_CASTERS );
        return;
    }

    appendRange( 0, modelCount, outBatches );
}


void GameModelRenderer::SubmitShadowCasterBatches( const ShadowCasterBatches& batches, const Matrix4& view, const Matrix4& proj, const CinematicRenderConfig* cinematic )
{
    if ( batches.Empty() )
    {
        return;
    }

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Spheres" );
        DRAW_CALL_TRACE_SCOPE( "Spheres" );

        SkullbonezHelper::DrawShadowDepthSphereBatchBegin( view, proj, cinematic );
        for ( const Matrix4& model : batches.spheres )
        {
            SkullbonezHelper::DrawShadowDepthSphereBatchModel( model );
        }
        SkullbonezHelper::DrawShadowDepthSphereBatchEnd();
    }

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Boxes" );
        DRAW_CALL_TRACE_SCOPE( "Boxes" );

        SkullbonezHelper::DrawShadowDepthBoxBatchBegin( view, proj );
        for ( const Matrix4& model : batches.boxes )
        {
            SkullbonezHelper::DrawShadowDepthBoxBatchModel( model );
        }
        SkullbonezHelper::DrawShadowDepthBoxBatchEnd();
    }

    if ( !batches.pines.empty() )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/SubmitBatches/Pines" );
        DRAW_CALL_TRACE_SCOPE( "Pines" );

        SkullbonezHelper::DrawShadowDepthPineBatchBegin( view, proj );
        for ( const Matrix4& model : batches.pines )
        {
            SkullbonezHelper::DrawShadowDepthPineBatchModel( model );
        }
        SkullbonezHelper::DrawShadowDepthPineBatchEnd();
    }
}


void GameModelRenderer::RenderShadowCasters( GameModelCollection& collection, const Matrix4& view, const Matrix4& proj, const CinematicRenderConfig* cinematic )
{
    ShadowCasterBatches batches;
    BuildShadowCasterBatches( collection, batches );
    SubmitShadowCasterBatches( batches, view, proj, cinematic );

    const std::vector<GameModel>& models = collection.Models();
    DRAW_CALL_TRACE_SCOPE( "ConvexHulls" );
    for ( const GameModel& model : models )
    {
        if ( !model.IsConvexHull() )
        {
            continue;
        }

        const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &model.GetCollisionShape() );
        if ( !hull )
        {
            continue;
        }

        const Matrix4 bodyModel = Matrix4::Translate( model.GetPosition() ) * Matrix4::FromQuaternion( model.GetOrientation() );
        SkullbonezHelper::DrawShadowDepthConvexHullModel( *hull, bodyModel, view, proj );
    }
}


bool GameModelRenderer::GetObjectShadowBounds( GameModelCollection& collection, const Vector3& focus, float maxDistance, Vector3& outCenter, float& outRadius, float& outHeightRange )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds" );

    const GameModelBodyStream bodyStream = collection.GetBodyStream();

    if ( bodyStream.Empty() )
    {
        return false;
    }

    const float queryDistance = (std::max)( maxDistance, 1.0f );
    const int modelCount = bodyStream.count;
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
            const Vector3& pos = bodyStream.positions[i];
            const float radius = bodyStream.boundingRadii[i];
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
    if ( SHADOW_PARALLEL_PREP_WORKER_ENABLED && Cfg().shadowParallelPrep && modelCount >= SHADOW_PARALLEL_PREP_MIN_CASTERS )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/OrderedWorkerCollect" );
        std::vector<BoundsAccumulator> chunkOutputs;
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelCollectOrdered<BoundsAccumulator>(
            0,
            modelCount,
            chunkOutputs,
            [&]( int, int begin, int end, BoundsAccumulator& local )
            {
                PROFILE_WORKER_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame/ObjectBounds/OrderedWorkerCollect/WorkerScanBounds" );
                scanBoundsRange( begin, end, local );
            },
            [&]( int, const BoundsAccumulator& local )
            { mergeBounds( bounds, local ); },
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
