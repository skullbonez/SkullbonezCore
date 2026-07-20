//
// File: SkullbonezTests/TestCoverageFloorContracts.cpp
// Purpose:
//   Exercise high-value physics and replay owner contracts needed by the armed
//   coverage gate.
//
// Summary:
//   The fixtures drive production replay checkpoint/event serialization from a
//   real one-body solver capture, exercise every object-manifold shape pair,
//   and drive the retained timeline through its value API. Assertions validate
//   owner-visible values and geometric invariants rather than private branches.
//
// Glossary:
//   Solver checkpoint: Sparse replay frame containing restorable body and
//     solver-world state.
//   Shape dispatch: Selection of the sphere, box, or convex-hull manifold path.
//   Manifold: Bounded contact-point set and normal for one colliding pair.
//   Buoyancy sample: Shape-volume estimate used to apply fluid force and drag.
//   Control artifact: Versioned replay file containing presentation, solver,
//     hash, branch, cursor, and owner-event tracks.
//   Retained timeline: Owner that advances presentation, solver, and event
//     histories under one retention policy.
//
// Invariants:
//   - A writer-made full artifact must round-trip through each public loader.
//   - Contact normals always point from body A toward body B.
//   - Swapping shape order preserves contact existence without reusing a
//     hard-coded private feature identifier.
//   - Physics force fixtures use a fixed 1/120-second tick and assert finite,
//     directionally meaningful results rather than golden internal values.
//
// Related:
//   - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp
//   - SkullbonezSource/Physics/ObjectContactManifold.cpp
//   - Agentic/Plans/TODO/unit-test-coverage-campaign.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ObjectContactManifold.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/TerrainContactManifold.h"
#include "../SkullbonezSource/Assets/AssetSystem.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayTimeline.h"
#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestRenderResourceDoubles.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuildObjectContactManifold;
using SkullbonezCore::Physics::BuildTerrainContactManifold;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::ObjectContactBodyView;
using SkullbonezCore::Physics::ObjectContactManifold;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsSceneObjectId;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Physics::SweepObjectContact;
using SkullbonezCore::Physics::SweepTerrainContact;
using SkullbonezCore::Physics::TerrainContactBodyView;
using SkullbonezCore::Physics::TerrainContactManifold;
using SkullbonezCore::Physics::TerrainContactSweepResult;
using namespace SkullbonezCore::Runtime;

namespace
{
CollisionShape SphereShape( float radius )
{
    return CollisionShape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
}

CollisionShape BoxShape( const Vector3& halfExtents )
{
    return CollisionShape( BoundingBox( halfExtents, Vector3( 0.0f, 0.0f, 0.0f ) ) );
}

void CheckContactPair( const ObjectContactBodyView& a,
                       const CollisionShape& shapeA,
                       const ObjectContactBodyView& b,
                       const CollisionShape& shapeB )
{
    ObjectContactManifold manifold;
    REQUIRE( BuildObjectContactManifold( a, shapeA, b, shapeB, 3, 7, 0.02f, manifold ) );
    REQUIRE( manifold.pointCount > 0u );
    CHECK( manifold.bodyA == 3 );
    CHECK( manifold.bodyB == 7 );
    CHECK( std::isfinite( manifold.normal.x ) );
    CHECK( std::isfinite( manifold.normal.y ) );
    CHECK( std::isfinite( manifold.normal.z ) );
    for ( uint8_t point = 0; point < manifold.pointCount; ++point )
    {
        CHECK( std::isfinite( manifold.points[point].penetration ) );
        CHECK( manifold.points[point].penetration >= -0.02f );
    }
}

std::string FullArtifactPath()
{
    return "TestOutput/coverage_floor_unit/full_tracks.skreplay";
}

SkullbonezCore::Geometry::Terrain& FlatCoverageTerrain()
{
    static SkullbonezCore::Core::EngineConfig config;
    static SkullbonezCore::Assets::AssetSystem assets;
    static SkullbonezTests::NullRenderResourceFactory resources;
    static SkullbonezCore::Geometry::Terrain terrain( 0.0f, 0.0f, 0.0f, config, assets, resources );
    return terrain;
}

ColliderShapeKind ShapeKind( const CollisionShape& shape )
{
    if ( std::holds_alternative<BoundingBox>( shape ) )
    {
        return ColliderShapeKind::Box;
    }
    if ( std::holds_alternative<ConvexHullShape>( shape ) )
    {
        return ColliderShapeKind::ConvexHull;
    }
    return ColliderShapeKind::Sphere;
}

void CheckUnderwaterForcePath( const CollisionShape& shape, uint32_t sceneId )
{
    // Lifetime: fixed-capacity stores are process-owned so repeated shape cases
    // reuse one allocation without placing multi-megabyte fixtures on the stack.
    static const std::unique_ptr<PhysicsBodyStore> bodyStorage = std::make_unique<PhysicsBodyStore>();
    static const std::unique_ptr<ColliderStore> colliderStorage = std::make_unique<ColliderStore>();
    PhysicsBodyStore& bodies = *bodyStorage;
    ColliderStore& colliders = *colliderStorage;
    bodies.Clear();
    colliders.Clear();

    PhysicsBodyCreateRecord body;
    body.cold.sceneObjectId = PhysicsSceneObjectId{ sceneId };
    body.cold.mass = 4.0f;
    body.cold.volume = SkullbonezCore::Math::CollisionDetection::GetShapeVolume( shape );
    body.cold.projectedSurfaceArea =
        SkullbonezCore::Math::CollisionDetection::GetShapeProjectedSurfaceArea( shape );
    body.cold.dragCoefficient = 0.4f;
    body.cold.rotationalInertia = Vector3( 8.0f, 2.0f, 6.0f );
    body.cold.angularVelocityLimit = 100.0f;
    body.cold.usesWorldInertia = true;
    body.hot.position = Vector3( 20.0f, 0.25f, 20.0f );
    body.hot.orientation.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 0.35f );
    body.hot.linearVelocity = Vector3( 3.0f, -1.0f, 2.0f );
    body.hot.angularVelocity = Vector3( 1.0f, 2.0f, -1.0f );
    body.hot.inverseMass = 0.25f;
    body.hot.inverseRotationalInertia = Vector3( 0.125f, 0.5f, 1.0f / 6.0f );
    body.hot.boundingRadius = SkullbonezCore::Math::CollisionDetection::GetShapeBoundingRadius( shape );
    const auto handle = bodies.CreateBodyRecord( body );
    REQUIRE( handle.IsValid() );

    ColliderRecord collider;
    collider.body = handle;
    collider.sceneObjectId = body.cold.sceneObjectId;
    collider.shape = shape;
    collider.shapeKind = ShapeKind( shape );
    collider.boundingRadius = body.hot.boundingRadius;
    collider.projectedSurfaceArea = body.cold.projectedSurfaceArea;
    collider.dragCoefficient = body.cold.dragCoefficient;
    REQUIRE( colliders.CreateColliderRecord( collider ).IsValid() );

    PhysicsWorldForces forces;
    forces.gravity = -9.8f;
    forces.fluidSurfaceHeight = 2.0f;
    forces.fluidDensity = 1000.0f;
    forces.gasDensity = 0.05f;
    forces.angularDragMultiplier = 2.0f;
    const Vector3 mutualForce( 1.0f, 0.0f, -0.5f );
    REQUIRE( bodies.ApplyForces( forces, colliders, 0, 1.0f / 120.0f, &mutualForce ) );
    const auto hot = bodies.HotFields();
    CHECK( std::isfinite( hot.linearVelocityX[0] ) );
    CHECK( std::isfinite( hot.linearVelocityY[0] ) );
    CHECK( std::isfinite( hot.angularVelocityZ[0] ) );
    CHECK( hot.linearVelocityY[0] > body.hot.linearVelocity.y );
}
} // namespace

TEST_CASE( "Coverage floor contract: full replay tracks round-trip owner values" )
{
    auto engineStorage = std::make_unique<PhysicsEngine>();
    PhysicsEngine& engine = *engineStorage;
    engine.Clear();
    engine.ReserveAuthoredBodyCapacity( 1 );

    const CollisionShape shape = SphereShape( 1.0f );
    auto bodyDesc = MakePhysicsBodyCreateDesc( PhysicsSceneObjectId{ 501u },
                                               shape,
                                               Vector3( 0.0f, 4.0f, 0.0f ),
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               Vector3( 1.0f, 0.0f, 0.0f ),
                                               Vector3( 0.0f, 0.25f, 0.0f ),
                                               Vector3( 0.8f, 0.8f, 0.8f ),
                                               2.0f,
                                               0.25f,
                                               PhysicsBodyMotionKind::Dynamic,
                                               nullptr,
                                               "coverage-artifact-body" );
    auto colliderDesc = MakeColliderCreateDesc( shape, 0.25f, 4u, "coverage-artifact" );
    colliderDesc.sceneObjectId = bodyDesc.sceneObjectId;
    const auto registration = engine.RegisterAuthoredBody( bodyDesc, colliderDesc );
    REQUIRE( registration.IsValid() );

    SceneEntityStore entities;
    entities.ConfigureCapacity( 1 );
    SceneEntityCreateDesc entity;
    entity.sceneObjectId = bodyDesc.sceneObjectId;
    entity.SetName( "coverage_artifact_body" );
    REQUIRE( entities.PreflightAppend( entity ).ok );
    entities.CommitAppend( entity, registration.body );

    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 1;
    config.runtimeBodyCapacity = 1;
    ReplaySolverRecorder solver;
    ReplayRecorder presentation;
    ReplayEventRecorder events;
    REQUIRE( solver.Configure( config ) );
    REQUIRE( presentation.Configure( config ) );
    REQUIRE( events.Configure( config ) );
    solver.ResetTimeline( "coverage-floor" );
    presentation.ResetTimeline( "coverage-floor" );
    events.ResetTimeline( "coverage-floor" );

    ReplayLauncherVisualSample launcher;
    launcher.fireMode = ReplayLauncherFireMode::Projectile;
    launcher.visualizeRays = true;
    launcher.impulseStrength = 42.0f;
    launcher.projectileSpeed = 84.0f;
    ReplayRayCastLineSample ray;
    ray.start = Vector3( 1.0f, 2.0f, 3.0f );
    ray.end = Vector3( 4.0f, 5.0f, 6.0f );
    ray.ageSeconds = 0.5f;
    ray.active = true;
    ray.hit = true;
    launcher.rayLines.push_back( ray );
    LauncherLaserShotSnapshot shot;
    shot.start = Vector3( -1.0f, 2.0f, 0.0f );
    shot.end = Vector3( 3.0f, 2.0f, 0.0f );
    shot.cameraRight = Vector3( 1.0f, 0.0f, 0.0f );
    shot.cameraUp = Vector3( 0.0f, 1.0f, 0.0f );
    shot.ageSeconds = 0.25f;
    shot.lifetimeSeconds = 1.0f;
    shot.active = true;
    launcher.laserShots.push_back( shot );

    ReplayCaptureInput capture;
    SkullbonezCore::Gameplay::TornadoGameplay tornadoGameplay;
    capture.branch.branchId = 9u;
    capture.branch.parentBranchId = 4u;
    capture.eventCursor = 3u;
    capture.sceneFrame = 20;
    capture.physicsDt = 1.0f / 120.0f;
    capture.fixedStep = true;
    capture.physics = &engine;
    capture.tornadoGameplay = &tornadoGameplay;
    capture.entities = &entities;
    capture.bodyStore = &PhysicsEngine::ReadBodies( engine );
    capture.colliderStore = &PhysicsEngine::ReadColliders( engine );
    capture.launcherVisual = &launcher;

    solver.CaptureFrame( capture );
    const ReplaySolverFrameSample* sample = solver.LatestSample();
    REQUIRE( sample != nullptr );
    presentation.CaptureFrameFromSolverSample( *sample );

    REQUIRE( engine.SetBodyVelocity( registration.body,
                                     Vector3( 2.0f, 1.0f, -1.0f ),
                                     Vector3( 0.1f, 0.2f, 0.3f ),
                                     true ) );
    capture.eventCursor = 4u;
    capture.sceneFrame = 21;
    solver.CaptureFrame( capture );
    sample = solver.LatestSample();
    REQUIRE( sample != nullptr );
    presentation.CaptureFrameFromSolverSample( *sample );

    for ( ReplayFrameIndex frame = 0u; frame < 2u; ++frame )
    {
        ReplayEventInput event;
        event.frameIndex = frame;
        event.branch = capture.branch;
        event.kind = ReplayEventKind::OwnerAction;
        event.flags = 5u + static_cast<uint32_t>( frame );
        event.value0 = 100 + static_cast<int32_t>( frame );
        event.data0 = 0xABC000u + frame;
        event.text = frame == 0u ? "first-owner-event" : "second-owner-event";
        events.RecordEvent( event );
    }

    ReplayV2SaveResult save;
    const std::string path = FullArtifactPath();
    REQUIRE( ReplayV2Artifact::SavePresentationWithSolverHashes(
        presentation, solver, events, path.c_str(), &save ) );
    CHECK( save.sampleCount == 2u );
    CHECK( save.solverHashCount == 2u );
    CHECK( save.solverCheckpointCount == 2u );
    CHECK( save.eventCount == 2u );
    CHECK( save.eventCursorCount == 2u );
    CHECK( save.fileBytes > 0u );

    std::vector<ReplayPresentationSample> loadedPresentation;
    ReplayV2LoadResult presentationResult;
    REQUIRE( ReplayV2Artifact::LoadPresentation( path.c_str(), loadedPresentation, &presentationResult ) );
    REQUIRE( loadedPresentation.size() == 2u );
    CHECK( presentationResult.firstFrame == 0u );
    CHECK( presentationResult.lastFrame == 1u );
    REQUIRE( loadedPresentation.back().bodies.size() == 1u );
    CHECK( loadedPresentation.back().bodies[0].id.value == 501u );
    CHECK( loadedPresentation.back().bodies[0].linearVelocity.x == doctest::Approx( 2.0f ) );

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult checkpointResult;
    REQUIRE( ReplayV2Artifact::LoadSolverCheckpoints( path.c_str(), checkpoints, &checkpointResult ) );
    REQUIRE( checkpoints.size() == 2u );
    CHECK( checkpointResult.firstFrame == 0u );
    CHECK( checkpointResult.lastFrame == 1u );
    CHECK( checkpoints[0].branch.branchId == 9u );
    CHECK( checkpoints[0].eventCursor == 3u );
    CHECK( checkpoints[1].eventCursor == 4u );
    REQUIRE( checkpoints[1].bodies.size() == 1u );
    CHECK( checkpoints[1].bodies[0].linearVelocity.y == doctest::Approx( 1.0f ) );
    CHECK( checkpoints[0].launcherVisual.rayLines.size() == 1u );
    CHECK( checkpoints[0].launcherVisual.laserShots.size() == 1u );

    std::vector<ReplayEventSample> loadedEvents;
    ReplayV2EventLoadResult eventResult;
    REQUIRE( ReplayV2Artifact::LoadEvents( path.c_str(), loadedEvents, &eventResult ) );
    REQUIRE( loadedEvents.size() == 2u );
    CHECK( loadedEvents[0].value0 == 100 );
    CHECK( std::string( loadedEvents[1].text ) == "second-owner-event" );

    std::vector<ReplayV2SolverHashSample> hashes;
    ReplayV2SolverHashLoadResult hashResult;
    REQUIRE( ReplayV2Artifact::LoadSolverHashes( path.c_str(), hashes, &hashResult ) );
    REQUIRE( hashes.size() == 2u );
    CHECK( hashes[0].checkpointBoundary );
    CHECK( hashes[1].checkpointBoundary );
    CHECK( hashes[1].solverHash != 0u );
    CHECK( hashes[1].presentationHash != 0u );
}

TEST_CASE( "Coverage floor contract: every object manifold shape pair publishes contacts" )
{
    const CollisionShape sphere = SphereShape( 2.0f );
    const CollisionShape box = BoxShape( Vector3( 2.0f, 2.0f, 2.0f ) );
    const CollisionShape hull = ConvexHullShape::LoadFromFile( "SkullbonezData/hulls/pyramid.hull" );

    ObjectContactBodyView a;
    a.position = Vector3( 0.0f, 0.0f, 0.0f );
    ObjectContactBodyView b;
    b.position = Vector3( 1.0f, 0.0f, 0.0f );

    CheckContactPair( a, sphere, b, sphere );
    CheckContactPair( a, sphere, b, box );
    CheckContactPair( a, box, b, sphere );
    CheckContactPair( a, box, b, box );
    CheckContactPair( a, sphere, b, hull );
    CheckContactPair( a, hull, b, sphere );
    CheckContactPair( a, box, b, hull );
    CheckContactPair( a, hull, b, box );
    CheckContactPair( a, hull, b, hull );

    ObjectContactBodyView far = b;
    far.position = Vector3( 30.0f, 0.0f, 0.0f );
    ObjectContactManifold separated;
    CHECK_FALSE( BuildObjectContactManifold( a, sphere, far, sphere, 0, 1, 0.0f, separated ) );
    CHECK_FALSE( BuildObjectContactManifold( a, sphere, far, hull, 0, 1, 0.0f, separated ) );

    ObjectContactBodyView moving = a;
    moving.position = Vector3( -5.0f, 0.0f, 0.0f );
    ObjectContactBodyView target = a;
    const auto sweep = SweepObjectContact( moving,
                                           sphere,
                                           Vector3( 10.0f, 0.0f, 0.0f ),
                                           target,
                                           sphere,
                                           Vector3( 0.0f, 0.0f, 0.0f ),
                                           1.0f );
    CHECK( sweep.hit );
    CHECK( sweep.collisionTime >= 0.0f );
    CHECK( sweep.collisionTime <= 1.0f );
}

TEST_CASE( "Coverage floor contract: box and hull buoyancy stay finite under partial submersion" )
{
    CheckUnderwaterForcePath( BoxShape( Vector3( 2.0f, 0.5f, 1.0f ) ), 601u );
    CheckUnderwaterForcePath( ConvexHullShape::LoadFromFile( "SkullbonezData/hulls/pyramid.hull" ), 602u );
}

TEST_CASE( "Coverage floor contract: terrain sweep and manifold support every collision shape" )
{
    SkullbonezCore::Geometry::Terrain& terrain = FlatCoverageTerrain();
    const CollisionShape shapes[] = {
        SphereShape( 1.0f ),
        BoxShape( Vector3( 1.0f, 1.0f, 1.0f ) ),
        ConvexHullShape::LoadFromFile( "SkullbonezData/hulls/pyramid.hull" ),
    };
    const float centerHeights[] = { 4.0f, 4.0f, 5.0f };

    for ( int index = 0; index < 3; ++index )
    {
        TerrainContactBodyView body;
        body.position = Vector3( 20.0f, centerHeights[index], 20.0f );
        body.linearVelocity = Vector3( 0.0f, -5.0f, 0.0f );
        body.terrain = &terrain;
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
    resting.terrain = &terrain;
    resting.terrainContactThreshold = 0.15f;
    resting.contactEpsilon = 0.05f;
    TerrainContactSweepResult restingSweep;
    restingSweep.hit = true;
    restingSweep.collisionTime = 0.0f;
    restingSweep.collidedPlane.m_normal = Vector3( 0.0f, 1.0f, 0.0f );
    restingSweep.collidedPlane.m_distance = 0.0f;
    TerrainContactManifold restingManifold;
    REQUIRE( BuildTerrainContactManifold(
        resting, shapes[1], 9, restingSweep, 1.0f / 120.0f, restingManifold ) );
    CHECK( restingManifold.pointCount == 4u );
    CHECK( restingManifold.supportsRestingPolicy );
    CHECK( restingManifold.allowsTangentFriction );
    CHECK_FALSE( restingManifold.inhibitsSleep );
}

TEST_CASE( "Coverage floor contract: replay timeline applies retention and sequences owner events atomically" )
{
    auto timeline = std::make_unique<ReplayTimeline>();
    CHECK_FALSE( timeline->RecordingConfigured() );
    const ReplayRecordingConfigResult configured = timeline->ConfigureRecording( true, 12, nullptr, 2 );
    CHECK( timeline->RecordingConfigured() );
    CHECK( timeline->RecordingEnabled() );
    CHECK( configured.presentationConfig.retentionSeconds == 12 );
    CHECK( configured.solverConfig.retentionSeconds == 12 );
    CHECK( configured.eventStats.enabled );

    timeline->Reset( "coverage-timeline" );
    ReplayEventCommand command;
    command.kind = ReplayEventKind::OwnerAction;
    command.useNextFrame = true;
    command.flags = 7u;
    command.value0 = 11;
    ReplayBranchInfo branch;
    branch.branchId = 3u;
    timeline->SubmitEvent( command, branch );
    const ReplayEventRecorderStats eventStats = timeline->Events().GetStats();
    CHECK( eventStats.eventCount == 1u );
    CHECK( eventStats.nextSequence == 1u );

    // Record/stop is a gate over already-reserved rings. It neither clears the
    // timeline nor lets stopped owner events advance the event cursor.
    CHECK( timeline->SetRecordingEnabled( false ) );
    CHECK_FALSE( timeline->RecordingEnabled() );
    timeline->SubmitEvent( command, branch );
    CHECK( timeline->Events().GetStats().eventCount == 1u );
    CHECK( timeline->SetRecordingEnabled( true ) );
    CHECK( timeline->RecordingEnabled() );
    timeline->SubmitEvent( command, branch );
    CHECK( timeline->Events().GetStats().eventCount == 2u );

    ReplayMemoryPolicyRequest compact;
    compact.presetIndex = static_cast<int>( ReplayMemoryPreset::Compact );
    const ReplayMemoryPolicyApplyResult changed = timeline->ApplyMemoryPolicyRequest( compact );
    CHECK( changed.changed );
    CHECK( changed.recordersReset );
    CHECK( timeline->MemoryPolicy().solverRetentionSeconds <=
           timeline->MemoryPolicy().presentationRetentionSeconds );
    const ReplayMemoryPolicyApplyResult unchanged = timeline->ApplyMemoryPolicyRequest( {} );
    CHECK_FALSE( unchanged.changed );
    CHECK_FALSE( unchanged.recordersReset );

    const ReplayTimelineMemoryStats memory = timeline->CollectMemoryStats();
    CHECK( memory.presentationSamples == 0u );
    CHECK( memory.solverSamples == 0u );
    CHECK( memory.eventSamples == 0u );
    timeline->ClearLoadedPresentation();
    CHECK_FALSE( timeline->LoadedPresentation().enabled );
}
