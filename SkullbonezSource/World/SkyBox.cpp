/*
File: SkullbonezSource/World/SkyBox.cpp
Purpose:
  Builds and renders the skybox or sky backdrop for scene rendering.

Summary:
  SkyBox.cpp builds and renders the skybox or sky backdrop for scene
  rendering. As an implementation unit, keep edits anchored on world-state
  ownership, terrain/environment data, and physics/render handoff and on the
  glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - SkyBox owns renderer-facing face meshes and shader resources for the active
    backend, but it borrows the texture registry.
  - Face order and texture hashes must stay paired so cube sides do not swap
    during renderer rebuilds.

Related:
  - SkullbonezSource/World/SkyBox.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkyBox.h"
#include "../Assets/AssetKeys.h"
#include "../Core/Config.h"
#include "../Runtime/WindowConstants.h"
#include "../Assets/AssetSystem.h"
#include "../Rendering/IRenderResourceFactory.h"
#include <vector>


using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Textures;
namespace Runtime = SkullbonezCore::Runtime;


SkyBox::SkyBox( int m_xMin, int m_xMax, int yMin, int yMax, int m_zMin, int m_zMax )
{
    m_boundaries.m_xMin = m_xMin;
    m_boundaries.m_xMax = m_xMax;
    m_boundaries.yMin = yMin;
    m_boundaries.yMax = yMax;
    m_boundaries.m_zMin = m_zMin;
    m_boundaries.m_zMax = m_zMax;
    m_textures = 0;
    m_config = nullptr;
    m_assets = nullptr;
    m_resources = nullptr;
}


SkullbonezCore::Core::SbResult SkyBox::LoadTextures( const SkullbonezCore::Core::EngineConfig& cfg )
{
    assert( m_textures );
    const SkullbonezCore::Core::SbResult leftResult =
        m_textures->EnsureJpegTexture( ( std::string( DATA_ROOT ) + cfg.assetPaths.skyLeft ).c_str(),
                                       TEXTURE_SKY_LEFT );
    if ( !leftResult.ok )
    {
        return leftResult;
    }
    const SkullbonezCore::Core::SbResult rightResult =
        m_textures->EnsureJpegTexture( ( std::string( DATA_ROOT ) + cfg.assetPaths.skyRight ).c_str(),
                                       TEXTURE_SKY_RIGHT );
    if ( !rightResult.ok )
    {
        return rightResult;
    }
    const SkullbonezCore::Core::SbResult frontResult =
        m_textures->EnsureJpegTexture( ( std::string( DATA_ROOT ) + cfg.assetPaths.skyFront ).c_str(),
                                       TEXTURE_SKY_FRONT );
    if ( !frontResult.ok )
    {
        return frontResult;
    }
    const SkullbonezCore::Core::SbResult backResult =
        m_textures->EnsureJpegTexture( ( std::string( DATA_ROOT ) + cfg.assetPaths.skyBack ).c_str(),
                                       TEXTURE_SKY_BACK );
    if ( !backResult.ok )
    {
        return backResult;
    }
    const SkullbonezCore::Core::SbResult upResult =
        m_textures->EnsureJpegTexture( ( std::string( DATA_ROOT ) + cfg.assetPaths.skyUp ).c_str(), TEXTURE_SKY_UP );
    if ( !upResult.ok )
    {
        return upResult;
    }
    const SkullbonezCore::Core::SbResult downResult =
        m_textures->EnsureJpegTexture( ( std::string( DATA_ROOT ) + cfg.assetPaths.skyDown ).c_str(),
                                       TEXTURE_SKY_DOWN );
    if ( !downResult.ok )
    {
        return downResult;
    }
    return SkullbonezCore::Core::SbResult::Success();
}


void SkyBox::BuildMeshes( const SkullbonezCore::Core::EngineConfig& cfg,
                          SkullbonezCore::Assets::AssetSystem& assets,
                          IRenderResourceFactory& resources )
{
    // Shorthand for boundary values with overflow
    const int overflow = cfg.skybox.overflow;
    float xn = static_cast<float>( m_boundaries.m_xMin - overflow );
    float xp = static_cast<float>( m_boundaries.m_xMax + overflow );
    float yn = static_cast<float>( m_boundaries.yMin - overflow );
    float yp = static_cast<float>( m_boundaries.yMax + overflow );
    float zn = static_cast<float>( m_boundaries.m_zMin - overflow );
    float zp = static_cast<float>( m_boundaries.m_zMax + overflow );
    float yMinF = static_cast<float>( m_boundaries.yMin );
    float yMaxF = static_cast<float>( m_boundaries.yMax );
    float xMinF = static_cast<float>( m_boundaries.m_xMin );
    float xMaxF = static_cast<float>( m_boundaries.m_xMax );
    float zMinF = static_cast<float>( m_boundaries.m_zMin );
    float zMaxF = static_cast<float>( m_boundaries.m_zMax );

    // Each face: 2 triangles = 6 vertices, 5 floats each (pos3 + tex2)
    // Face order: up, down, right(west), left(east), front, back

    // UP face (y = yMax)
    float up[] = {
        xn, yMaxF, zp, 0, 1, xn, yMaxF, zn, 0, 0, xp, yMaxF, zn, 1, 0,
        xn, yMaxF, zp, 0, 1, xp, yMaxF, zn, 1, 0, xp, yMaxF, zp, 1, 1,
    };

    // DOWN face (y = yMin)
    float down[] = {
        xp, yMinF, zp, 1, 0, xp, yMinF, zn, 1, 1, xn, yMinF, zn, 0, 1,
        xp, yMinF, zp, 1, 0, xn, yMinF, zn, 0, 1, xn, yMinF, zp, 0, 0,
    };

    // RIGHT/WEST face (x = m_xMin)
    float right[] = {
        xMinF, yn, zp, 1, 1, xMinF, yn, zn, 0, 1, xMinF, yp, zn, 0, 0,
        xMinF, yn, zp, 1, 1, xMinF, yp, zn, 0, 0, xMinF, yp, zp, 1, 0,
    };

    // LEFT/EAST face (x = m_xMax)
    float left[] = {
        xMaxF, yn, zn, 1, 1, xMaxF, yn, zp, 0, 1, xMaxF, yp, zp, 0, 0,
        xMaxF, yn, zn, 1, 1, xMaxF, yp, zp, 0, 0, xMaxF, yp, zn, 1, 0,
    };

    // FRONT face (z = m_zMax)
    float front[] = {
        xp, yn, zMaxF, 1, 1, xn, yn, zMaxF, 0, 1, xn, yp, zMaxF, 0, 0,
        xp, yn, zMaxF, 1, 1, xn, yp, zMaxF, 0, 0, xp, yp, zMaxF, 1, 0,
    };

    // BACK face (z = m_zMin)
    float back[] = {
        xn, yn, zMinF, 1, 1, xp, yn, zMinF, 0, 1, xp, yp, zMinF, 0, 0,
        xn, yn, zMinF, 1, 1, xp, yp, zMinF, 0, 0, xn, yp, zMinF, 1, 0,
    };

    float* faceData[] = { up, down, right, left, front, back };
    m_faceTextures =
        { TEXTURE_SKY_UP, TEXTURE_SKY_DOWN, TEXTURE_SKY_RIGHT, TEXTURE_SKY_LEFT, TEXTURE_SKY_FRONT, TEXTURE_SKY_BACK };

    for ( int i = 0; i < 6; ++i )
    {
        m_faceMeshes[i] = resources.CreateMesh( faceData[i], 6, false, true );
    }

    m_shader = assets.CreateShader( resources, "shader.unlit_textured" );
    if ( !m_shader )
    {
        return;
    }
    m_shader->Use();
    m_shader->SetMat4( "uModel", Matrix4() );
    m_shader->SetVec4( "uColorTint", 1.0f, 1.0f, 1.0f, 1.0f );
}


void SkyBox::BindTextures( TextureCollection& textures )
{
    // Lifetime: Run owns the texture collection; skybox only borrows it between
    // Initialise and backend teardown/rebuild.
    m_textures = &textures;
}


void SkyBox::BindRenderContexts( const SkullbonezCore::Core::EngineConfig& config,
                                 SkullbonezCore::Assets::AssetSystem& assets,
                                 IRenderResourceFactory& resources )
{
    // Lifetime: sky resources rebuild during backend init/reset while all three
    // borrows are owned by Run.
    m_config = &config;
    m_assets = &assets;
    m_resources = &resources;
}


SkullbonezCore::Core::SbResult SkyBox::ResetRenderResources()
{
    for ( int i = 0; i < 6; ++i )
    {
        m_faceMeshes[i].reset();
    }
    m_shader.reset();
    assert( m_textures );
    assert( m_config );
    assert( m_assets );
    assert( m_resources );
    const SkullbonezCore::Core::SbResult textureResult = LoadTextures( *m_config );
    if ( !textureResult.ok )
    {
        return textureResult;
    }
    BuildMeshes( *m_config, *m_assets, *m_resources );
    return SkullbonezCore::Core::SbResult::Success();
}


void SkyBox::ReleaseRenderResources()
{
    for ( int i = 0; i < 6; ++i )
    {
        m_faceMeshes[i].reset();
    }
    m_shader.reset();
    m_textures = nullptr;
    m_config = nullptr;
    m_assets = nullptr;
    m_resources = nullptr;
}


SkullbonezCore::Core::SbResult SkyBox::Render( const Matrix4& view, const Matrix4& proj )
{
    if ( !m_shader )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/SkyBox", "Skybox shader is unavailable." );
    }
    m_shader->Use();
    m_shader->SetMat4( "uView", view );
    m_shader->SetMat4( "uProjection", proj );

    for ( int i = 0; i < 6; ++i )
    {
        if ( !m_faceMeshes[i] )
        {
            return SkullbonezCore::Core::SbResult::Failure( "Rendering/SkyBox",
                                                            "Skybox face mesh %d is unavailable.",
                                                            i );
        }
        const SkullbonezCore::Core::SbResult textureResult = m_textures->SelectTexture( m_faceTextures[i] );
        if ( !textureResult.ok )
        {
            return textureResult;
        }
        m_faceMeshes[i]->Draw();
    }
    return SkullbonezCore::Core::SbResult::Success();
}
