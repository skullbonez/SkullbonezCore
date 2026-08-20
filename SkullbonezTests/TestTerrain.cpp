//
// File: SkullbonezTests/TestTerrain.cpp
// Purpose:
//   Locks real Terrain flat-slope collision queries used by focused physics tests.
//
// Summary:
//   Terrain owns both render resources and collision lookup data. Unit tests use
//   render-resource doubles so construction follows the production path while
//   assertions stay on CPU-side height, plane, bounds, and shape-consistent
//   sphere support behavior. Height-map factory cases also prove malformed
//   dimensions fail before construction or output publication.
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
//   - Scalar height queries outside those bounds return the lowest finite float
//     even when fluid-min support is requested.
//   - Terrain-stage detection touches only body indices supplied by the awake
//     list, leaving fixed and sleeping candidate slots untested.
//   - Sphere sweep and manifold construction use the same terrain-normal pole;
//     box and convex-hull sweeps use their exact vertices.
//   - Every terrain shape publishes finite manifolds with terrain-owned body
//     identity and resting-contact policy.
//   - Height-map dimensions are positive, exactly divisible, and large enough
//     for one quad before pixel/post/quad storage is constructed.
//
// Related:
//   - SkullbonezSource/World/Terrain.cpp
//   - SkullbonezSource/Physics/TerrainContactManifold.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "TestResultLoadFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"

#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/TerrainContactManifold.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestCollisionShapeFixtures.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Assets::AssetSystem;
using SkullbonezCore::Core::EngineConfig;
using SkullbonezCore::Geometry::Plane;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildTerrainContactManifold;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsTerrainStage;
using SkullbonezCore::Physics::SweepTerrainContact;
using SkullbonezCore::Physics::TerrainContactBodyView;
using SkullbonezCore::Physics::TerrainContactManifold;
using SkullbonezCore::Physics::TerrainContactSweepResult;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;
using SkullbonezTests::CollisionShapeFixtures::BoxShape;
using SkullbonezTests::CollisionShapeFixtures::SphereShape;

namespace
{
PhysicsBodyStore& TerrainBodyStore()
{
    static PhysicsBodyStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }
    store.Clear();
    return store;
}

ColliderStore& TerrainColliderStore()
{
    static ColliderStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        store.ReserveShapeCapacity( 16u, 0u, 0u );
    }
    store.Clear();
    return store;
}

void ExpectHeightMapShapeFailure( int mapSize, int stepSize, const char* expectedMessage )
{
    EngineConfig config;
    std::unique_ptr<Terrain> terrain = std::make_unique<Terrain>( 0.0f, 0.0f, 0.0f, config );
    Terrain* const originalTerrain = terrain.get();
    const auto result = Terrain::TryCreatePhysicsFromHeightMap( diagnostics, "unused-height-map.raw", mapSize, stepSize, 1,
                                                                config, terrain );

    CHECK_FALSE( result.Ok() );
    CHECK( std::strcmp( result.ErrorOwner(), "World/Terrain" ) == 0 );
    CHECK( std::strstr( result.ErrorMessage(), expectedMessage ) != nullptr );
    CHECK( terrain.get() == originalTerrain );
}

} // namespace

TEST_CASE( "Terrain: invalid height-map dimensions fail before construction" )
{
    // Lane P: the retired floor-sized allocation reserved four posts for the
    // 5/2 shape while its ceiling-shaped translation loops wrote nine. This
    // local fixture keeps the old false-pass mechanism observable without
    // executing an out-of-bounds write.
    constexpr int retiredAllocatedPosts = ( 5 / 2 ) * ( 5 / 2 );
    constexpr int retiredTranslatedPosts = ( ( 5 + 2 - 1 ) / 2 ) * ( ( 5 + 2 - 1 ) / 2 );
    CHECK( retiredAllocatedPosts == 4 );
    CHECK( retiredTranslatedPosts == 9 );
    CHECK( retiredTranslatedPosts > retiredAllocatedPosts );

    ExpectHeightMapShapeFailure( 0, 1, "size must be positive" );
    ExpectHeightMapShapeFailure( -4, 1, "size must be positive" );
    ExpectHeightMapShapeFailure( 4, 0, "step size must be positive" );
    ExpectHeightMapShapeFailure( 4, -1, "step size must be positive" );
    ExpectHeightMapShapeFailure( 1, 1, "at least two posts" );
    ExpectHeightMapShapeFailure( 4, 4, "at least two posts" );
    ExpectHeightMapShapeFailure( 5, 2, "must be divisible" );
    ExpectHeightMapShapeFailure( ( std::numeric_limits<int>::max )(), 1, "pixel count exceeds" );
}


TEST_CASE( "Terrain: exact-minimum height map publishes one checked quad" )
{
    constexpr const char* kHeightMapPath = "TestOutput/terrain_exact_minimum.raw";
    constexpr int kMapSize = 2;
    {
        std::ofstream heightMap( kHeightMapPath, std::ios::binary | std::ios::trunc );
        const char pixels[kMapSize * kMapSize] = {};
        heightMap.write( pixels, sizeof( pixels ) );
    }

    EngineConfig config;
    std::unique_ptr<Terrain> terrain;
    const auto result = Terrain::TryCreatePhysicsFromHeightMap( diagnostics, kHeightMapPath, kMapSize, 1, 1, config,
                                                                terrain );

    std::remove( kHeightMapPath );

    REQUIRE( result.Ok() );
    REQUIRE( terrain != nullptr );
    const auto view = terrain->PhysicsView();
    CHECK( view.quadsPerSide == 1 );
    CHECK( view.cells.size() == 1u );
    CHECK( terrain->BuildRenderVertexData().size() == 48u );
}

TEST_CASE( "Terrain: flat slope reports analytic height, plane, and bounds" )
{
    SkullbonezCore::Core::EngineConfig config;
    config.worldForces.fluidHeight = 25.0f;
    Terrain terrain( 10.0f, 0.25f, -0.1f, config );

    CHECK( terrain.IsInBounds( 0.0f, 0.0f ) );
    CHECK( terrain.IsInBounds( Terrain::FLAT_SLOPE_EXTENT - 0.01f, Terrain::FLAT_SLOPE_EXTENT - 0.01f ) );
    CHECK_FALSE( terrain.IsInBounds( -0.01f, 0.0f ) );
    CHECK_FALSE( terrain.IsInBounds( Terrain::FLAT_SLOPE_EXTENT, 0.0f ) );

    const float noTerrainHeight = -( std::numeric_limits<float>::max )();
    CHECK( terrain.GetTerrainHeightAt( -0.01f, 0.0f, false ) == noTerrainHeight );
    CHECK( terrain.GetTerrainHeightAt( -0.01f, 0.0f, true ) == noTerrainHeight );
    CHECK( terrain.GetTerrainHeightAt( Terrain::FLAT_SLOPE_EXTENT, 0.0f, false ) == noTerrainHeight );
    CHECK( terrain.GetTerrainHeightAt( Terrain::FLAT_SLOPE_EXTENT, 0.0f, true ) == noTerrainHeight );

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


TEST_CASE( "Terrain: resting sphere sweep uses the same slope-normal pole as its manifold" )
{
    EngineConfig config;
    Terrain terrain( 0.0f, 0.25f, -0.1f, config );
    constexpr float radius = 1.0f;
    constexpr float surfaceX = 20.0f;
    constexpr float surfaceZ = 30.0f;
    float surfaceHeight = 0.0f;
    Plane plane;
    terrain.GetTerrainHeightAndPlaneAt( surfaceX, surfaceZ, surfaceHeight, plane );

    TerrainContactBodyView body;
    body.position = Vector3( surfaceX, surfaceHeight, surfaceZ ) + plane.m_normal * radius;
    body.terrain = terrain.PhysicsView();
    body.boundingRadius = radius;
    body.contactEpsilon = 0.001f;
    const CollisionShape sphere = SphereShape( radius );

    const TerrainContactSweepResult sweep = SweepTerrainContact( body, sphere, 1.0f / 120.0f );
    REQUIRE( sweep.hit );
    CHECK( sweep.collisionTime == 0.0f );

    TerrainContactManifold manifold;
    REQUIRE( BuildTerrainContactManifold( body, sphere, 0, sweep, 1.0f / 120.0f, manifold ) );
    REQUIRE( manifold.pointCount == 1u );
    CHECK( manifold.points[0].penetration == doctest::Approx( 0.0f ).epsilon( 0.0001 ) );
    CHECK( manifold.supportsRestingPolicy );

    // A moving sphere must solve the same pole/plane equation at a nonzero
    // time, not merely inherit the resting fast path above.
    TerrainContactBodyView movingBody = body;
    movingBody.position = movingBody.position + plane.m_normal * 0.5f;
    movingBody.linearVelocity = plane.m_normal * -1.0f;
    const TerrainContactSweepResult movingSweep = SweepTerrainContact( movingBody, sphere, 1.0f );
    REQUIRE( movingSweep.hit );
    CHECK( movingSweep.collisionTime == doctest::Approx( 0.5f ).epsilon( 0.0001 ) );

    TerrainContactManifold movingManifold;
    REQUIRE( BuildTerrainContactManifold( movingBody, sphere, 0, movingSweep, 1.0f, movingManifold ) );
    CHECK( movingManifold.sweptHit );
    CHECK( movingManifold.normal.x == doctest::Approx( plane.m_normal.x ) );
    CHECK( movingManifold.normal.y == doctest::Approx( plane.m_normal.y ) );
    CHECK( movingManifold.normal.z == doctest::Approx( plane.m_normal.z ) );
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
    std::unique_ptr<Terrain> terrain;

    const auto result = Terrain::TryCreatePhysicsFromHeightMap( diagnostics, kHeightMapPath, kMapSize, 1, 1, config,
                                                                terrain );

    std::remove( kHeightMapPath );

    REQUIRE( result.Ok() );
    REQUIRE( terrain != nullptr );
    const std::vector<float> vertices = terrain->BuildRenderVertexData();
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
    SkullbonezCore::Physics::PhysicsRuntimeSettings physicsSettings;
    Terrain terrain( 0.0f, 0.0f, 0.0f, config );
    PhysicsBodyStore& bodies = TerrainBodyStore();
    ColliderStore& colliders = TerrainColliderStore();

    for ( int bodyIndex = 0; bodyIndex < 3; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.hot.position = Vector3( static_cast<float>( bodyIndex ), 5.0f, 0.0f );
        body.hot.fixed = bodyIndex == 1;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        const CollisionShape shape( BoundingSphere( 1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ) );
        collider.boundingRadius = 1.0f;
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shape );
    }

    const std::array<uint8_t, 3> sleepState = { 0u, 0u, 1u };
    const std::array<float, 3> timeRemaining = { 0.5f, 0.5f, 0.5f };
    const std::array<SkullbonezCore::Physics::BuoyancyBodyFacts, 3> buoyancyFacts;
    SkullbonezCore::Physics::PhysicsExecutionSettings execution;
    execution.parallel = false;
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    PhysicsTerrainStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveSceneCapacity( 3u );
    }
    const std::array<int, 1> awakeBodyIndices = { 0 };

    stage.Detect( bodies, colliders, buoyancyFacts, terrain.PhysicsView(), physicsSettings, sleepState, timeRemaining,
                  nullptr, awakeBodyIndices, execution, inlinePool );

    const auto candidates = stage.GetDetectionCandidates();
    REQUIRE( candidates.size() == 3u );
    CHECK( candidates[0].tested == 1u );
    CHECK( candidates[0].availableTime == doctest::Approx( 0.5f ) );
    CHECK( candidates[1].tested == 0u );
    CHECK( candidates[2].tested == 0u );
}


TEST_CASE( "Coverage floor contract: terrain sweep and manifold support every collision shape" )
{
    EngineConfig config;
    Terrain terrain( 0.0f, 0.0f, 0.0f, config );
    SkullbonezCore::Math::CollisionDetection::ConvexHullShape hull;
    REQUIRE(
        SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics, "SkullbonezData/hulls/pyramid.hull", hull ) );
    const CollisionShape shapes[] = {
        SphereShape( 1.0f ),
        BoxShape( Vector3( 1.0f, 1.0f, 1.0f ) ),
        hull,
    };

    const float centerHeights[] = { 4.0f, 4.0f, 5.0f };

    for ( int index = 0; index < 3; ++index )
    {
        TerrainContactBodyView body;
        body.position = Vector3( 20.0f, centerHeights[index], 20.0f );
        body.linearVelocity = Vector3( 0.0f, -5.0f, 0.0f );
        body.terrain = terrain.PhysicsView();
        body.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( shapes[index] );
        body.contactEpsilon = 0.05f;
        body.terrainContactThreshold = 0.15f;
        body.restitutionThreshold = 2.0f;
        const TerrainContactSweepResult sweep = SweepTerrainContact( body, shapes[index], 2.0f );
        REQUIRE( sweep.hit );
        CHECK( sweep.collisionTime >= 0.0f );
        CHECK( sweep.collisionTime <= 2.0f );
        TerrainContactManifold manifold;
        REQUIRE( BuildTerrainContactManifold( body, shapes[index], index, sweep, 2.0f, manifold ) );
        CHECK( manifold.bodyA == index );
        CHECK( manifold.bodyB == -1 );
        REQUIRE( manifold.pointCount > 0u );
        CHECK( manifold.sweptHit );
        CHECK( std::isfinite( manifold.tangent1.x ) );
        CHECK( std::isfinite( manifold.tangent2.z ) );
    }

    // A zero-time hit exercises the resting patch rather than the fast-impact
    // centroid reduction used by the swept cases above.
    TerrainContactBodyView resting;
    resting.position = Vector3( 20.0f, 1.0f, 20.0f );
    resting.terrain = terrain.PhysicsView();
    resting.terrainContactThreshold = 0.15f;
    resting.contactEpsilon = 0.05f;
    TerrainContactSweepResult restingSweep;
    restingSweep.hit = true;
    restingSweep.collisionTime = 0.0f;
    restingSweep.collidedPlane.m_normal = Vector3( 0.0f, 1.0f, 0.0f );
    restingSweep.collidedPlane.m_distance = 0.0f;
    TerrainContactManifold restingManifold;
    REQUIRE( BuildTerrainContactManifold( resting, shapes[1], 9, restingSweep, 1.0f / 120.0f, restingManifold ) );
    CHECK( restingManifold.pointCount == 4u );
    CHECK( restingManifold.supportsRestingPolicy );
    CHECK( restingManifold.allowsTangentFriction );
    CHECK_FALSE( restingManifold.inhibitsSleep );
}
