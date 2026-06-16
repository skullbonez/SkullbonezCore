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
#include "SkullbonezProfiler.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::RenderMaterial;
using SkullbonezCore::Rendering::RenderMaterialKind;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
constexpr int PINE_VISUAL_MATERIAL_MODE = 13;

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

    SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( !renderStream.isBox[x] )
        {
            RenderMaterial material = renderStream.isFixed[x] ? MaterialWithFixedContactHighlight( models[x], false ) : models[x].GetRenderMaterial();
            SkullbonezHelper::DrawSphereBatchModel( renderStream.modelMatrices[x], material );
        }
    }
    SkullbonezHelper::DrawSphereBatchEnd();

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

    SkullbonezHelper::DrawBoxBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
    appendBoxLikeModels( false );
    SkullbonezHelper::DrawBoxBatchEnd();

    if ( hasPineVisualModels )
    {
        SkullbonezHelper::DrawPineBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
        appendBoxLikeModels( true );
        SkullbonezHelper::DrawPineBatchEnd();
    }
}


void GameModelRenderer::RenderShadowCasters( GameModelCollection& collection, const Matrix4& view, const Matrix4& proj, const CinematicRenderConfig* cinematic )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches" );

    const std::vector<GameModel>& models = collection.Models();

    if ( models.empty() )
    {
        return;
    }

    const GameModelRenderStream renderStream = collection.GetRenderStream();
    const int modelCount = renderStream.count;

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/Spheres" );

        SkullbonezHelper::DrawShadowDepthSphereBatchBegin( view, proj, cinematic );
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !renderStream.isBox[x] )
            {
                SkullbonezHelper::DrawShadowDepthSphereBatchModel( renderStream.modelMatrices[x] );
            }
        }
        SkullbonezHelper::DrawShadowDepthSphereBatchEnd();
    }

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !renderStream.isBox[x] )
            {
                continue;
            }
            const bool isPineVisual = IsPineVisualMaterial( models[x].GetRenderMaterial() );
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
                SkullbonezHelper::DrawShadowDepthPineBatchModel( renderStream.modelMatrices[x] );
            }
            else
            {
                SkullbonezHelper::DrawShadowDepthBoxBatchModel( renderStream.modelMatrices[x] );
            }
        }
    };

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/Boxes" );

        SkullbonezHelper::DrawShadowDepthBoxBatchBegin( view, proj );
        appendBoxLikeModels( false );
        SkullbonezHelper::DrawShadowDepthBoxBatchEnd();
    }

    if ( hasPineVisualModels )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/Pines" );

        SkullbonezHelper::DrawShadowDepthPineBatchBegin( view, proj );
        appendBoxLikeModels( true );
        SkullbonezHelper::DrawShadowDepthPineBatchEnd();
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
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float minZ = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;
    float maxZ = -FLT_MAX;
    bool found = false;

    for ( int i = 0; i < modelCount; ++i )
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

        minX = (std::min)( minX, pos.x - radius );
        minY = (std::min)( minY, pos.y - radius );
        minZ = (std::min)( minZ, pos.z - radius );
        maxX = (std::max)( maxX, pos.x + radius );
        maxY = (std::max)( maxY, pos.y + radius );
        maxZ = (std::max)( maxZ, pos.z + radius );
        found = true;
    }

    if ( !found )
    {
        return false;
    }

    outCenter = Vector3( ( minX + maxX ) * 0.5f,
                         ( minY + maxY ) * 0.5f,
                         ( minZ + maxZ ) * 0.5f );

    const float halfX = ( maxX - minX ) * 0.5f;
    const float halfY = ( maxY - minY ) * 0.5f;
    const float halfZ = ( maxZ - minZ ) * 0.5f;
    const float clusterRadius = sqrtf( halfX * halfX + halfY * halfY + halfZ * halfZ );
    const float padding = 36.0f;

    outRadius = std::clamp( clusterRadius + padding, 48.0f, queryDistance + padding );
    outHeightRange = (std::max)( maxY - minY + padding * 2.0f, 64.0f );
    return true;
}


void GameModelRenderer::ResetRenderResources()
{
}
