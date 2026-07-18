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
//   - Invalid broadphase radii are accepted, not rejected, so narrowphase keeps
//     authority over malformed or transitional shape data.
//
// Related:
//   - SkullbonezSource/Physics/SolverBroadphaseStage.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Physics/SolverBroadphaseStage.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsForceStage.h"

#include <array>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BroadphaseCandidateAppendHasCapacity;
using SkullbonezCore::Physics::BroadphaseCandidateCanTouch;
using SkullbonezCore::Physics::BroadphaseCandidateFilterContext;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsForceStage;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
void AddCandidateBody( PhysicsBodyStore& bodyStore,
                       ColliderRecordList& colliderRecords,
                       const Vector3& position,
                       const Vector3& velocity,
                       float radius )
{
    PhysicsBodyCreateRecord body;
    body.hot.position = position;
    body.hot.linearVelocity = velocity;
    (void)bodyStore.CreateBodyRecord( body );

    ColliderRecord collider;
    collider.boundingRadius = radius;
    colliderRecords.push_back( collider );
}

TEST_CASE( "Broadphase candidate append capacity rejects equality before vector growth" )
{
    CHECK( BroadphaseCandidateAppendHasCapacity( 0u, 1u ) );
    CHECK( BroadphaseCandidateAppendHasCapacity( 7u, 8u ) );
    CHECK_FALSE( BroadphaseCandidateAppendHasCapacity( 8u, 8u ) );
    CHECK_FALSE( BroadphaseCandidateAppendHasCapacity( 9u, 8u ) );
}

PhysicsBodyStore& TestBodyStore()
{
    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS slots. Static storage matches
    // runtime ownership and avoids consuming the doctest thread stack.
    static PhysicsBodyStore store;
    store.Clear();
    return store;
}

ColliderRecordList& TestColliderRecords()
{
    // Why: Collider records mirror runtime fixed storage, so the focused unit
    // fixture clears one static list between cases instead of stack-allocating it.
    static ColliderRecordList records( "TestSolverBroadphaseStage.colliderRecords" );
    records.clear();
    return records;
}
} // namespace


TEST_CASE( "Solver broadphase stage: candidate filter handles static and swept pairs" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 2.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 8.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 10.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );

    BroadphaseCandidateFilterContext context{ bodyStore.Records(),
                                              bodyStore.HotFields(),
                                              { colliderRecords.data(), colliderRecords.size() },
                                              4,
                                              1.0f,
                                              0.0f };

    CHECK( BroadphaseCandidateCanTouch( &context, 0, 1 ) );
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, 0, 2 ) );

    bodyStore.MutableHotFields().linearVelocityX[0] = 10.0f;
    CHECK( BroadphaseCandidateCanTouch( &context, 0, 3 ) );
}


TEST_CASE( "Solver broadphase stage: candidate filter keeps boundary policy conservative" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 1.0f );
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 100.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), -1.0f );

    BroadphaseCandidateFilterContext context{ bodyStore.Records(),
                                              bodyStore.HotFields(),
                                              { colliderRecords.data(), colliderRecords.size() },
                                              2,
                                              1.0f,
                                              0.0f };

    CHECK( BroadphaseCandidateCanTouch( nullptr, 0, 1 ) );
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, -1, 1 ) );
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, 0, 2 ) );
    CHECK( BroadphaseCandidateCanTouch( &context, 0, 1 ) );
}


TEST_CASE( "Solver broadphase stage: contact skin includes the exact static boundary" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 0.0f, 0.0f, 0.0f ), Vector3(), 1.0f );
    AddCandidateBody( bodyStore, colliderRecords, Vector3( 2.1f, 0.0f, 0.0f ), Vector3(), 1.0f );
    BroadphaseCandidateFilterContext context{ bodyStore.Records(),
                                              bodyStore.HotFields(),
                                              { colliderRecords.data(), colliderRecords.size() },
                                              2,
                                              1.0f,
                                              0.1f };

    CHECK( BroadphaseCandidateCanTouch( &context, 0, 1 ) );
    bodyStore.MutableHotFields().positionX[1] = 2.1002f;
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, 0, 1 ) );
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
    SkullbonezCore::Core::PhysicsExecutionConfig execution;
    execution.parallel = false;
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    PhysicsForceStage stage;
    stage.ReserveBodyScratchCapacity( 4u );

    const Vector3* forces = stage.PrepareMutualGravityForces( bodyStore.Records(),
                                                              bodyStore.HotFields(),
                                                              sleepState,
                                                              4,
                                                              worldForces,
                                                              execution,
                                                              inlinePool );

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
    stage.ReserveBodyScratchCapacity( 2u );
    SkullbonezCore::Core::PhysicsExecutionConfig execution;
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
        left.hot.position =
            Vector3( random.Float( -20.0f, 20.0f ), random.Float( -20.0f, 20.0f ), random.Float( -20.0f, 20.0f ) );
        PhysicsBodyCreateRecord right;
        right.cold.mass = random.Float( 0.25f, 20.0f );
        right.hot.inverseMass = 1.0f / right.cold.mass;
        right.hot.position =
            left.hot.position +
            Vector3( random.Float( 0.5f, 12.0f ), random.Float( 0.5f, 12.0f ), random.Float( 0.5f, 12.0f ) );
        (void)bodyStore.CreateBodyRecord( left );
        (void)bodyStore.CreateBodyRecord( right );
        PhysicsWorldForces worldForces;
        worldForces.mutualGravity.enabled = true;
        worldForces.mutualGravity.gravitationalConstant = random.Float( 0.01f, 50.0f );
        worldForces.mutualGravity.softeningLength = random.Float( 0.0f, 2.0f );

        const Vector3* forces = stage.PrepareMutualGravityForces( bodyStore.Records(),
                                                                  bodyStore.HotFields(),
                                                                  sleepState,
                                                                  2,
                                                                  worldForces,
                                                                  execution,
                                                                  inlinePool );

        REQUIRE( forces != nullptr );
        CHECK( forces[0].x == -forces[1].x );
        CHECK( forces[0].y == -forces[1].y );
        CHECK( forces[0].z == -forces[1].z );
    }
}
