//
// File: SkullbonezTests/TestSolverBroadphaseStage.cpp
// Purpose:
//   Lock direct coverage for the scalar solver broadphase filter and retained
//   mutual-gravity stage boundaries.
//
// Summary:
//   SpatialGrid emits possible pairs from cell overlap. The solver broadphase
//   filter is a cheaper geometric pass that keeps only pairs whose swept
//   bounding spheres can touch this tick. The force-stage fixtures separately
//   lock fixed, sleeping, massless, and Newton-pair scalar behavior.
//
// Glossary:
//   Swept pair: Two bodies whose relative motion may close the gap before the
//     fixed tick ends.
//   Static separated pair: Bodies whose current spheres do not overlap and have
//     no relative motion that can close the distance.
//   Conservative accept: A pair kept intentionally because source data is not
//     trustworthy enough for safe rejection.
//
// Invariants:
//   - Out-of-range model indices are rejected before array access.
//   - Sleep-only pairs are rejected before geometric candidate work.
//   - Invalid broadphase radii are accepted, not rejected, so narrowphase keeps
//     authority over malformed or transitional shape data.
//
// Related:
//   - SkullbonezSource/Physics/SolverBroadphaseStage.h
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Physics/SolverBroadphaseStage.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsForceStage.h"

#include <array>
#include <cmath>
#include <limits>

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::CollisionShape;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BroadphaseBodyActivityView;
using SkullbonezCore::Physics::BroadphaseCandidateAppendHasCapacity;
using SkullbonezCore::Physics::BroadphasePairFilter;
using SkullbonezCore::Physics::BroadphaseSweepContactEnvelope;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsForceStage;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
void AddCandidateBody( PhysicsBodyStore& bodyStore, ColliderStore& colliderStore, const Vector3& position,
                       const Vector3& velocity, float radius )
{
    PhysicsBodyCreateRecord body;
    body.hot.position = position;
    body.hot.linearVelocity = velocity;
    (void)bodyStore.CreateBodyRecord( body );

    ColliderRecord collider;
    collider.boundingRadius = radius;
    const CollisionShape shape( BoundingSphere( radius, Vector3( 0.0f, 0.0f, 0.0f ), 0.0f ) );
    (void)SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore, collider, shape );
}

TEST_CASE( "Broadphase candidate append capacity rejects equality before vector growth" )
{
    CHECK( BroadphaseCandidateAppendHasCapacity( 0u, 1u ) );
    CHECK( BroadphaseCandidateAppendHasCapacity( 7u, 8u ) );
    CHECK_FALSE( BroadphaseCandidateAppendHasCapacity( 8u, 8u ) );
    CHECK_FALSE( BroadphaseCandidateAppendHasCapacity( 9u, 8u ) );
}

TEST_CASE( "Solver broadphase stage: checked step values reject misaligned domains and invalid scalars" )
{
    const std::array<uint8_t, 3> sleepState = {};
    const std::array<int, 2> awakeBodies = { 0, 2 };
    const std::array<int, 2> duplicateAwakeBodies = { 1, 1 };
    const std::array<uint8_t, 3> motionState = { 0u, 1u, 0u };
    const std::array<float, 3> angularExpansion = { 0.0f, 0.25f, 0.0f };

    CHECK( BroadphaseBodyActivityView::IsValid( 3, sleepState, awakeBodies, motionState, angularExpansion ) );
    CHECK_FALSE( BroadphaseBodyActivityView::IsValid( 2, sleepState, awakeBodies, motionState, angularExpansion ) );
    CHECK_FALSE( BroadphaseBodyActivityView::IsValid( 3, sleepState, duplicateAwakeBodies, motionState, angularExpansion ) );

    const BroadphaseBodyActivityView activity( 3, sleepState, awakeBodies, motionState, angularExpansion );
    CHECK_FALSE( activity.IsSleeping( 0 ) );
    CHECK_FALSE( activity.IsLinearPromoted( 0 ) );
    CHECK( activity.IsLinearPromoted( 1 ) );
    CHECK( activity.AngularExpansion( 1 ) == doctest::Approx( 0.25f ) );

    CHECK( BroadphaseSweepContactEnvelope::IsValid( 1.0f / 120.0f, 0.05f, 0.01f ) );
    CHECK_FALSE( BroadphaseSweepContactEnvelope::IsValid( -0.01f, 0.05f, 0.01f ) );
    CHECK_FALSE(
        BroadphaseSweepContactEnvelope::IsValid( 1.0f / 120.0f, ( std::numeric_limits<float>::infinity )(), 0.01f ) );
    CHECK_FALSE( BroadphaseSweepContactEnvelope::IsValid( 1.0f / 120.0f, 0.05f, -0.01f ) );
}

PhysicsBodyStore& TestBodyStore()
{
    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS slots. Static storage matches
    // runtime ownership and avoids consuming the doctest thread stack.
    static PhysicsBodyStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        store.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }
    store.Clear();
    return store;
}

ColliderStore& TestColliderStore()
{
    // Why: Collider records mirror runtime fixed storage, so the focused unit
    // fixture clears one static list between cases instead of stack-allocating it.
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
} // namespace


TEST_CASE( "Solver broadphase stage: candidate filter handles static and swept pairs" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderStore& colliderStore = TestColliderStore();
    AddCandidateBody( bodyStore, colliderStore, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderStore, Vector3( 2.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderStore, Vector3( 8.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderStore, Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    const std::array<uint8_t, 4> sleepState = {};
    const BroadphaseBodyActivityView activity( 4, sleepState, {}, {}, {} );
    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, activity,
                                           BroadphaseSweepContactEnvelope( 1.0f, 0.0f, 0.0f ) );

    CHECK( pairFilter.CanTouch( 0, 1 ) );
    CHECK_FALSE( pairFilter.CanTouch( 0, 2 ) );

    bodyStore.MutableHotFields().linearVelocityX[0] = 10.0f;
    CHECK( pairFilter.CanTouch( 0, 3 ) );
}


TEST_CASE( "Solver broadphase stage: candidate filter keeps boundary policy conservative" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderStore& colliderStore = TestColliderStore();
    AddCandidateBody( bodyStore, colliderStore, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderStore, Vector3( 100.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), -1.0f );
    const std::array<uint8_t, 2> sleepState = {};
    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ),
                                           BroadphaseSweepContactEnvelope( 1.0f, 0.0f, 0.0f ) );

    CHECK_FALSE( pairFilter.CanTouch( -1, 1 ) );
    CHECK_FALSE( pairFilter.CanTouch( 0, 2 ) );
    CHECK( pairFilter.CanTouch( 0, 1 ) );
}


TEST_CASE( "Solver broadphase stage: contact skin includes the exact static boundary" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderStore& colliderStore = TestColliderStore();
    // Hazard: Debug poisons default-constructed Vector3 values, so zero
    // velocity is explicit in every geometry-boundary fixture.
    AddCandidateBody( bodyStore, colliderStore, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderStore, Vector3( 2.1f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    const std::array<uint8_t, 2> sleepState = {};
    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ),
                                           BroadphaseSweepContactEnvelope( 1.0f, 0.1f, 0.0f ) );

    CHECK( pairFilter.CanTouch( 0, 1 ) );
    bodyStore.MutableHotFields().positionX[1] = 2.1002f;
    CHECK_FALSE( pairFilter.CanTouch( 0, 1 ) );
}


TEST_CASE( "Solver broadphase stage: two sleepers never enter candidate work" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderStore& colliderStore = TestColliderStore();
    AddCandidateBody( bodyStore, colliderStore, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderStore, Vector3( 1.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    std::array<uint8_t, 2> sleepState = { 1u, 1u };
    const BroadphaseSweepContactEnvelope envelope( 1.0f / 120.0f, 0.0f, 0.0f );

    CHECK_FALSE(
        BroadphasePairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ), envelope )
            .CanTouch( 0, 1 ) );
    sleepState[0] = 0u;
    CHECK(
        BroadphasePairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ), envelope )
            .CanTouch( 0, 1 ) );
}


TEST_CASE( "Physics force stage: mutual gravity respects fixed sleeping and massless receive flags" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    for ( int bodyIndex = 0; bodyIndex < 4; ++bodyIndex )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = bodyIndex == 3 ? 0.0f : 2.0f;
        body.hot.inverseMass = bodyIndex == 3 ? 0.0f : 0.5f;
        body.hot.position = Vector3( static_cast<float>( bodyIndex * 2 ), 0.0f, 0.0f );
        body.hot.fixed = bodyIndex == 1;
        (void)bodyStore.CreateBodyRecord( body );
    }
    const std::array<uint8_t, 4> sleepState = { 0u, 0u, 1u, 0u };
    PhysicsWorldForces worldForces;
    worldForces.mutualGravity.enabled = true;
    worldForces.mutualGravity.gravitationalConstant = 1.0f;
    worldForces.mutualGravity.softeningLength = 0.1f;
    SkullbonezCore::Physics::PhysicsExecutionSettings execution;
    execution.parallel = false;
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    PhysicsForceStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyScratchCapacity( 4u );
    }

    const Vector3* forces = stage.PrepareMutualGravityForces( bodyStore.Records(), bodyStore.HotFields(), sleepState, 4,
                                                              worldForces, execution, inlinePool );

    REQUIRE( forces != nullptr );
    CHECK( forces[0].x > 0.0f );
    CHECK( forces[0].y == doctest::Approx( 0.0f ) );
    CHECK( forces[0].z == doctest::Approx( 0.0f ) );
    CHECK( forces[1] == SkullbonezCore::Math::Vector::ZERO_VECTOR );
    CHECK( forces[2] == SkullbonezCore::Math::Vector::ZERO_VECTOR );
    CHECK( forces[3] == SkullbonezCore::Math::Vector::ZERO_VECTOR );
}


TEST_CASE( "Property invariant: mutual gravity obeys Newton-pair antisymmetry [seed 0x16B0D1E5]" )
{
    SkullbonezTests::FixedSeed random( 0x16B0D1E5u );
    PhysicsBodyStore& bodyStore = TestBodyStore();
    PhysicsForceStage stage;
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyScratchCapacity( 2u );
    }
    SkullbonezCore::Physics::PhysicsExecutionSettings execution;
    execution.parallel = false;
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    const std::array<uint8_t, 2> sleepState = { 0u, 0u };

    // Invariant: the pair table computes one force and applies its exact
    // negation to the other body, independent of mass, distance, or softening.
    for ( int sample = 0; sample < 64; ++sample )
    {
        bodyStore.Clear();
        PhysicsBodyCreateRecord left;
        left.cold.mass = random.Float( 0.25f, 20.0f );
        left.hot.inverseMass = 1.0f / left.cold.mass;
        left.hot.position = Vector3( random.Float( -20.0f, 20.0f ), random.Float( -20.0f, 20.0f ),
                                     random.Float( -20.0f, 20.0f ) );
        PhysicsBodyCreateRecord right;
        right.cold.mass = random.Float( 0.25f, 20.0f );
        right.hot.inverseMass = 1.0f / right.cold.mass;
        right.hot.position = left.hot.position + Vector3( random.Float( 0.5f, 12.0f ), random.Float( 0.5f, 12.0f ),
                                                          random.Float( 0.5f, 12.0f ) );
        (void)bodyStore.CreateBodyRecord( left );
        (void)bodyStore.CreateBodyRecord( right );
        PhysicsWorldForces worldForces;
        worldForces.mutualGravity.enabled = true;
        worldForces.mutualGravity.gravitationalConstant = random.Float( 0.01f, 50.0f );
        worldForces.mutualGravity.softeningLength = random.Float( 0.0f, 2.0f );

        const Vector3* forces = stage.PrepareMutualGravityForces( bodyStore.Records(), bodyStore.HotFields(), sleepState, 2,
                                                                  worldForces, execution, inlinePool );

        REQUIRE( forces != nullptr );
        CHECK( forces[0].x == -forces[1].x );
        CHECK( forces[0].y == -forces[1].y );
        CHECK( forces[0].z == -forces[1].z );
    }
}
