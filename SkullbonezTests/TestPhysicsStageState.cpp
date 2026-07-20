//
// File: SkullbonezTests/TestPhysicsStageState.cpp
// Purpose:
//   Locks sleep-owner transitions and narrowphase island determinism.
//
// Summary:
//   Sleep policy is model-order state owned by PhysicsSleepController. These
//   tests drive its public value seams and a disjoint parallel narrowphase pass
//   without constructing PhysicsWorld or a scene owner.
//
// Glossary:
//   Support edge: Directed relation from a grounded supporter to a supported body.
//   Underwater lock: Dormancy state that prevents a fully submerged ball from jitter-waking.
//   Pair slot: Event row whose index remains identical to the broadphase candidate index.
//   Runtime settings snapshot: Physics-owned copy stamped once from process
//     configuration before fixed-step consumers borrow it.
//   Point-joint relaxation: Anchor-distance check that prevents a stretched
//     constraint island from sleeping before the joint settles inside slack.
//
// Invariants:
//   - Sleep frame counts clamp to uint8 storage without wrapping.
//   - Support propagates to a fixed point through model-order edges.
//   - A stretched point joint publishes the sleep-block reason without
//     advancing its island counter; a relaxed joint remains eligible.
//   - Awake indices remain sorted across sleep, parallel-wake flush, and cold
//     topology rebuild boundaries.
//   - Parallel narrowphase scheduling cannot reorder pair-slot results.
//   - Config stamping copies every Physics-owned source field without clamping;
//     clamp policy remains at the consuming owner boundary.
//
// Related:
//   - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
//   - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
//   - SkullbonezSource/Physics/PhysicsRuntimeSettings.h
//   - SkullbonezSource/Physics/SleepIslandSystem.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsSleepController.h"

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::ObjectNarrowphaseEvent;
using SkullbonezCore::Physics::ObjectNarrowphaseEventKind;
using SkullbonezCore::Physics::ObjectNarrowphasePairStageContext;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsNarrowphaseStage;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Physics::PhysicsRuntimeSettings;
using SkullbonezCore::Physics::PhysicsSleepController;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
PhysicsBodyStore& StageBodyStore()
{
    // Lifetime: the fixed-capacity store is process-owned. Keeping only its
    // pointer in the image avoids inflating the Debug PE's SizeOfImage beyond
    // the Windows loader limit while retaining one reusable test allocation.
    static const std::unique_ptr<PhysicsBodyStore> store = std::make_unique<PhysicsBodyStore>();
    store->Clear();
    return *store;
}

ColliderStore& StageColliderStore()
{
    // Lifetime: see StageBodyStore; the collider capacity is similarly owned
    // by the test process rather than embedded as initialized image data.
    static const std::unique_ptr<ColliderStore> store = std::make_unique<ColliderStore>();
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

TEST_CASE( "Physics sleep policy: thresholds square after clamp and frame count saturates at 255" )
{
    PhysicsSleepController controller;
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
        collider.shape = sphere;
        collider.boundingRadius = 1.0f;
        colliders.CreateColliderRecord( collider );
    }

    PhysicsSleepController controller;
    CHECK( controller.MirrorFlagsFrom( bodies, 3 ) );
    CHECK_FALSE( controller.MirrorFlagsFrom( bodies, 3 ) );
    CHECK( std::vector<int>( controller.GetAwakeBodyIndices().begin(), controller.GetAwakeBodyIndices().end() ) ==
           std::vector<int>{ 1, 2 } );

    controller.SeedModelAsleep( bodies, 1 );
    REQUIRE( controller.GetAwakeBodyIndices().size() == 1u );
    CHECK( controller.GetAwakeBodyIndices()[0] == 2 );

    PhysicsWorldForces worldForces;
    std::array<float, 3> timeRemaining = { 1.0f / 120.0f, 1.0f / 120.0f, 1.0f / 120.0f };
    const auto wakeAccess = controller.CreateNarrowphaseWakeAccess( bodies,
                                                                    colliders,
                                                                    worldForces,
                                                                    bodies.MutableRecords(),
                                                                    timeRemaining,
                                                                    3,
                                                                    1.0f / 120.0f );
    wakeAccess.WakeBody( 1 );
    CHECK( controller.GetAwakeBodyIndices().size() == 1u );
    controller.FlushPendingAwakeBodyIndices();
    CHECK( std::vector<int>( controller.GetAwakeBodyIndices().begin(), controller.GetAwakeBodyIndices().end() ) ==
           std::vector<int>{ 1, 2 } );

    // A cold fixed/dynamic edit can change list membership without changing
    // body count; invalidation makes the next owner mirror rebuild it.
    bodies.MutableHotFields().fixed[1] = 1u;
    controller.InvalidateBodyTopology();
    CHECK( controller.MirrorFlagsFrom( bodies, 3 ) );
    REQUIRE( controller.GetAwakeBodyIndices().size() == 1u );
    CHECK( controller.GetAwakeBodyIndices()[0] == 2 );
}

TEST_CASE( "Physics sleep underwater lock: fully submerged sleeper locks and disabling sleep clears it" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    const auto desc =
        SkullbonezCore::Physics::MakePhysicsBodyCreateDesc( MakePhysicsSceneObjectId( 91u ),
                                                            sphere,
                                                            Vector3( 0.0f, 0.0f, 0.0f ),
                                                            SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                                            Vector3( 1.0f, 2.0f, 3.0f ),
                                                            Vector3( 4.0f, 5.0f, 6.0f ),
                                                            Vector3( 1.0f, 1.0f, 1.0f ),
                                                            1.0f,
                                                            0.0f,
                                                            PhysicsBodyMotionKind::Dynamic,
                                                            nullptr );
    const auto handle = bodies.CreateBodyRecord( desc, true );
    ColliderRecord collider;
    collider.body = handle;
    collider.sceneObjectId = MakePhysicsSceneObjectId( 91u );
    collider.shape = sphere;
    collider.boundingRadius = 1.0f;
    colliders.CreateColliderRecord( collider );
    PhysicsSleepController controller;
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

    controller.LockUnderwaterSleeperIfReady( worldForces, bodies, colliders, timeRemaining, 0 );

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
        collider.shape = sphere;
        collider.boundingRadius = 1.0f;
        colliders.CreateColliderRecord( collider );
    }

    PhysicsSleepController controller;
    REQUIRE( controller.MirrorFlagsFrom( bodies, 4 ) );
    std::fill( controller.MutableSupportedStatesForTerrain().begin(),
               controller.MutableSupportedStatesForTerrain().end(),
               static_cast<uint8_t>( 1u ) );
    std::array<float, 4> timeRemaining = { 1.0f, 1.0f, 1.0f, 1.0f };
    const std::vector<SkullbonezCore::Physics::PersistentContact> contacts;
    const std::array<uint16_t, 4> restingCounts = { 0u, 0u, 0u, 0u };
    const std::vector<SkullbonezCore::Physics::PointJointConstraint> joints;
    std::vector<SkullbonezCore::Physics::PhysicsPipelineRecord> pipeline;
    PhysicsWorldForces worldForces;
    const SkullbonezCore::Physics::PhysicsSleepIslandStageContext context{ bodies,
                                                                           colliders,
                                                                           worldForces,
                                                                           bodies.MutableRecords(),
                                                                           bodies.MutableHotFields(),
                                                                           timeRemaining,
                                                                           contacts,
                                                                           restingCounts,
                                                                           controller.GetAwakeBodyIndices(),
                                                                           joints,
                                                                           pipeline,
                                                                           4,
                                                                           0.01f,
                                                                           0.01f,
                                                                           1u };

    controller.RunIslandStage( context );

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
        std::vector<SkullbonezCore::Physics::PhysicsPipelineRecord> pipeline;
        PhysicsWorldForces worldForces;
        PhysicsSleepController controller;
        controller.MirrorFlagsFrom( bodies, 2 );
        const SkullbonezCore::Physics::PhysicsSleepIslandStageContext context{ bodies,
                                                                               colliders,
                                                                               worldForces,
                                                                               bodies.MutableRecords(),
                                                                               bodies.MutableHotFields(),
                                                                               timeRemaining,
                                                                               contacts,
                                                                               restingCounts,
                                                                               controller.GetAwakeBodyIndices(),
                                                                               joints,
                                                                               pipeline,
                                                                               2,
                                                                               0.01f,
                                                                               0.01f,
                                                                               3u };

        controller.RunIslandStage( context );

        REQUIRE( pipeline.size() == 1u );
        CHECK( pipeline[0].stage == SkullbonezCore::Physics::PhysicsPipelineStage::SleepIslandDecision );
        CHECK( pipeline[0].bodyA == 1 );
        CHECK( pipeline[0].scalarB == doctest::Approx( 1.0f ) );
        CHECK( pipeline[0].scalarC == doctest::Approx( stretched ? 2.0f : 0.0f ) );
        CHECK( controller.GetSleepCounters()[1] == ( stretched ? 0u : 1u ) );
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
        collider.shape = sphere;
        collider.boundingRadius = 1.0f;
        colliders.CreateColliderRecord( collider );
        if ( ( bodyIndex & 1 ) != 0 )
        {
            candidatePairs.emplace_back( bodyIndex - 1, bodyIndex );
        }
    }
    PhysicsSleepController sleep;
    sleep.MirrorFlagsFrom( bodies, kBodyCount );
    std::vector<float> timeRemaining( kBodyCount, 1.0f / 120.0f );
    PhysicsWorldForces worldForces;
    std::vector<SkullbonezCore::Physics::PersistentContactCacheEntry> persistentCache;
    const auto wakeAccess = sleep.CreateNarrowphaseWakeAccess( bodies,
                                                               colliders,
                                                               worldForces,
                                                               bodies.MutableRecords(),
                                                               timeRemaining,
                                                               kBodyCount,
                                                               1.0f / 120.0f );
    const ObjectNarrowphasePairStageContext context{ bodies,
                                                     colliders,
                                                     worldForces,
                                                     bodies.MutableRecords(),
                                                     bodies.HotFields(),
                                                     colliders.Records(),
                                                     candidatePairs,
                                                     wakeAccess,
                                                     sleep.GetSleepStates(),
                                                     timeRemaining,
                                                     sleep.GetUnderwaterSleepLocks(),
                                                     persistentCache,
                                                     kBodyCount,
                                                     0.25f,
                                                     0.09f,
                                                     0.01f,
                                                     0.05f,
                                                     1.0f / 24.0f,
                                                     1.0f / 120.0f };
    SkullbonezCore::Physics::PhysicsExecutionSettings execution;
    execution.parallel = true;
    execution.parallelNarrowphase = true;
    LockOrderValidator lockOrderValidator;
    WorkerPool workerPool( lockOrderValidator );
    workerPool.Initialise( 1 );
    PhysicsNarrowphaseStage stage;

    REQUIRE( stage.TryRunParallel( context, kPairCount, kBodyCount, execution, workerPool ) );
    const std::vector<ObjectNarrowphaseEvent> first( stage.GetEvents().begin(), stage.GetEvents().end() );
    REQUIRE( stage.TryRunParallel( context, kPairCount, kBodyCount, execution, workerPool ) );
    const auto second = stage.GetEvents();

    REQUIRE( first.size() == kPairCount );
    REQUIRE( second.size() == first.size() );
    for ( int pairIndex = 0; pairIndex < kPairCount; ++pairIndex )
    {
        const auto& expectedPair = candidatePairs[static_cast<size_t>( pairIndex )];
        CHECK( first[static_cast<size_t>( pairIndex )].kind == ObjectNarrowphaseEventKind::SweptObjectMiss );
        CHECK( second[static_cast<size_t>( pairIndex )].kind == first[static_cast<size_t>( pairIndex )].kind );
        CHECK( second[static_cast<size_t>( pairIndex )].pipelineRecord.bodyA == expectedPair.first );
        CHECK( second[static_cast<size_t>( pairIndex )].pipelineRecord.bodyB == expectedPair.second );
    }
}
