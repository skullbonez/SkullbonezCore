//
// File: SkullbonezTests/TestTerrainLinkStubs.cpp
// Purpose:
//   Provide deterministic unit-test Terrain stubs for focused physics tests.
//
// Mental model:
//   The unit harness links selected runtime translation units directly. Some
//   focused tests need a real terrain query shape without constructing the
//   render-backed Terrain implementation. The stubbed Terrain instance behaves
//   as a flat analytic plane while render/resource paths stay outside this
//   focused harness.
//
// Glossary:
//   Stub terrain: Test-only method definitions that satisfy Terrain references
//     while returning a deterministic flat surface for physics queries.
//   Flat plane: y=0 terrain with an upward normal and the standard flat-slope
//     X/Z bounds from Terrain.
//
// Invariants:
//   - These methods are test-only link stubs, not runtime Terrain.
//   - Only collision/height queries are supported; render-resource entrypoints
//     remain outside this focused harness.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsBodyStore.cpp
//   - SkullbonezSource/World/Terrain.h
//   - fable_plans/01-unit-test-pyramid-progress.md
//

#include "../SkullbonezSource/World/Terrain.h"

namespace SkullbonezCore
{
namespace Geometry
{

Terrain::Terrain( float,
                  float,
                  float,
                  const Basics::EngineConfig& config,
                  Assets::AssetSystem& assets,
                  Rendering::IRenderResourceFactory& resources )
    : displayListReference( 0 ),
      m_mapSize( 0 ),
      m_stepSize( 0 ),
      m_renderStepSize( 0 ),
      m_renderPostsPerSide( 0 ),
      m_textureWrap( 0 ),
      m_postsPerSide( 0 ),
      m_terrainSizeWorldCoords( 0 ),
      m_maxTerrainHeight( 0.0f ),
      m_minTerrainHeight( 0.0f ),
      m_config( &config ),
      m_assets( &assets ),
      m_resources( &resources ),
      m_isFlatSlope( true ),
      m_slopeBaseY( 0.0f ),
      m_slopeX( 0.0f ),
      m_slopeZ( 0.0f ),
      m_flatSlopeNormal( 0.0f, 1.0f, 0.0f )
{
    m_flatSlopePlane.m_normal = m_flatSlopeNormal;
    m_flatSlopePlane.m_distance = 0.0f;
}

Terrain::~Terrain() = default;

bool Terrain::IsInBounds( float xPosition, float zPosition )
{
    return xPosition >= 0.0f && xPosition < FLAT_SLOPE_EXTENT && zPosition >= 0.0f &&
           zPosition < FLAT_SLOPE_EXTENT;
}

float Terrain::GetTerrainHeightAt( float, float, bool isFluidMin )
{
    if ( isFluidMin && m_config )
    {
        return ( 0.0f < m_config->fluidHeight ) ? m_config->fluidHeight : 0.0f;
    }
    return 0.0f;
}

void Terrain::GetTerrainHeightAndPlaneAt( float, float, float& outHeight, Plane& outPlane )
{
    outHeight = 0.0f;
    outPlane.m_normal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    outPlane.m_distance = 0.0f;
}
} // namespace Geometry
} // namespace SkullbonezCore
