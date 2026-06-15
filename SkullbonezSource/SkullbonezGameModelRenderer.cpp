#include "SkullbonezGameModelRenderer.h"

#include "SkullbonezConfig.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezHelper.h"
#include "SkullbonezProfiler.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Rendering::ShadowFrameData;

namespace
{
constexpr int PINE_VISUAL_MATERIAL_MODE = 13;

bool IsPineVisualMaterial( float colorOverride )
{
    return colorOverride > 1.25f && static_cast<int>( std::floor( colorOverride + 0.5f ) ) == PINE_VISUAL_MATERIAL_MODE;
}
} // namespace


void GameModelRenderer::RenderModels( GameModelCollection& collection, const Matrix4& view, const Matrix4& proj, const float lightPos[4], const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow, float materialAlpha )
{
    auto& m_gameModels = collection.m_gameModels;
    auto& m_soaIsBox = collection.m_soaCache.isBox;
    auto& m_soaIsFixed = collection.m_soaCache.isFixed;
    auto& m_soaModelMatrices = collection.m_soaCache.modelMatrices;

    if ( m_gameModels.empty() )
    {
        return;
    }

    collection.EnsureSoAModelMatrices();
    const int modelCount = static_cast<int>( m_gameModels.size() );
    const float clampedMaterialAlpha = std::clamp( materialAlpha, 0.0f, 1.0f );
    const bool transparentMaterial = Cfg().runtimeRender.renderCollisionVolumes || clampedMaterialAlpha < 1.0f;

    SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, transparentMaterial, cinematic, shadow, clampedMaterialAlpha );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( !m_soaIsBox[x] )
        {
            float tintR = 1.0f;
            float tintG = 1.0f;
            float tintB = 1.0f;
            float colorOverride = 0.0f;
            m_gameModels[x].GetRenderTint( tintR, tintG, tintB, colorOverride );
            if ( m_soaIsFixed[x] )
            {
                const float hit = m_gameModels[x].GetFixedContactHighlightAlpha();
                if ( hit > 0.0f )
                {
                    tintR = tintR + ( 1.0f - tintR ) * hit;
                    tintG = tintG * ( 1.0f - hit );
                    tintB = tintB * ( 1.0f - hit );
                }
            }
            SkullbonezHelper::DrawSphereBatchModel( m_soaModelMatrices[x], tintR, tintG, tintB, colorOverride );
        }
    }
    SkullbonezHelper::DrawSphereBatchEnd();

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( m_soaIsBox[x] )
            {
                float tintR = 1.0f;
                float tintG = 1.0f;
                float tintB = 1.0f;
                float colorOverride = 0.0f;
                m_gameModels[x].GetRenderTint( tintR, tintG, tintB, colorOverride );
                const bool isPineVisual = IsPineVisualMaterial( colorOverride );
                if ( isPineVisual )
                {
                    hasPineVisualModels = true;
                }
                if ( isPineVisual != pineVisualPass )
                {
                    continue;
                }
                if ( m_soaIsFixed[x] )
                {
                    float hit = m_gameModels[x].GetFixedContactHighlightAlpha();
                    if ( colorOverride <= 0.5f && colorOverride >= -0.5f )
                    {
                        constexpr float fixedBase = 241.0f / 255.0f;
                        tintR = fixedBase + ( 1.0f - fixedBase ) * hit;
                        tintG = fixedBase * ( 1.0f - hit );
                        tintB = fixedBase * ( 1.0f - hit );
                        colorOverride = 1.0f;
                    }
                    else if ( hit > 0.0f )
                    {
                        tintR = tintR + ( 1.0f - tintR ) * hit;
                        tintG = tintG * ( 1.0f - hit );
                        tintB = tintB * ( 1.0f - hit );
                    }
                }
                if ( pineVisualPass )
                {
                    SkullbonezHelper::DrawPineBatchModel( m_soaModelMatrices[x], tintR, tintG, tintB, colorOverride );
                }
                else
                {
                    SkullbonezHelper::DrawBoxBatchModel( m_soaModelMatrices[x], tintR, tintG, tintB, colorOverride );
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

    auto& m_gameModels = collection.m_gameModels;
    auto& m_soaIsBox = collection.m_soaCache.isBox;
    auto& m_soaModelMatrices = collection.m_soaCache.modelMatrices;

    if ( m_gameModels.empty() )
    {
        return;
    }

    collection.EnsureSoAModelMatrices();
    const int modelCount = static_cast<int>( m_gameModels.size() );

    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches/Spheres" );

        SkullbonezHelper::DrawShadowDepthSphereBatchBegin( view, proj, cinematic );
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !m_soaIsBox[x] )
            {
                SkullbonezHelper::DrawShadowDepthSphereBatchModel( m_soaModelMatrices[x] );
            }
        }
        SkullbonezHelper::DrawShadowDepthSphereBatchEnd();
    }

    bool hasPineVisualModels = false;
    auto appendBoxLikeModels = [&]( bool pineVisualPass )
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            if ( !m_soaIsBox[x] )
            {
                continue;
            }
            float tintR = 1.0f;
            float tintG = 1.0f;
            float tintB = 1.0f;
            float colorOverride = 0.0f;
            m_gameModels[x].GetRenderTint( tintR, tintG, tintB, colorOverride );
            const bool isPineVisual = IsPineVisualMaterial( colorOverride );
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
                SkullbonezHelper::DrawShadowDepthPineBatchModel( m_soaModelMatrices[x] );
            }
            else
            {
                SkullbonezHelper::DrawShadowDepthBoxBatchModel( m_soaModelMatrices[x] );
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

    auto& m_gameModels = collection.m_gameModels;
    auto& m_soaPositions = collection.m_soaCache.positions;
    auto& m_soaBoundingRadii = collection.m_soaCache.boundingRadii;

    if ( m_gameModels.empty() )
    {
        return false;
    }

    if ( !collection.m_soaCache.bodyDataValid )
    {
        collection.RefreshSoABodyData();
    }

    const float queryDistance = (std::max)( maxDistance, 1.0f );
    const int modelCount = static_cast<int>( m_gameModels.size() );
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float minZ = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;
    float maxZ = -FLT_MAX;
    bool found = false;

    for ( int i = 0; i < modelCount; ++i )
    {
        const Vector3& pos = m_soaPositions[i];
        const float radius = m_soaBoundingRadii[i];
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
