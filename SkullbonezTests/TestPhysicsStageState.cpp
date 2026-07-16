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
//
// Invariants:
//   - Sleep frame counts clamp to uint8 storage without wrapping.
//   - Support propagates to a fixed point through model-order edges.
//   - Parallel narrowphase scheduling cannot reorder pair-slot results.
//
// Related:
//   - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
//   - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
//   - SkullbonezSource/Physics/SleepIslandSystem.cpp
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsSleepController.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakePhysicsSceneObjectIdFromReplayBodyId;
using SkullbonezCore::Physics::ObjectNarrowphaseEvent;
using SkullbonezCore::Physics::ObjectNarrowphaseEventKind;
using SkullbonezCore::Physics::ObjectNarrowphasePairStageContext;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsNarrowphaseStage;
using SkullbonezCore::Physics::PhysicsSleepController;
using SkullbonezCore::Physics::PhysicsWorldForces;
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
} // namespace

TEST_CASE( "Physics sleep policy: thresholds square after clamp and frame count saturates at 255" )
{
    PhysicsSleepController controller;
    SkullbonezCore::Core::PhysicsSleepConfig config;
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
    controller.MirrorFlagsFrom( bodies, 3 );
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

TEST_CASE( "Physics sleep underwater lock: fully submerged sleeper locks and disabling sleep clears it" )
{
    PhysicsBodyStore& bodies = StageBodyStore();
    ColliderStore& colliders = StageColliderStore();
    const CollisionShape sphere = UnitSphere();
    const auto desc = SkullbonezCore::Physics::MakePhysicsBodyCreateDesc(
        MakePhysicsSceneObjectIdFromReplayBodyId( 91u ),
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
    collider.replayBodyId = 91u;
    collider.shape = sphere;
    collider.boundingRadius = 1.0f;
    colliders.CreateColliderRecord( collider );
    PhysicsSleepController controller;
    controller.MirrorFlagsFrom( bodies, 1 );
    controller.SeedModelAsleep( bodies, 0 );
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
    const auto wakeAccess = sleep.CreateNarrowphaseWakeAccess(
        bodies, colliders, worldForces, bodies.MutableRecords(), timeRemaining, kBodyCount, 1.0f / 120.0f );
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
    SkullbonezCore::Core::PhysicsExecutionConfig execution;
    execution.parallel = true;
    execution.parallelNarrowphase = true;
    WorkerPool workerPool;
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
