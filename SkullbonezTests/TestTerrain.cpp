//
// File: SkullbonezTests/TestTerrain.cpp
// Purpose:
//   Locks real Terrain flat-slope collision queries used by focused physics tests.
//
// Mental model:
//   Terrain owns both render resources and collision lookup data. Unit tests use
//   render-resource doubles so construction follows the production path while
//   assertions stay on CPU-side height, plane, and bounds behavior.
//
// Glossary:
//   Flat slope terrain: Analytic terrain plane defined by base Y plus X/Z slope
//     coefficients.
//   Collision query: Terrain height/plane lookup consumed by physics and camera
//     bounds code.
//   Render-resource double: Test-owned no-op shader/mesh factory used only to
//     keep backend setup out of this unit harness.
//
// Invariants:
//   - Flat-slope height must match `baseY + slopeX*x + slopeZ*z`.
//   - Bounds exclude the exact upper edge so quad lookup never selects a
//     non-existent terrain cell.
//
// Related:
//   - SkullbonezSource/World/Terrain.cpp
//   - SkullbonezSource/Physics/TerrainContactManifold.cpp
//   - engine-cleanup-plans/05-behavioral-test-coverage.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestRenderResourceDoubles.h"

using SkullbonezCore::Assets::AssetSystem;
using SkullbonezCore::Basics::EngineConfig;
using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Geometry::Terrain;

TEST_CASE( "Terrain: flat slope reports analytic height, plane, and bounds" )
{
    EngineConfig config;
    config.fluidHeight = 25.0f;
    AssetSystem assets;
    SkullbonezTests::NullRenderResourceFactory resources;
    Terrain terrain( 10.0f, 0.25f, -0.1f, config, assets, resources );

    CHECK( terrain.IsInBounds( 0.0f, 0.0f ) );
    CHECK( terrain.IsInBounds( Terrain::FLAT_SLOPE_EXTENT - 0.01f, Terrain::FLAT_SLOPE_EXTENT - 0.01f ) );
    CHECK_FALSE( terrain.IsInBounds( -0.01f, 0.0f ) );
    CHECK_FALSE( terrain.IsInBounds( Terrain::FLAT_SLOPE_EXTENT, 0.0f ) );

    const float x = 20.0f;
    const float z = 30.0f;
    const float expectedHeight = 10.0f + 0.25f * x - 0.1f * z;
    CHECK( terrain.GetTerrainHeightAt( x, z, false ) == doctest::Approx( expectedHeight ) );
    CHECK( terrain.GetTerrainHeightAt( x, z, true ) == doctest::Approx( config.fluidHeight ) );

    float planeHeight = 0.0f;
    Plane plane;
    terrain.GetTerrainHeightAndPlaneAt( x, z, planeHeight, plane );
    CHECK( planeHeight == doctest::Approx( expectedHeight ) );
    CHECK( plane.m_normal.y > 0.0f );
    CHECK( plane.m_distance == doctest::Approx( plane.m_normal.y * 10.0f ) );
}
