//
// File: SkullbonezTests/TestTerrain.cpp
// Purpose:
//   Locks real Terrain flat-slope collision queries used by focused physics tests.
//
// Summary:
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
//   - Terrain-stage detection touches only body indices supplied by the awake
//     list, leaving fixed and sleeping candidate slots untested.
//
// Related:
//   - SkullbonezSource/World/Terrain.cpp
//   - SkullbonezSource/Physics/TerrainContactManifold.cpp
//   - Agentic/Reports/2026-07-15/math-fatal-call-site-survey.md
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestRenderResourceDoubles.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>

using SkullbonezCore::Assets::AssetSystem;
using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsTerrainStage;
using SkullbonezCore::Physics::TerrainDetectionStageContext;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
PhysicsBodyStore& TerrainBodyStore()
{
    static PhysicsBodyStore store;
    store.Clear();
    return store;
}

ColliderStore& TerrainColliderStore()
{
    static ColliderStore store;
    store.Clear();
    return store;
}
} // namespace

TEST_CASE( "Terrain: flat slope reports analytic height, plane, and bounds" )
{
    SkullbonezCore::Core::EngineConfig config;
    config.worldForces.fluidHeight = 25.0f;
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
    CHECK( terrain.GetTerrainHeightAt( x, z, true ) == doctest::Approx( config.worldForces.fluidHeight ) );

    float planeHeight = 0.0f;
    Plane plane;
    terrain.GetTerrainHeightAndPlaneAt( x, z, planeHeight, plane );
    CHECK( planeHeight == doctest::Approx( expectedHeight ) );
    CHECK( plane.m_normal.y > 0.0f );
    CHECK( plane.m_distance == doctest::Approx( plane.m_normal.y * 10.0f ) );
}


TEST_CASE( "Terrain: collapsed height-map posts publish world-up render normals" )
{
    constexpr const char* kHeightMapPath = "TestOutput/terrain_degenerate_normals.raw";
    constexpr int kMapSize = 4;
    {
        std::ofstream heightMap( kHeightMapPath, std::ios::binary | std::ios::trunc );
        const char pixels[kMapSize * kMapSize] = {};
        heightMap.write( pixels, sizeof( pixels ) );
    }

    EngineConfig config;
    config.terrainGeometry.scale = 0.0f;
    config.terrainGeometry.renderStepSize = 1;
    AssetSystem assets;
    SkullbonezTests::NullRenderResourceFactory resources;
    std::unique_ptr<Terrain> terrain;

    const auto result =
        Terrain::TryCreateFromHeightMap( kHeightMapPath, kMapSize, 1, 1, config, assets, resources, terrain );
    std::remove( kHeightMapPath );

    REQUIRE( result.ok );
    REQUIRE( terrain != nullptr );
    REQUIRE( resources.LastMeshStride() == 8 );
    const auto& vertices = resources.LastMeshVertices();
    REQUIRE_FALSE( vertices.empty() );
    for ( size_t vertex = 0; vertex < vertices.size(); vertex += 8u )
    {
        CHECK( vertices[vertex + 3u] == doctest::Approx( 0.0f ) );
        CHECK( vertices[vertex + 4u] == doctest::Approx( 1.0f ) );
        CHECK( vertices[vertex + 5u] == doctest::Approx( 0.0f ) );
    }
}


TEST_CASE( "Physics terrain stage: candidate rows preserve model order and eligibility" )
{
    EngineConfig config;
    AssetSystem assets;
    SkullbonezTests::NullRenderResourceFactory resources;
    Terrain terrain( 0.0f, 0.0f, 0.0f, config, assets, resources );
    PhysicsBodyStore& bodies = TerrainBodyStore();
    ColliderStore& colliders = TerrainColliderStore();
    for ( int bodyIndex = 0; bodyIndex < 3; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.terrain = &terrain;
        body.hot.position = Vector3( static_cast<float>( bodyIndex ), 5.0f, 0.0f );
        body.hot.fixed = bodyIndex == 1;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.shape = CollisionShape( BoundingSphere( 1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ) );
        collider.boundingRadius = 1.0f;
        colliders.CreateColliderRecord( collider );
    }
    const std::array<uint8_t, 3> sleepState = { 0u, 0u, 1u };
    const std::array<float, 3> timeRemaining = { 0.5f, 0.5f, 0.5f };
    const TerrainDetectionStageContext context{ bodies.Records(),
                                                bodies.HotFields(),
                                                colliders.Records(),
                                                config,
                                                sleepState,
                                                timeRemaining };
    SkullbonezCore::Core::PhysicsExecutionConfig execution;
    execution.parallel = false;
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    PhysicsTerrainStage stage;
    const std::array<int, 1> awakeBodyIndices = { 0 };

    stage.Detect( context, 3, awakeBodyIndices, execution, inlinePool );

    const auto candidates = stage.GetDetectionCandidates();
    REQUIRE( candidates.size() == 3u );
    CHECK( candidates[0].tested == 1u );
    CHECK( candidates[0].availableTime == doctest::Approx( 0.5f ) );
    CHECK( candidates[1].tested == 0u );
    CHECK( candidates[2].tested == 0u );
}
