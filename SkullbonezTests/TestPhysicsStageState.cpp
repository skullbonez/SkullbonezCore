// Purpose:
//   Locks sleep-owner transitions and narrowphase island determinism.

// Invariants:
//   - Sleep frame counts clamp to uint8 storage without wrapping.
//   - Unsupported or nonquiet bodies reset a positive sleep counter, and
//     repeated material-impact wake requests publish one awake transition.
//   - Support propagates to a fixed point through model-order edges.
//   - A stretched point joint publishes the sleep-block reason without
//     advancing its island counter; a relaxed joint remains eligible.
//   - Awake indices remain sorted across sleep, parallel-wake flush, and cold
//     topology rebuild boundaries.
//   - Parallel narrowphase scheduling cannot reorder pair-slot results.
//   - Terrain sampling and swept contact consume Physics-owned value views
//     without linking a World terrain implementation into the test path.
//   - Config stamping copies every Physics-owned source field without clamping;
//     clamp policy remains at the consuming owner boundary.
//   - Pipeline count-only and full-record modes share the fixed saturation
//     ceiling, while full mode preserves every retained payload field.
//   - Motion eligibility applies exact direction-valid shape thresholds with
//     stable equality and no worker scheduling dependency.
//   - Sleep and parallel narrowphase count-only lanes preserve event identity
//     while leaving their optional payload storage untouched.

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "TestResultLoadFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"

#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/SbResult.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsSpatialCellKey.h"
#include "../SkullbonezSource/Physics/PhysicsMotionEligibility.h"
#include "../SkullbonezSource/Physics/PhysicsTerrainView.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/TerrainContactManifold.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsSleepController.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::ObjectNarrowphaseEvent;
using SkullbonezCore::Physics::ObjectNarrowphaseEventKind;
using SkullbonezCore::Physics::ObjectNarrowphaseStepPolicy;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsNarrowphaseStage;
using SkullbonezCore::Physics::PhysicsRuntimeSettings;
using SkullbonezCore::Physics::PhysicsSleepController;
using SkullbonezCore::Physics::PhysicsTerrainCell;
using SkullbonezCore::Physics::PhysicsTerrainView;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Physics::SweepTerrainContact;
using SkullbonezCore::Physics::TerrainContactBodyView;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
void ReserveTestSleepCapacity( PhysicsSleepController& controller )
{
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    controller.ReserveBodyCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
}

PhysicsBodyStore& StageBodyStore()
{
    // Lifetime: the fixed-capacity store is process-owned. Keeping only its
    // pointer in the image avoids inflating the Debug PE's SizeOfImage beyond
    // the Windows loader limit while retaining one reusable test allocation.
    static const std::unique_ptr<PhysicsBodyStore> store = std::make_unique<PhysicsBodyStore>();

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store->ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }
    store->Clear();
    return *store;
}

ColliderStore& StageColliderStore()
{
    // Lifetime: see StageBodyStore; the collider capacity is similarly owned
    // by the test process rather than embedded as initialized image data.
    static const std::unique_ptr<ColliderStore> store = std::make_unique<ColliderStore>();

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store->ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        store->ReserveShapeCapacity( 512u, 512u, 16u );
    }
    store->Clear();
    return *store;
}

CollisionShape UnitSphere()
{
    return CollisionShape( BoundingSphere( 1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ) );
}

void CheckRuntimeSettingsMatchConfig( const PhysicsRuntimeSettings& settings,
                                      const SkullbonezCore::Core::EngineConfig& config )
{
    CHECK( settings.material.sphereDragCoefficient == config.physicsMaterial.sphereDragCoeff );
    CHECK( settings.material.terrainFrictionCoefficient == config.physicsMaterial.frictionCoeff );
    CHECK( settings.material.objectFrictionCoefficient == config.physicsMaterial.objectFrictionCoeff );
    CHECK( settings.material.rollingFrictionCoefficient == config.physicsMaterial.rollingFrictionCoeff );
    CHECK( settings.material.spinFrictionCoefficient == config.physicsMaterial.spinFrictionCoeff );
    CHECK( settings.body.angularVelocityLimit == config.bodySimulation.velocityLimit );
    CHECK( settings.body.contactRestitutionThreshold == config.bodySimulation.contactRestitutionThreshold );
    CHECK( settings.body.contactEpsilon == config.bodySimulation.contactEpsilon );
    CHECK( settings.solver.slop == config.persistentContactSolver.slop );
    CHECK( settings.solver.baumgarteBeta == config.persistentContactSolver.baumgarteBeta );
    CHECK( settings.solver.positionCorrectionPercent == config.persistentContactSolver.positionCorrectionPercent );
    CHECK( settings.solver.iterations == config.persistentContactSolver.iterations );
    CHECK( settings.terrain.threshold == config.terrainContact.threshold );
    CHECK( settings.terrain.slop == config.terrainContact.slop );
    CHECK( settings.terrain.baumgarteBeta == config.terrainContact.baumgarteBeta );
    CHECK( settings.terrain.maxBaumgarteBias == config.terrainContact.maxBaumgarteBias );
    CHECK( settings.sleep.linearSpeed == config.physicsSleep.linearSpeed );
    CHECK( settings.sleep.angularSpeed == config.physicsSleep.angularSpeed );
    CHECK( settings.sleep.frames == config.physicsSleep.frames );
    CHECK( settings.broadphase.cellSize == config.broadphase.cellSize );
    CHECK( settings.execution.parallel == config.physicsExecution.parallel );
    CHECK( settings.execution.parallelApplyForces == config.physicsExecution.parallelApplyForces );
    CHECK( settings.execution.parallelMutualGravity == config.physicsExecution.parallelMutualGravity );
    CHECK( settings.execution.parallelNarrowphase == config.physicsExecution.parallelNarrowphase );
    CHECK( settings.execution.parallelTerrainDetect == config.physicsExecution.parallelTerrainDetect );
    CHECK( settings.execution.parallelIntegrate == config.physicsExecution.parallelIntegrate );
    CHECK( settings.worldForces.gravity == config.worldForces.gravity );
}
} // namespace

TEST_CASE( "Physics motion eligibility: cached sphere box hull facts are topology-owned" )
{
    namespace Collision = SkullbonezCore::Math::CollisionDetection;
    const CollisionShape sphere = BoundingSphere( 0.25f, Vector3( 0.5f, 0.0f, 0.0f ), 0.0f );
    const auto sphereFacts = Collision::GetCollisionShapeMotionGeometry( sphere );
    CHECK( sphereFacts.minimumCollisionThickness == doctest::Approx( 0.5f ) );
    CHECK( sphereFacts.maximumCenterOfMassRadius == doctest::Approx( 0.75f ) );

    const CollisionShape box = Collision::BoundingBox( Vector3( 1.0f, 2.0f, 0.25f ), Vector3( 0.0f, 0.0f, 0.0f ) );
    const auto boxFacts = Collision::GetCollisionShapeMotionGeometry( box );
    CHECK( boxFacts.minimumCollisionThickness == doctest::Approx( 0.5f ) );
    CHECK( boxFacts.maximumCenterOfMassRadius == doctest::Approx( std::sqrt( 5.0625f ) ) );

    const CollisionShape thinProjectile = Collision::BoundingBox( Vector3( 3.0f, 0.01f, 0.02f ),
                                                                  Vector3( 0.5f, 0.0f, 0.0f ) );
    const auto thinProjectileFacts = Collision::GetCollisionShapeMotionGeometry( thinProjectile );
    CHECK( thinProjectileFacts.minimumCollisionThickness == doctest::Approx( 0.02f ) );
    CHECK( thinProjectileFacts.maximumCenterOfMassRadius == doctest::Approx( std::sqrt( 12.2505f ) ) );

    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    Collision::ConvexHullShape hull;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics,
                                                                     "SkullbonezData/hulls/building_brick_unit.hull",
                                                                     hull ) );
    const auto hullFacts = Collision::GetCollisionShapeMotionGeometry( CollisionShape( hull ) );
    CHECK( std::isfinite( hullFacts.minimumCollisionThickness ) );
    CHECK( hullFacts.minimumCollisionThickness == doctest::Approx( 0.68f ) );
    CHECK( hullFacts.maximumCenterOfMassRadius == doctest::Approx( hull.GetBoundingRadius() ) );

    Collision::ConvexHullShape scaledNormalHull;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics,
                                                                     "SkullbonezData/hulls/test_scaled_normals_box.hull",
                                                                     scaledNormalHull ) );
    const auto scaledNormalFacts = Collision::GetCollisionShapeMotionGeometry( CollisionShape( scaledNormalHull ) );
    CHECK( scaledNormalFacts.minimumCollisionThickness == doctest::Approx( hullFacts.minimumCollisionThickness ) );
    CHECK( scaledNormalFacts.maximumCenterOfMassRadius == doctest::Approx( hullFacts.maximumCenterOfMassRadius ) );
}

TEST_CASE( "Physics motion eligibility: exact radius boundary sleep wake and topology reset are deterministic" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    constexpr float radiusBoundary = 1.0f;
    const float belowBoundary = std::nextafter( radiusBoundary, 0.0f );
    const float aboveBoundary = std::nextafter( radiusBoundary, std::numeric_limits<float>::infinity() );
    const float linearSpeeds[] = { belowBoundary, radiusBoundary, aboveBoundary, 0.0f, 10.0f };

    for ( int bodyIndex = 0; bodyIndex < 5; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.fixed = bodyIndex == 4;
        body.hot.linearVelocity = Vector3( linearSpeeds[bodyIndex], 0.0f, 0.0f );
        body.hot.angularVelocity = bodyIndex == 3 ? Vector3( 0.0f, radiusBoundary, 0.0f )
                                                  : SkullbonezCore::Math::Vector::ZERO_VECTOR;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = 1.0f;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere ).IsValid() );
    }

    SkullbonezCore::Physics::PhysicsMotionEligibilityStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 5u );
    }
    std::array<uint8_t, 5> sleep = {};
    stage.Run( bodies, colliders, sleep, 1.0f );
    REQUIRE( stage.State().size() == 5u );
    CHECK( stage.State()[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) == 0u );
    CHECK( stage.State()[4] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.Stats().evaluatedBodies == 4 );
    CHECK( stage.Stats().discreteBodies == 3 );
    CHECK( stage.Stats().promotedBodies == 1 );
    CHECK( stage.Stats().angularExpandedBodies == 0 );
    CHECK( stage.LinearTravelSquared()[1] == doctest::Approx( 1.0f ) );
    CHECK( stage.AngularTravelSquared()[3] == doctest::Approx( 1.0f ) );
    CHECK( stage.LinearDirectionalBoundary()[4] == -1.0f );
    CHECK( stage.Stats().passDurationNanoseconds > 0u );
    const uint64_t committedBytes = stage.CollectDynamicMemoryBytes();

    auto hot = bodies.MutableHotFields();
    hot.linearVelocityX[0] = belowBoundary;
    hot.linearVelocityX[1] = aboveBoundary;
    hot.angularVelocityY[3] = aboveBoundary;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( stage.CollectDynamicMemoryBytes() == committedBytes );
    CHECK( ( stage.State()[0] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) != 0u );

    hot.linearVelocityX[1] = radiusBoundary;
    hot.angularVelocityY[3] = radiusBoundary;
    sleep[2] = 1u;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( stage.State()[2] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) != 0u );
    CHECK( stage.LinearDirectionalBoundary()[2] == -1.0f );
    CHECK( stage.LinearDirectionalBoundary()[4] == -1.0f );

    sleep[2] = 0u;
    hot.linearVelocityX[1] = radiusBoundary;
    hot.linearVelocityX[2] = aboveBoundary;
    hot.angularVelocityY[3] = aboveBoundary;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) != 0u );

    hot.linearVelocityX[1] = belowBoundary;
    hot.angularVelocityY[3] = belowBoundary;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) == 0u );

    hot.angularVelocityY[3] = belowBoundary;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) == 0u );

    hot.linearVelocityX[2] = 0.08f;
    stage.InvalidateBodyTopology();
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );
}

TEST_CASE( "Physics motion eligibility: linear and angular travel scale with collider geometry" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape shapes[] = {
        BoundingSphere( 0.01f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ),
        BoundingSphere( 100.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ),
        BoundingSphere( 0.01f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ),
        BoundingSphere( 100.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ),
    };
    const float colliderRadii[] = { 0.01f, 100.0f, 0.01f, 100.0f };
    const float travelPerTick[] = { 0.099f, 0.099f, 0.1f, 0.1f };
    const float angularTipTravelPerTick[] = { 0.05f, 0.05f, 0.2f, 0.2f };

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.linearVelocity = Vector3( travelPerTick[bodyIndex], 0.0f, 0.0f );
        body.hot.angularVelocity = Vector3( 0.0f, angularTipTravelPerTick[bodyIndex] / colliderRadii[bodyIndex], 0.0f );
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = colliderRadii[bodyIndex];
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shapes[bodyIndex] )
                     .IsValid() );
    }

    SkullbonezCore::Physics::PhysicsMotionEligibilityStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 4u );
    }
    const std::array<uint8_t, 4> sleep = {};
    stage.Run( bodies, colliders, sleep, 1.0f );

    REQUIRE( stage.State().size() == 4u );
    CHECK( ( stage.State()[0] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( stage.State()[1] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) != 0u );
    CHECK( ( stage.State()[3] & SkullbonezCore::Physics::PhysicsMotionEligibilityAngularExpanded ) == 0u );
}

TEST_CASE( "Physics motion eligibility: spheres use one squared radius boundary" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const float radii[] = { 0.06f, 0.06f, 0.06f, 0.5f };
    const float travel[] = { 0.06f, std::nextafter( 0.06f, std::numeric_limits<float>::infinity() ), 0.09f, 0.09f };

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.linearVelocity = Vector3( travel[bodyIndex], 0.0f, 0.0f );
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        const CollisionShape sphere = BoundingSphere( radii[bodyIndex], SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f );
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere ).IsValid() );
    }

    SkullbonezCore::Physics::PhysicsMotionEligibilityStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 4u );
    }
    const std::array<uint8_t, 4> sleep = {};
    stage.Run( bodies, colliders, sleep, 1.0f );

    REQUIRE( stage.State().size() == 4u );
    CHECK( stage.State()[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( stage.State()[3] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.Stats().policyVersion == SkullbonezCore::Physics::PHYSICS_MOTION_ELIGIBILITY_POLICY_VERSION );
    CHECK( stage.Stats().discreteBodies == 2 );
    CHECK( stage.Stats().promotedBodies == 2 );
    CHECK( stage.Stats().promotionsThisStep == 2 );
    CHECK( stage.Stats().demotionsThisStep == 0 );

    auto hot = bodies.MutableHotFields();
    hot.linearVelocityX[1] = 0.06f;
    hot.linearVelocityX[2] = std::nextafter( 0.06f, 0.0f );
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) == 0u );
    CHECK( stage.Stats().promotionsThisStep == 0 );
    CHECK( stage.Stats().demotionsThisStep == 1 );
}

TEST_CASE( "Physics motion eligibility: elongated boxes and hulls use the first reached local-axis radius" )
{
    namespace Collision = SkullbonezCore::Math::CollisionDetection;
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    Collision::ConvexHullShape hull;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics,
                                                                     "SkullbonezData/hulls/building_brick_unit.hull",
                                                                     hull ) );
    Collision::ConvexHullShape diagonalHull;
    REQUIRE( SkullbonezTests::ResultLoadFixtures::TryLoadConvexHull( diagnostics,
                                                                     "SkullbonezData/hulls/test_motion_tetrahedron.hull",
                                                                     diagonalHull ) );

    Vector3 edgeAxisTravel = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    bool foundRequiredEdgeAxis = false;
    const std::span<const Collision::ConvexHullMotionAxis> diagonalAxes = diagonalHull.GetMotionAxes();

    for ( int x = -5; x <= 5 && !foundRequiredEdgeAxis; ++x )
    {
        for ( int y = -5; y <= 5 && !foundRequiredEdgeAxis; ++y )
        {
            for ( int z = -5; z <= 5 && !foundRequiredEdgeAxis; ++z )
            {
                const Vector3 rawDirection( static_cast<float>( x ), static_cast<float>( y ), static_cast<float>( z ) );
                const float directionLengthSquared = SkullbonezCore::Math::Vector::VectorMagSquared( rawDirection );

                if ( directionLengthSquared <= 0.0f )
                {
                    continue;
                }

                const Vector3 direction = rawDirection / std::sqrt( directionLengthSquared );
                float faceBoundary = ( std::numeric_limits<float>::max )();
                float completeBoundary = ( std::numeric_limits<float>::max )();

                for ( std::size_t candidateIndex = 0; candidateIndex < diagonalAxes.size(); ++candidateIndex )
                {
                    const float projection = std::fabs( SkullbonezCore::Math::Vector::Dot( direction, diagonalAxes[candidateIndex].normalLocal ) );

                    if ( projection <= 1.0e-5f )
                    {
                        continue;
                    }

                    const float boundary = std::sqrt( diagonalAxes[candidateIndex].halfWidthSquared ) / projection;
                    completeBoundary = (std::min)( completeBoundary, boundary );

                    if ( candidateIndex < diagonalHull.GetFaceCount() )
                    {
                        faceBoundary = (std::min)( faceBoundary, boundary );
                    }
                }

                if ( completeBoundary < faceBoundary * 0.99f )
                {
                    edgeAxisTravel = direction * ( 0.5f * ( completeBoundary + faceBoundary ) );
                    foundRequiredEdgeAxis = true;
                }
            }
        }
    }

    REQUIRE( foundRequiredEdgeAxis );

    Collision::ConvexHullShape scaledDiagonalHull = diagonalHull;
    scaledDiagonalHull.ScaleAxis( 0, 0.001f );
    scaledDiagonalHull.ScaleAxis( 1, 0.001f );
    scaledDiagonalHull.ScaleAxis( 2, 0.001f );
    REQUIRE( scaledDiagonalHull.GetMotionAxes().size() == diagonalHull.GetMotionAxes().size() );

    Collision::ConvexHullShape nearParallelDiagonalHull = diagonalHull;
    nearParallelDiagonalHull.ScaleAxis( 0, 3.0e5f );
    REQUIRE( nearParallelDiagonalHull.GetMotionAxes().size() == 9u );

    SkullbonezCore::Math::Orientation::Quaternion quarterTurn;
    quarterTurn.RotateAboutAxis( Vector3( 0.0f, 0.0f, 1.0f ), 1.57079632679f );
    const CollisionShape shapes[] = {
        Collision::BoundingBox( Vector3( 3.0f, 0.1f, 0.1f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
        Collision::BoundingBox( Vector3( 3.0f, 0.1f, 0.1f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
        Collision::BoundingBox( Vector3( 3.0f, 0.1f, 0.1f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
        CollisionShape( hull ),
        CollisionShape( hull ),
        Collision::BoundingBox( Vector3( 3.0f, 0.1f, 0.1f ), Vector3( 0.0f, 0.0f, 0.0f ) ),
        CollisionShape( hull ),
        CollisionShape( diagonalHull ),
        CollisionShape( scaledDiagonalHull ),
        CollisionShape( nearParallelDiagonalHull ),
    };
    const Vector3 travel[] = {
        Vector3( 1.0f, 0.0f, 0.0f ), // Below the long box radius: remains Discrete.
        Vector3( 0.0f, 0.2f, 0.0f ), // Above the thin box radius: promote.
        Vector3( 1.0f, 0.0f, 0.0f ), // Quarter turn maps world X onto thin local Y: promote.
        Vector3( 1.0f, 0.0f, 0.0f ), // Below the hull's 1.45 m X radius: remains Discrete.
        Vector3( 0.0f, 0.0f, 0.5f ), // Above the hull's 0.34 m Z radius: promote.
        Vector3( 1.0f, 0.2f, 0.0f ), // Long X travel cannot mask above-thickness Y travel.
        Vector3( 1.0f, 0.0f, 0.5f ), // Long X travel cannot mask above-thickness hull Z travel.
        edgeAxisTravel,              // Face slabs fit, but an edge-cross-edge difference-body axis does not.
        edgeAxisTravel * 0.001f,     // Uniform copy scale preserves the same required edge-cross-edge axis.
        Vector3( edgeAxisTravel.x * 3.0e5f, edgeAxisTravel.y,
                 edgeAxisTravel.z ), // Anisotropic copy keeps near-parallel axes.
    };

    for ( int bodyIndex = 0; bodyIndex < 10; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.linearVelocity = travel[bodyIndex];
        body.hot.orientation = bodyIndex == 2 ? quarterTurn : SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shapes[bodyIndex] )
                     .IsValid() );
    }

    SkullbonezCore::Physics::PhysicsMotionEligibilityStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 10u );
    }
    const std::array<uint8_t, 10> sleep = {};
    stage.Run( bodies, colliders, sleep, 1.0f );

    REQUIRE( stage.State().size() == 10u );
    REQUIRE( stage.LinearDirectionalBoundary().size() == 10u );
    CHECK( stage.State()[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[2] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( stage.State()[3] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[4] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[5] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[6] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[7] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[8] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( ( stage.State()[9] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( stage.LinearDirectionalBoundary()[0] == doctest::Approx( 9.0f ) );
    CHECK( stage.LinearDirectionalBoundary()[1] == doctest::Approx( 0.01f ) );
    CHECK( stage.LinearDirectionalBoundary()[2] == doctest::Approx( 0.01f ) );
    CHECK( stage.LinearDirectionalBoundary()[3] == doctest::Approx( 1.45f * 1.45f ) );
    CHECK( stage.LinearDirectionalBoundary()[4] == doctest::Approx( 0.34f * 0.34f ) );
    CHECK( stage.LinearDirectionalBoundary()[5] == doctest::Approx( 0.26f ) );
    CHECK( stage.LinearDirectionalBoundary()[6] < 1.25f );
    CHECK( stage.LinearDirectionalBoundary()[7] < 9.0f );
    CHECK( stage.LinearDirectionalBoundary()[8] < 9.0e-6f );
    CHECK( stage.LinearDirectionalBoundary()[9] < 9.0e12f );
    CHECK( stage.Stats().discreteBodies == 2 );
    CHECK( stage.Stats().promotedBodies == 8 );
    CHECK( stage.Stats().promotionsThisStep == 8 );

    auto hot = bodies.MutableHotFields();
    hot.linearVelocityY[1] = 0.05f;
    hot.linearVelocityX[2] = 0.0f;
    hot.linearVelocityZ[4] = 0.1f;
    hot.linearVelocityY[5] = 0.05f;
    hot.linearVelocityZ[6] = 0.1f;
    hot.linearVelocityX[7] = 0.0f;
    hot.linearVelocityY[7] = 0.0f;
    hot.linearVelocityZ[7] = 0.0f;
    hot.linearVelocityX[8] = 0.0f;
    hot.linearVelocityY[8] = 0.0f;
    hot.linearVelocityZ[8] = 0.0f;
    hot.linearVelocityX[9] = 0.0f;
    hot.linearVelocityY[9] = 0.0f;
    hot.linearVelocityZ[9] = 0.0f;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( stage.State()[1] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[2] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[4] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[5] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[6] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[7] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[8] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.State()[9] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.Stats().promotionsThisStep == 0 );
    CHECK( stage.Stats().demotionsThisStep == 8 );
}

TEST_CASE( "Physics motion eligibility: mixed-axis equality retains both previous linear states" )
{
    namespace Collision = SkullbonezCore::Math::CollisionDetection;
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape box = Collision::BoundingBox( Vector3( 3.0f, 0.34f, 0.1f ), Vector3( 0.0f, 0.0f, 0.0f ) );

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.linearVelocity = Vector3( 0.2f, bodyIndex == 0 ? 0.34f : 0.35f, 0.0f );
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, box ).IsValid() );
    }

    SkullbonezCore::Physics::PhysicsMotionEligibilityStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 2u );
    }
    const std::array<uint8_t, 2> sleep = {};
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( stage.State()[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );

    bodies.MutableHotFields().linearVelocityY[1] = 0.34f;
    stage.Run( bodies, colliders, sleep, 1.0f );
    CHECK( stage.State()[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( ( stage.State()[1] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );
    CHECK( stage.Stats().promotionsThisStep == 0 );
    CHECK( stage.Stats().demotionsThisStep == 0 );
}

TEST_CASE( "Physics motion eligibility: dense-row removal cannot inherit retired hysteresis" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    std::array<SkullbonezCore::Physics::PhysicsBodyHandle, 2> bodyHandles;
    std::array<SkullbonezCore::Physics::PhysicsColliderHandle, 2> colliderHandles;

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.linearVelocity = Vector3( bodyIndex == 0 ? 2.0f : 0.0f, 0.0f, 0.0f );
        bodyHandles[static_cast<std::size_t>( bodyIndex )] = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = bodyHandles[static_cast<std::size_t>( bodyIndex )];
        collider.boundingRadius = 1.0f;
        colliderHandles[static_cast<std::size_t>( bodyIndex )] = SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere );
        REQUIRE( colliderHandles[static_cast<std::size_t>( bodyIndex )].IsValid() );
    }

    SkullbonezCore::Physics::PhysicsMotionEligibilityStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyCapacity( 2u );
    }
    const std::array<uint8_t, 2> initialSleep = {};
    stage.Run( bodies, colliders, initialSleep, 1.0f );
    REQUIRE( ( stage.State()[0] & SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted ) != 0u );

    REQUIRE( colliders.DestroyColliderRecord( colliderHandles[0] ) );
    REQUIRE( bodies.DestroyBodyRecord( bodyHandles[0] ) );
    stage.InvalidateBodyTopology();
    const std::array<uint8_t, 1> survivingSleep = {};
    stage.Run( bodies, colliders, survivingSleep, 1.0f );

    REQUIRE( stage.State().size() == 1u );
    CHECK( stage.State()[0] == SkullbonezCore::Physics::PhysicsMotionEligibilityNone );
    CHECK( stage.LinearTravelSquared()[0] == 0.0f );
}

TEST_CASE( "Physics motion eligibility: angular blade expansion retains a conservative broadphase candidate" )
{
    namespace Collision = SkullbonezCore::Math::CollisionDetection;
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape shapes[] = {
        Collision::BoundingBox( Vector3( 5.0f, 0.1f, 0.1f ), SkullbonezCore::Math::Vector::ZERO_VECTOR ),
        BoundingSphere( 0.1f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f ),
    };

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = bodyIndex == 0 ? 1.0f : 0.0f;
        body.hot.fixed = bodyIndex == 1;
        body.hot.position = Vector3( bodyIndex == 0 ? 0.0f : 11.0f, 0.0f, 0.0f );
        body.hot.angularVelocity = bodyIndex == 0 ? Vector3( 0.0f, 2.0f, 0.0f ) : SkullbonezCore::Math::Vector::ZERO_VECTOR;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = Collision::GetShapeBodyOriginBoundingRadius( shapes[bodyIndex] );
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shapes[bodyIndex] )
                     .IsValid() );
    }

    auto eligibility = std::make_unique<SkullbonezCore::Physics::PhysicsMotionEligibilityStage>();
    auto broadphase = std::make_unique<SkullbonezCore::Physics::PhysicsBroadphaseStage>();
    auto diagnostics = std::make_unique<SkullbonezCore::Physics::PhysicsStepDiagnostics>();
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        eligibility->ReserveBodyCapacity( 2u );
        broadphase->ReserveSceneCapacity( 2u );
        diagnostics->ReserveSceneCapacity( 2u );
    }

    const std::array<uint8_t, 2> sleep = {};
    eligibility->Run( bodies, colliders, sleep, 1.0f );
    REQUIRE( eligibility->AngularBroadphaseExpansion()[0] > 0.0f );
    diagnostics->BeginStep( 2 );
    const std::array<int, 1> awake = { 0 };
    const std::array<SkullbonezCore::Physics::PointJointConstraint, 0> joints;
    SkullbonezCore::Physics::BroadphaseSettings settings;
    settings.cellSize = 5.0f;
    const std::array<float, 2> noAngularExpansion = { 0.0f, 0.0f };
    const auto rejectedPairs = broadphase->Run( bodies, colliders, settings, joints, sleep, awake, eligibility->State(),
                                                noAngularExpansion, *diagnostics, 1.0f, 0.0f, 0.0f );
    CHECK( rejectedPairs.empty() );
    broadphase->InvalidateBodyTopology();
    diagnostics->BeginStep( 2 );
    const auto pairs = broadphase->Run( bodies, colliders, settings, joints, sleep, awake, eligibility->State(),
                                        eligibility->AngularBroadphaseExpansion(), *diagnostics, 1.0f, 0.0f, 0.0f );
    CHECK( std::find( pairs.begin(), pairs.end(), std::make_pair( 0, 1 ) ) != pairs.end() );

    auto hot = bodies.MutableHotFields();
    hot.positionX[1] = 100.0f;
    hot.angularVelocityY[0] = ( std::numeric_limits<float>::infinity )();
    eligibility->Run( bodies, colliders, sleep, 1.0f );
    REQUIRE_FALSE( std::isfinite( eligibility->AngularBroadphaseExpansion()[0] ) );
    broadphase->InvalidateBodyTopology();
    diagnostics->BeginStep( 2 );
    const auto fallbackPairs = broadphase->Run( bodies, colliders, settings, joints, sleep, awake, eligibility->State(),
                                                eligibility->AngularBroadphaseExpansion(), *diagnostics, 1.0f, 0.0f, 0.0f );
    CHECK( std::find( fallbackPairs.begin(), fallbackPairs.end(), std::make_pair( 0, 1 ) ) != fallbackPairs.end() );
}

TEST_CASE( "Physics terrain view: analytic and cached-cell sampling stay detached from World" )
{
    PhysicsTerrainView slope;
    slope.flatSlope = true;
    slope.flatSlopeExtent = 100.0f;
    slope.slopeBaseY = 2.0f;
    slope.slopeX = 0.5f;
    slope.slopeZ = -0.25f;
    slope.maxHeight = 52.0f;
    slope.flatSlopePlane.m_normal = Vector3( -0.5f, 1.0f, 0.25f );
    slope.flatSlopePlane.m_distance = 2.0f;

    CHECK( slope.IsValid() );
    CHECK( slope.IsInBounds( 10.0f, 4.0f ) );
    CHECK_FALSE( slope.IsInBounds( 100.0f, 4.0f ) );
    CHECK( slope.HeightAt( 10.0f, 4.0f ) == doctest::Approx( 6.0f ) );
    CHECK( slope.MaxHeight() == doctest::Approx( 52.0f ) );

    std::array<PhysicsTerrainCell, 1> cells;
    cells[0].triangleA.m_normal = Vector3( 0.0f, 1.0f, 0.0f );
    cells[0].triangleA.m_distance = 1.0f;
    cells[0].triangleB.m_normal = Vector3( 1.0f, 1.0f, 0.0f );
    cells[0].triangleB.m_distance = 12.0f;

    PhysicsTerrainView cached;
    cached.cells = cells;
    cached.quadsPerSide = 1;
    cached.scaledStepSize = 10.0f;
    cached.worldExtent = 10.0f;
    cached.maxHeight = 4.0f;

    SkullbonezCore::Geometry::Plane sampledPlane;
    float sampledHeight = 0.0f;
    cached.HeightAndPlaneAt( 1.0f, 1.0f, sampledHeight, sampledPlane );
    CHECK( sampledHeight == doctest::Approx( 1.0f ) );
    CHECK( sampledPlane.m_normal.y == doctest::Approx( 1.0f ) );
    cached.HeightAndPlaneAt( 9.0f, 9.0f, sampledHeight, sampledPlane );
    CHECK( sampledHeight == doctest::Approx( 3.0f ) );
    CHECK( sampledPlane.m_normal.x == doctest::Approx( 1.0f ) );

    TerrainContactBodyView body;
    body.position = Vector3( 10.0f, 5.0f, 10.0f );
    body.linearVelocity = Vector3( 0.0f, -2.0f, 0.0f );
    body.terrain = slope;
    body.boundingRadius = 1.0f;
    body.contactEpsilon = 0.05f;
    body.terrainContactThreshold = 0.15f;
    const auto sweep = SweepTerrainContact( body, UnitSphere(), 3.0f );
    CHECK( sweep.hit );
    CHECK( sweep.collisionTime >= 0.0f );
    CHECK( sweep.collisionTime <= 3.0f );
}

TEST_CASE( "Physics runtime settings: default config stamps every owned field exactly" )
{
    const SkullbonezCore::Core::EngineConfig config;

    // Invariant: PhysicsEngine may exist before the first cold config stamp, so
    // the owner-native value defaults must independently mirror Core defaults.
    CheckRuntimeSettingsMatchConfig( PhysicsRuntimeSettings(), config );
    CheckRuntimeSettingsMatchConfig( PhysicsEngine::RuntimeSettingsFromConfig( config ), config );
}

TEST_CASE( "Physics runtime settings: custom config remains unclamped at the stamp boundary" )
{
    SkullbonezCore::Core::EngineConfig config;
    config.physicsMaterial.sphereDragCoeff = 1.1f;
    config.physicsMaterial.frictionCoeff = 1.2f;
    config.physicsMaterial.objectFrictionCoeff = 1.3f;
    config.physicsMaterial.rollingFrictionCoeff = 1.4f;
    config.physicsMaterial.spinFrictionCoeff = 1.5f;
    config.bodySimulation.velocityLimit = 2.1f;
    config.bodySimulation.contactRestitutionThreshold = 2.2f;
    config.bodySimulation.contactEpsilon = 2.3f;
    config.persistentContactSolver.slop = -3.1f;
    config.persistentContactSolver.baumgarteBeta = -3.2f;
    config.persistentContactSolver.positionCorrectionPercent = 3.3f;
    config.persistentContactSolver.iterations = -34;
    config.terrainContact.threshold = 4.1f;
    config.terrainContact.slop = -4.2f;
    config.terrainContact.baumgarteBeta = -4.3f;
    config.terrainContact.maxBaumgarteBias = -4.4f;
    config.physicsSleep.linearSpeed = -5.1f;
    config.physicsSleep.angularSpeed = -5.2f;
    config.physicsSleep.frames = -53;
    config.broadphase.cellSize = -6.1f;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = true;
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = true;
    config.physicsExecution.parallelIntegrate = false;
    config.worldForces.gravity = 7.1f;

    // Invariant: stamping records provenance exactly. Sleep, solver, and
    // broadphase owners retain the single authoritative clamp sites.
    CheckRuntimeSettingsMatchConfig( PhysicsEngine::RuntimeSettingsFromConfig( config ), config );
}

TEST_CASE( "Physics runtime settings: execution switches preserve one-hot provenance" )
{
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = false;

    auto checkOneHot = [&]()
    { CheckRuntimeSettingsMatchConfig( PhysicsEngine::RuntimeSettingsFromConfig( config ), config ); };

    config.physicsExecution.parallel = true;
    checkOneHot();
    config.physicsExecution.parallel = false;
    config.physicsExecution.parallelApplyForces = true;
    checkOneHot();
    config.physicsExecution.parallelApplyForces = false;
    config.physicsExecution.parallelMutualGravity = true;
    checkOneHot();
    config.physicsExecution.parallelMutualGravity = false;
    config.physicsExecution.parallelNarrowphase = true;
    checkOneHot();
    config.physicsExecution.parallelNarrowphase = false;
    config.physicsExecution.parallelTerrainDetect = true;
    checkOneHot();
    config.physicsExecution.parallelTerrainDetect = false;
    config.physicsExecution.parallelIntegrate = true;
    checkOneHot();
}

TEST_CASE( "Physics pipeline recorder: count-only and full modes share exact saturation" )
{
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsPipelineTraceRecorder;

    PhysicsPipelineTraceRecorder fullRecorder;
    PhysicsPipelineTraceRecorder countOnlyRecorder;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        fullRecorder.Reserve();
        countOnlyRecorder.Reserve();
    }

    fullRecorder.BeginStep( true );
    countOnlyRecorder.BeginStep( false );
    const auto makeRecord = []( uint32_t row )
    {
        PhysicsPipelineRecord record;
        record.stage = row % 2u == 0u ? PhysicsPipelineStage::SleepIslandDecision
                                      : PhysicsPipelineStage::BroadphaseCandidate;
        record.bodyA = static_cast<int>( row );
        record.bodyB = -static_cast<int>( row ) - 1;
        record.iteration = static_cast<int>( row % 23u );
        record.featureId = 0x12340000u + row;
        record.point = Vector3( static_cast<float>( row ) + 0.25f, -static_cast<float>( row ) - 0.5f,
                                static_cast<float>( row ) + 0.75f );
        record.normal = Vector3( -0.5f, 0.25f, 0.75f );
        record.scalarA = static_cast<float>( row ) + 1.5f;
        record.scalarB = -static_cast<float>( row ) - 2.5f;
        record.scalarC = static_cast<float>( row ) + 3.5f;
        return record;
    };

    constexpr uint32_t recordLimit = SkullbonezCore::Physics::PHYSICS_MAX_PIPELINE_TRACE_RECORDS;

    for ( uint32_t row = 0u; row + 1u < recordLimit; ++row )
    {
        const PhysicsPipelineRecord record = makeRecord( row );
        fullRecorder.Record( record );
        countOnlyRecorder.Record( record );
    }

    CHECK( fullRecorder.Count() == recordLimit - 1u );
    CHECK( countOnlyRecorder.Count() == fullRecorder.Count() );
    CHECK( fullRecorder.RemainingRecordCapacity() == 1 );
    CHECK( countOnlyRecorder.RemainingRecordCapacity() == 1 );
    REQUIRE( fullRecorder.Records().size() == recordLimit - 1u );
    CHECK( countOnlyRecorder.Records().empty() );

    const PhysicsPipelineRecord ceilingRecord = makeRecord( recordLimit - 1u );
    fullRecorder.Record( ceilingRecord );
    countOnlyRecorder.Record( ceilingRecord );
    CHECK( fullRecorder.Count() == recordLimit );
    CHECK( countOnlyRecorder.Count() == recordLimit );
    REQUIRE( fullRecorder.Records().size() == recordLimit );
    CHECK( fullRecorder.Records().back().featureId == ceilingRecord.featureId );

    constexpr uint32_t overflowRows = 17u;

    for ( uint32_t row = recordLimit; row < recordLimit + overflowRows; ++row )
    {
        const PhysicsPipelineRecord record = makeRecord( row );
        fullRecorder.Record( record );
        countOnlyRecorder.Record( record );
    }

    CHECK( fullRecorder.Count() == recordLimit );
    CHECK( countOnlyRecorder.Count() == recordLimit );
    CHECK( fullRecorder.RemainingRecordCapacity() == 0 );
    CHECK( countOnlyRecorder.RemainingRecordCapacity() == 0 );
    CHECK( countOnlyRecorder.Records().empty() );

    const std::span<const PhysicsPipelineRecord> records = fullRecorder.Records();
    REQUIRE( records.size() == fullRecorder.Count() );
    const auto checkRecord = [&]( uint32_t row )
    {
        const PhysicsPipelineRecord expected = makeRecord( row );
        const PhysicsPipelineRecord& actual = records[row];
        CHECK( actual.stage == expected.stage );
        CHECK( actual.bodyA == expected.bodyA );
        CHECK( actual.bodyB == expected.bodyB );
        CHECK( actual.iteration == expected.iteration );
        CHECK( actual.featureId == expected.featureId );
        CHECK( actual.point.x == doctest::Approx( expected.point.x ) );
        CHECK( actual.point.y == doctest::Approx( expected.point.y ) );
        CHECK( actual.point.z == doctest::Approx( expected.point.z ) );
        CHECK( actual.normal.x == doctest::Approx( expected.normal.x ) );
        CHECK( actual.normal.y == doctest::Approx( expected.normal.y ) );
        CHECK( actual.normal.z == doctest::Approx( expected.normal.z ) );
        CHECK( actual.scalarA == doctest::Approx( expected.scalarA ) );
        CHECK( actual.scalarB == doctest::Approx( expected.scalarB ) );
        CHECK( actual.scalarC == doctest::Approx( expected.scalarC ) );
    };
    checkRecord( 0u );
    checkRecord( recordLimit / 2u );
    checkRecord( recordLimit - 1u );

    fullRecorder.BeginStep( true );
    countOnlyRecorder.BeginStep( false );
    CHECK( fullRecorder.Count() == 0u );
    CHECK( countOnlyRecorder.Count() == 0u );
    CHECK( fullRecorder.Records().empty() );
    CHECK( countOnlyRecorder.Records().empty() );

    PhysicsPipelineTraceRecorder batchedCountRecorder;
    batchedCountRecorder.BeginStep( false );
    batchedCountRecorder.RecordEvents( recordLimit - 1u );
    CHECK( batchedCountRecorder.Count() == recordLimit - 1u );
    CHECK( batchedCountRecorder.RemainingRecordCapacity() == 1 );
    batchedCountRecorder.RecordEvents( 1u );
    CHECK( batchedCountRecorder.Count() == recordLimit );
    batchedCountRecorder.RecordEvents( overflowRows );
    CHECK( batchedCountRecorder.Count() == recordLimit );
    CHECK( batchedCountRecorder.Records().empty() );
}

TEST_CASE( "Physics step diagnostics: consumer selection keeps counting active in both modes" )
{
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsStepDiagnostics;

    // Why: the diagnostics owner contains fixed-capacity Debug storage. Static
    // placement mirrors its runtime lifetime without consuming the doctest stack.
    static PhysicsStepDiagnostics diagnostics;
    diagnostics.Clear();
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        diagnostics.ReserveSceneCapacity( 0u );
    }

    PhysicsPipelineRecord record;
    record.stage = PhysicsPipelineStage::SleepIslandDecision;
    record.bodyA = 7;
    record.featureId = 0xabcdu;

    diagnostics.SetPipelineTraceFullRecordConsumerActive( false );
    diagnostics.BeginStep( 0 );
    diagnostics.RecordPipelineStage( record );
    CHECK( diagnostics.GetPipelineRecordCount() == 1u );
    CHECK( diagnostics.GetPipelineTrace().empty() );

    diagnostics.SetPipelineTraceFullRecordConsumerActive( true );
    diagnostics.BeginStep( 0 );
    diagnostics.RecordPipelineStage( record );
    CHECK( diagnostics.GetPipelineRecordCount() == 1u );
    REQUIRE( diagnostics.GetPipelineTrace().size() == 1u );
    CHECK( diagnostics.GetPipelineTrace().front().stage == record.stage );
    CHECK( diagnostics.GetPipelineTrace().front().bodyA == record.bodyA );
    CHECK( diagnostics.GetPipelineTrace().front().featureId == record.featureId );
}

TEST_CASE( "Physics sleep policy: thresholds square after clamp and frame count saturates at 255" )
{
    PhysicsSleepController controller;
    ReserveTestSleepCapacity( controller );
    SkullbonezCore::Physics::SleepSettings config;
    config.linearSpeed = -2.0f;
    config.angularSpeed = 3.0f;
    config.frames = 999;

    const auto policy = controller.ResolveStepPolicy( config );

    CHECK( policy.linearSpeedSquared == doctest::Approx( 0.0f ) );
    CHECK( policy.angularSpeedSquared == doctest::Approx( 9.0f ) );
    CHECK( policy.frameCount == 255u );
    config.frames = 0;
    CHECK( controller.ResolveStepPolicy( config ).frameCount == 1u );
}

TEST_CASE( "Physics sleep support: fixed anchor propagates through a chained island" )
{
    PhysicsBodyStore& bodies = StageBodyStore();

    for ( int bodyIndex = 0; bodyIndex < 3; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.hot.fixed = bodyIndex == 0;
        (void)bodies.CreateBodyRecord( body );
    }

    PhysicsSleepController controller;
    ReserveTestSleepCapacity( controller );
    CHECK( controller.MirrorFlagsFrom( bodies, 3 ) );
    CHECK( controller.GetAwakeBodyCount() == 2 );
    auto& edges = controller.MutableSupportEdgesForContactSolver();
    edges.emplace_back( 0, 1 );
    edges.emplace_back( 1, 2 );

    controller.PropagateSupport( bodies );

    const auto supported = controller.GetSleepSupportedStates();
    REQUIRE( supported.size() == 3u );
    CHECK( supported[0] == 1u );
    CHECK( supported[1] == 1u );
    CHECK( supported[2] == 1u );
}

TEST_CASE( "Physics sleep awake list: transitions and queued wakes preserve ascending dense order" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();

    for ( int bodyIndex = 0; bodyIndex < 3; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = bodyIndex == 0 ? 0.0f : 1.0f;
        body.hot.fixed = bodyIndex == 0;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = 1.0f;
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere );
    }

    PhysicsSleepController controller;
    ReserveTestSleepCapacity( controller );
    CHECK( controller.MirrorFlagsFrom( bodies, 3 ) );
    CHECK_FALSE( controller.MirrorFlagsFrom( bodies, 3 ) );
    CHECK( std::vector<int>( controller.GetAwakeBodyIndices().begin(), controller.GetAwakeBodyIndices().end() ) ==
           std::vector<int> { 1, 2 } );

    controller.SeedModelAsleep( bodies, 1 );
    REQUIRE( controller.GetAwakeBodyIndices().size() == 1u );
    CHECK( controller.GetAwakeBodyIndices()[0] == 2 );

    PhysicsWorldForces worldForces;
    std::array<BuoyancyBodyFacts, 3> buoyancyFacts;
    std::array<float, 3> timeRemaining = { 1.0f / 120.0f, 1.0f / 120.0f, 1.0f / 120.0f };
    const auto wakeAccess = controller.CreateNarrowphaseWakeAccess( bodies, colliders, {}, worldForces, buoyancyFacts,
                                                                    bodies.MutableRecords(), timeRemaining, 3,
                                                                    1.0f / 120.0f );
    wakeAccess.WakeBody( 1 );
    CHECK( controller.GetAwakeBodyIndices().size() == 1u );
    controller.FlushPendingAwakeBodyIndices();
    CHECK( std::vector<int>( controller.GetAwakeBodyIndices().begin(), controller.GetAwakeBodyIndices().end() ) ==
           std::vector<int> { 1, 2 } );

    // A cold fixed/dynamic edit can change list membership without changing
    // body count; invalidation makes the next owner mirror rebuild it.
    bodies.MutableHotFields().fixed[1] = 1u;
    controller.InvalidateBodyTopology();
    CHECK( controller.MirrorFlagsFrom( bodies, 3 ) );
    REQUIRE( controller.GetAwakeBodyIndices().size() == 1u );
    CHECK( controller.GetAwakeBodyIndices()[0] == 2 );
}

TEST_CASE( "Physics sleep counters: unsupported or nonquiet state abandons a positive quiet run" )
{
    for ( const bool loseSupport : { true, false } )
    {
        PhysicsBodyStore& bodies = StageBodyStore();
        ColliderStore& colliders = StageColliderStore();
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        (void)bodies.CreateBodyRecord( body );

        PhysicsSleepController controller;
        ReserveTestSleepCapacity( controller );
        REQUIRE( controller.MirrorFlagsFrom( bodies, 1 ) );
        controller.MutableSupportedStatesForTerrain()[0] = 1u;

        std::array<float, 1> timeRemaining = { 1.0f / 120.0f };
        std::array<BuoyancyBodyFacts, 1> buoyancyFacts;
        const std::vector<SkullbonezCore::Physics::PersistentContact> contacts;
        const std::array<uint16_t, 1> restingCounts = { 0u };
        const std::vector<SkullbonezCore::Physics::PointJointConstraint> joints;
        SkullbonezCore::Physics::PhysicsPipelineTraceRecorder pipeline;
        {
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
            pipeline.Reserve();
        }
        PhysicsWorldForces worldForces;
        const SkullbonezCore::Physics::PhysicsSleepStepPolicy sleepPolicy { 0.25f * 0.25f, 0.25f * 0.25f, 3u };

        controller.RunIslandStage( bodies, colliders, worldForces, buoyancyFacts, timeRemaining, contacts, restingCounts,
                                   joints, pipeline, sleepPolicy );
        REQUIRE( controller.GetSleepCounters()[0] == 1u );

        if ( loseSupport )
        {
            controller.MutableSupportedStatesForTerrain()[0] = 0u;
        }
        else
        {
            bodies.MutableHotFields().linearVelocityX[0] = 1.0f;
        }

        controller.RunIslandStage( bodies, colliders, worldForces, buoyancyFacts, timeRemaining, contacts, restingCounts,
                                   joints, pipeline, sleepPolicy );
        CHECK( controller.GetSleepCounters()[0] == 0u );
        CHECK( controller.GetSleepStates()[0] == 0u );
    }
}

TEST_CASE( "Physics sleep wake: repeated material-impact requests publish one awake transition" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    PhysicsBodyCreateRecord body;
    body.cold.mass = 1.0f;
    body.hot.inverseMass = 1.0f;
    const auto handle = bodies.CreateBodyRecord( body );
    ColliderRecord collider;
    collider.body = handle;
    collider.boundingRadius = 1.0f;
    SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere );

    PhysicsSleepController controller;
    ReserveTestSleepCapacity( controller );
    REQUIRE( controller.MirrorFlagsFrom( bodies, 1 ) );
    REQUIRE( bodies.SeedBodyAsleep( handle ) );
    controller.SeedModelAsleep( bodies, 0 );
    REQUIRE( controller.GetAwakeBodyIndices().empty() );

    PhysicsWorldForces worldForces;
    std::array<BuoyancyBodyFacts, 1> buoyancyFacts;
    std::array<float, 1> timeRemaining = { 1.0f / 120.0f };
    const auto wakeAccess = controller.CreateNarrowphaseWakeAccess( bodies, colliders, {}, worldForces, buoyancyFacts,
                                                                    bodies.MutableRecords(), timeRemaining, 1,
                                                                    1.0f / 120.0f );

    // Narrowphase can rediscover the same material collision through multiple
    // rows; the first request owns the state transition and later requests are
    // idempotent before the pending awake index is flushed.
    wakeAccess.WakeBody( 0 );
    wakeAccess.WakeBody( 0 );
    CHECK( controller.GetAwakeBodyIndices().empty() );
    controller.FlushPendingAwakeBodyIndices();
    REQUIRE( controller.GetAwakeBodyIndices().size() == 1u );
    CHECK( controller.GetAwakeBodyIndices()[0] == 0 );
    CHECK( bodies.HotFields().awake[0] == 1u );
}

TEST_CASE( "Physics sleep underwater lock: fully submerged sleeper locks and disabling sleep clears it" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    const auto
        desc = SkullbonezCore::Physics::MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( 91u ), sphere,
                                                                   Vector3( 0.0f, 0.0f, 0.0f ),
                                                                   SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                                                   Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 4.0f, 5.0f, 6.0f ),
                                                                   Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                                                                   PhysicsBodyMotionKind::Dynamic );
    const auto handle = bodies.CreateBodyRecord( desc, true );
    ColliderRecord collider;
    collider.body = handle;
    collider.sceneObjectId = MakePhysicsSceneObjectId( 91u );
    collider.boundingRadius = 1.0f;
    SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere );
    PhysicsSleepController controller;
    ReserveTestSleepCapacity( controller );
    controller.MirrorFlagsFrom( bodies, 1 );
    REQUIRE( bodies.SeedBodyAsleep( handle ) );
    controller.SeedModelAsleep( bodies, 0 );

    // A cold same-count topology boundary must retain the authored sleeper and
    // schedule it for the owning world's one-shot submerged census.
    controller.InvalidateBodyTopology();
    CHECK( controller.MirrorFlagsFrom( bodies, 1 ) );
    PhysicsWorldForces worldForces;
    worldForces.fluidSurfaceHeight = 10.0f;
    worldForces.fluidDensity = 1000.0f;
    std::array<float, 1> timeRemaining = { 1.0f / 120.0f };
    std::array<BuoyancyBodyFacts, 1> buoyancyFacts;

    controller.LockUnderwaterSleeperIfReady( worldForces, bodies, colliders, buoyancyFacts, timeRemaining, 0 );

    REQUIRE( controller.GetUnderwaterSleepLocks().size() == 1u );
    CHECK( controller.GetUnderwaterSleepLocks()[0] == 1u );
    CHECK( timeRemaining[0] == doctest::Approx( 0.0f ) );
    CHECK( bodies.HotFields().awake[0] == 0u );
    controller.SetPhysicsSleepEnabled( false );
    CHECK( controller.GetUnderwaterSleepLocks()[0] == 0u );
    CHECK( controller.GetSleepStates()[0] == 0u );
}

TEST_CASE( "Physics sleep awake list: one-frame transitions visit every row while compacting the list" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();

    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = 1.0f;
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere );
    }

    PhysicsSleepController controller;
    ReserveTestSleepCapacity( controller );
    REQUIRE( controller.MirrorFlagsFrom( bodies, 4 ) );
    std::fill( controller.MutableSupportedStatesForTerrain().begin(), controller.MutableSupportedStatesForTerrain().end(),
               static_cast<uint8_t>( 1u ) );
    std::array<float, 4> timeRemaining = { 1.0f, 1.0f, 1.0f, 1.0f };
    const std::vector<SkullbonezCore::Physics::PersistentContact> contacts;
    const std::array<uint16_t, 4> restingCounts = { 0u, 0u, 0u, 0u };
    const std::vector<SkullbonezCore::Physics::PointJointConstraint> joints;
    SkullbonezCore::Physics::PhysicsPipelineTraceRecorder pipeline;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        pipeline.Reserve();
    }
    PhysicsWorldForces worldForces;
    std::array<BuoyancyBodyFacts, 4> buoyancyFacts;
    const SkullbonezCore::Physics::PhysicsSleepStepPolicy sleepPolicy { 0.01f, 0.01f, 1u };
    controller.RunIslandStage( bodies, colliders, worldForces, buoyancyFacts, timeRemaining, contacts, restingCounts, joints,
                               pipeline, sleepPolicy );

    CHECK( controller.GetAwakeBodyIndices().empty() );
    REQUIRE( controller.GetSleepStates().size() == 4u );

    for ( uint8_t sleepState : controller.GetSleepStates() )
    {
        CHECK( sleepState == 1u );
    }
}

TEST_CASE( "Physics sleep point-joint island: stretched anchors block relaxation while slack anchors remain eligible" )
{
    // Concept: the pipeline decision row is the public diagnostic for why an
    // otherwise quiet, fixed-anchored island could not advance toward sleep.
    for ( const bool retainPipelineRecords : { true, false } )
    {
        for ( const bool stretched : { false, true } )
        {
            PhysicsBodyStore& bodies = StageBodyStore();
            ColliderStore& colliders = StageColliderStore();
            PhysicsBodyCreateRecord anchor;
            anchor.hot.fixed = true;
            const auto anchorHandle = bodies.CreateBodyRecord( anchor );
            PhysicsBodyCreateRecord dynamic;
            dynamic.hot.position = Vector3( stretched ? 2.0f : 0.1f, 0.0f, 0.0f );
            const auto dynamicHandle = bodies.CreateBodyRecord( dynamic );

            SkullbonezCore::Physics::PointJointConstraint joint;
            joint.SetBodies( anchorHandle, dynamicHandle );
            joint.slack = 0.25f;
            const std::vector<SkullbonezCore::Physics::PointJointConstraint> joints = { joint };
            const std::vector<SkullbonezCore::Physics::PersistentContact> contacts;
            std::array<float, 2> timeRemaining = { 1.0f / 120.0f, 1.0f / 120.0f };
            std::array<uint16_t, 2> restingCounts = { 0u, 0u };
            SkullbonezCore::Physics::PhysicsPipelineTraceRecorder pipeline;
            {
                SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
                pipeline.Reserve();
            }
            pipeline.BeginStep( retainPipelineRecords );
            PhysicsWorldForces worldForces;
            std::array<BuoyancyBodyFacts, 2> buoyancyFacts;
            PhysicsSleepController controller;
            ReserveTestSleepCapacity( controller );
            controller.MirrorFlagsFrom( bodies, 2 );
            const SkullbonezCore::Physics::PhysicsSleepStepPolicy sleepPolicy { 0.01f, 0.01f, 3u };
            controller.RunIslandStage( bodies, colliders, worldForces, buoyancyFacts, timeRemaining, contacts, restingCounts,
                                       joints, pipeline, sleepPolicy );

            CHECK( pipeline.Count() == 1u );
            const std::span<const SkullbonezCore::Physics::PhysicsPipelineRecord> records = pipeline.Records();
            REQUIRE( records.size() == ( retainPipelineRecords ? 1u : 0u ) );

            if ( retainPipelineRecords )
            {
                CHECK( records[0].stage == SkullbonezCore::Physics::PhysicsPipelineStage::SleepIslandDecision );
                CHECK( records[0].bodyA == 1 );
                CHECK( records[0].scalarB == doctest::Approx( 1.0f ) );
                CHECK( records[0].scalarC == doctest::Approx( stretched ? 2.0f : 0.0f ) );
            }

            CHECK( controller.GetSleepCounters()[1] == ( stretched ? 0u : 1u ) );
        }
    }
}

TEST_CASE( "Physics narrowphase islands: repeated parallel evaluation preserves original pair slots" )
{
    constexpr int kPairCount = 256;
    constexpr int kBodyCount = kPairCount * 2;
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    std::vector<std::pair<int, int>> candidatePairs;
    candidatePairs.reserve( kPairCount );

    for ( int bodyIndex = 0; bodyIndex < kBodyCount; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.position = Vector3( static_cast<float>( bodyIndex * 4 ), 0.0f, 0.0f );
        const auto handle = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = 1.0f;
        SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere );

        if ( ( bodyIndex & 1 ) != 0 )
        {
            candidatePairs.emplace_back( bodyIndex - 1, bodyIndex );
        }
    }

    PhysicsSleepController sleep;
    ReserveTestSleepCapacity( sleep );
    sleep.MirrorFlagsFrom( bodies, kBodyCount );
    std::vector<float> timeRemaining( kBodyCount, 1.0f / 120.0f );
    PhysicsWorldForces worldForces;
    std::vector<BuoyancyBodyFacts> buoyancyFacts( kBodyCount );
    std::vector<uint8_t> motionEligibilityState( kBodyCount,
                                                 SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted );
    const auto wakeAccess = sleep.CreateNarrowphaseWakeAccess( bodies, colliders, {}, worldForces, buoyancyFacts,
                                                               bodies.MutableRecords(), timeRemaining, kBodyCount,
                                                               1.0f / 120.0f );
    const ObjectNarrowphaseStepPolicy policy { 0.25f, 0.09f, 0.01f, 1.0f / 24.0f, 1.0f / 120.0f, true, true, true };
    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    workerPool.Initialise( 1 );
    PhysicsNarrowphaseStage stage;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveSceneCapacity( kBodyCount );
    }

    REQUIRE( stage.TryRunParallel( bodies, colliders, {}, buoyancyFacts, candidatePairs, wakeAccess, timeRemaining,
                                   motionEligibilityState, policy, nullptr, workerPool ) );
    const std::vector<ObjectNarrowphaseEvent> first( stage.GetEvents().begin(), stage.GetEvents().end() );
    REQUIRE( stage.TryRunParallel( bodies, colliders, {}, buoyancyFacts, candidatePairs, wakeAccess, timeRemaining,
                                   motionEligibilityState, policy, nullptr, workerPool ) );
    const std::vector<ObjectNarrowphaseEvent> second( stage.GetEvents().begin(), stage.GetEvents().end() );

    ObjectNarrowphaseStepPolicy countOnlyPolicy = policy;
    countOnlyPolicy.retainPipelineRecords = false;
    REQUIRE( stage.TryRunParallel( bodies, colliders, {}, buoyancyFacts, candidatePairs, wakeAccess, timeRemaining,
                                   motionEligibilityState, countOnlyPolicy, nullptr, workerPool ) );
    const std::vector<ObjectNarrowphaseEvent> countOnly( stage.GetEvents().begin(), stage.GetEvents().end() );

    REQUIRE( first.size() == kPairCount );
    REQUIRE( second.size() == first.size() );
    REQUIRE( countOnly.size() == first.size() );

    for ( int pairIndex = 0; pairIndex < kPairCount; ++pairIndex )
    {
        const auto& expectedPair = candidatePairs[static_cast<size_t>( pairIndex )];
        CHECK( first[static_cast<size_t>( pairIndex )].kind == ObjectNarrowphaseEventKind::SweptObjectMiss );
        CHECK( second[static_cast<size_t>( pairIndex )].kind == first[static_cast<size_t>( pairIndex )].kind );
        CHECK( second[static_cast<size_t>( pairIndex )].hasPipelineEvent == 1u );
        REQUIRE( second[static_cast<size_t>( pairIndex )].pipelineRecord.has_value() );
        CHECK( second[static_cast<size_t>( pairIndex )].pipelineRecord->bodyA == expectedPair.first );
        CHECK( second[static_cast<size_t>( pairIndex )].pipelineRecord->bodyB == expectedPair.second );
        CHECK( countOnly[static_cast<size_t>( pairIndex )].kind == first[static_cast<size_t>( pairIndex )].kind );
        CHECK( countOnly[static_cast<size_t>( pairIndex )].hasPipelineEvent == 1u );
        CHECK_FALSE( countOnly[static_cast<size_t>( pairIndex )].pipelineRecord.has_value() );
    }
}


TEST_CASE( "Physics narrowphase collision events preserve full-width spatial cell keys" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = BoundingSphere( 0.5f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f );
    const Vector3 positions[] = { Vector3( 20000.0f, 0.0f, 0.0f ), Vector3( 20002.0f, 0.0f, 0.0f ) };

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.position = positions[bodyIndex];
        body.hot.linearVelocity = bodyIndex == 0 ? Vector3( 120.0f, 0.0f, 0.0f ) : SkullbonezCore::Math::Vector::ZERO_VECTOR;
        const auto handle = bodies.CreateBodyRecord( body );
        REQUIRE( handle.IsValid() );
        ColliderRecord collider;
        collider.body = handle;
        collider.boundingRadius = 0.5f;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere ).IsValid() );
    }

    PhysicsSleepController sleep;
    ReserveTestSleepCapacity( sleep );
    sleep.MirrorFlagsFrom( bodies, 2 );
    std::array<float, 2> timeRemaining = { 1.0f / 120.0f, 1.0f / 120.0f };
    std::array<BuoyancyBodyFacts, 2> buoyancyFacts;
    PhysicsWorldForces worldForces;
    const auto wakeAccess = sleep.CreateNarrowphaseWakeAccess( bodies, colliders, {}, worldForces, buoyancyFacts,
                                                               bodies.MutableRecords(), timeRemaining, 2, 1.0f / 120.0f );
    const std::array<std::pair<int, int>, 1> candidatePairs = { std::make_pair( 0, 1 ) };
    const std::array<uint8_t, 2> motionEligibilityState = {
        SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted,
        SkullbonezCore::Physics::PhysicsMotionEligibilityNone,
    };
    const ObjectNarrowphaseStepPolicy policy { 0.25f, 0.09f, 0.05f, 2.0f, 1.0f / 120.0f, true, false, true };
    PhysicsNarrowphaseStage stage;
    ObjectNarrowphaseEvent event;
    stage.ProcessObjectNarrowphasePair<true>( bodies, colliders, {}, buoyancyFacts, candidatePairs, wakeAccess,
                                              timeRemaining, motionEligibilityState, policy, nullptr, 0, event );

    REQUIRE( event.kind == ObjectNarrowphaseEventKind::SweptObjectHit );
    REQUIRE( event.hasCollisionCellKey == 1u );
    const auto hotFields = bodies.HotFields();
    const Vector3 midpoint = ( SkullbonezCore::Physics::PhysicsBodyPosition( hotFields, 0u ) +
                               SkullbonezCore::Physics::PhysicsBodyPosition( hotFields, 1u ) ) *
                             0.5f;
    const int exactCellX = static_cast<int>( floorf( midpoint.x * policy.invCellSize ) );
    REQUIRE( exactCellX > ( std::numeric_limits<int16_t>::max )() );
    CHECK( event.collisionCellKey == SkullbonezCore::Physics::EncodeExactSpatialCellKey( exactCellX, 0, 0 ) );
}


TEST_CASE( "Physics motion promotion: promoted swept impact wakes a sleeping target" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = BoundingSphere( 0.5f, SkullbonezCore::Math::Vector::ZERO_VECTOR, 0.0f );
    std::array<SkullbonezCore::Physics::PhysicsBodyHandle, 2> handles;

    for ( int bodyIndex = 0; bodyIndex < 2; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.position = Vector3( bodyIndex == 0 ? -2.0f : 0.0f, 0.0f, 0.0f );
        body.hot.linearVelocity = bodyIndex == 0 ? Vector3( 240.0f, 0.0f, 0.0f ) : SkullbonezCore::Math::Vector::ZERO_VECTOR;
        handles[static_cast<std::size_t>( bodyIndex )] = bodies.CreateBodyRecord( body );
        ColliderRecord collider;
        collider.body = handles[static_cast<std::size_t>( bodyIndex )];
        collider.boundingRadius = 0.5f;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, sphere ).IsValid() );
    }

    PhysicsSleepController sleep;
    ReserveTestSleepCapacity( sleep );
    REQUIRE( sleep.MirrorFlagsFrom( bodies, 2 ) );
    REQUIRE( bodies.SeedBodyAsleep( handles[1] ) );
    sleep.SeedModelAsleep( bodies, 1 );
    REQUIRE( sleep.GetSleepStates()[1] != 0u );
    std::array<float, 2> timeRemaining = { 1.0f / 120.0f, 1.0f / 120.0f };
    std::array<BuoyancyBodyFacts, 2> buoyancyFacts;
    PhysicsWorldForces worldForces;
    const auto wakeAccess = sleep.CreateNarrowphaseWakeAccess( bodies, colliders, {}, worldForces, buoyancyFacts,
                                                               bodies.MutableRecords(), timeRemaining, 2, 1.0f / 120.0f );
    const std::array<std::pair<int, int>, 1> candidatePairs = { std::make_pair( 0, 1 ) };
    const std::array<uint8_t, 2> motionEligibilityState = {
        SkullbonezCore::Physics::PhysicsMotionEligibilityLinearPromoted,
        SkullbonezCore::Physics::PhysicsMotionEligibilityNone,
    };
    const ObjectNarrowphaseStepPolicy policy { 0.25f, 0.09f, 0.05f, 2.0f, 1.0f / 120.0f, true, false, true };
    PhysicsNarrowphaseStage stage;
    ObjectNarrowphaseEvent event;

    stage.ProcessObjectNarrowphasePair<true>( bodies, colliders, {}, buoyancyFacts, candidatePairs, wakeAccess,
                                              timeRemaining, motionEligibilityState, policy, nullptr, 0, event );

    REQUIRE( event.kind == ObjectNarrowphaseEventKind::SweptObjectHit );
    CHECK( sleep.GetSleepStates()[1] == 0u );
    CHECK( timeRemaining[0] < 1.0f / 120.0f );
}
