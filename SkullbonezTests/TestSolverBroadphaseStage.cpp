//
// File: SkullbonezTests/TestSolverBroadphaseStage.cpp
// Purpose:
//   Lock direct coverage for the solver broadphase filter and the AVX2
//   integration, force, mutual-gravity, and broadphase SIMD mask boundaries.
//
// Summary:
//   SpatialGrid emits possible pairs from cell overlap. The solver broadphase
//   filter is a cheaper geometric pass that keeps only pairs whose swept
//   bounding spheres can touch this tick. The integration fixture separately
//   proves inactive and masked-tail lanes never mutate SoA body storage. Force
//   and bounds fixtures lock the S5 per-lane contracts before stage dispatch.
//
// Glossary:
//   Swept pair: Two bodies whose relative motion may close the gap before the
//     fixed tick ends.
//   Static separated pair: Bodies whose current spheres do not overlap and have
//     no relative motion that can close the distance.
//   Conservative accept: A pair kept intentionally because source data is not
//     trustworthy enough for safe rejection.
//   Masked tail: Final partial SIMD block whose absent body lanes cannot access
//     storage while valid lanes retain the eight-wide arithmetic shape.
//
// Invariants:
//   - Out-of-range model indices are rejected before array access.
//   - Invalid broadphase radii are accepted, not rejected, so narrowphase keeps
//     authority over malformed or transitional shape data.
//   - Integration-kernel activity masks exclude fixed, sleeping, and zero-time
//     rows and preserve masked-tail bounds.
//   - Force, broadphase, narrowphase-prune, and solver-row kernels report
//     valid/active lanes without horizontal reductions or tail over-read.
//
// Related:
//   - SkullbonezSource/Physics/SolverBroadphaseStage.h
//   - SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.h
//   - SkullbonezSource/Physics/Stages/Kernels/ForceKernel.h
//   - SkullbonezSource/Physics/Stages/Kernels/BroadphaseKernel.h
//   - SkullbonezSource/Physics/Stages/Kernels/NarrowphasePruneKernel.h
//   - SkullbonezSource/Physics/Stages/Kernels/SolverRowKernel.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Physics/SolverBroadphaseStage.h"
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsForceStage.h"
#include "../SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.h"
#include "../SkullbonezSource/Physics/Stages/Kernels/ForceKernel.h"
#include "../SkullbonezSource/Physics/Stages/Kernels/BroadphaseKernel.h"
#include "../SkullbonezSource/Physics/Stages/Kernels/NarrowphasePruneKernel.h"
#include "../SkullbonezSource/Physics/Stages/Kernels/SolverRowKernel.h"

#include <array>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BroadphaseCandidateCanTouch;
using SkullbonezCore::Physics::BroadphaseCandidateFilterContext;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsForceStage;
using SkullbonezCore::Physics::PhysicsWorldForces;
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

PhysicsBodyStore& TestBodyStore()
{
    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS slots. Static storage matches
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


TEST_CASE( "Integration AVX2 pilot: activity and masked-tail lanes stay bounded" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    for ( int index = 0; index < 10; ++index )
    {
        PhysicsBodyCreateRecord body;
        body.hot.position = Vector3( static_cast<float>( index ) + 0.1f, 2.0f, -3.0f );
        body.hot.linearVelocity = Vector3( index == 1 ? 0.00001f : 2.0f, -4.0f, 0.5f );
        body.hot.angularVelocity = Vector3( 0.00001f, 1.0f, -1.0f );
        (void)bodyStore.CreateBodyRecord( body );
    }

    auto hotFields = bodyStore.MutableHotFields();
    std::array<uint8_t, 10> sleepState = {};
    std::array<float, 10> timeRemaining = {};
    timeRemaining.fill( 0.25f );
    sleepState[2] = 1u;
    hotFields.fixed[3] = 1u;
    hotFields.awake[4] = 0u;
    timeRemaining[5] = 0.0f;

    const uint32_t firstMask =
        SkullbonezCore::Physics::Kernels::IntegratePositionAvx2( hotFields, sleepState, timeRemaining, 0, 10 );
    CHECK( firstMask == 0xc3u );
    CHECK( hotFields.positionX[0] == std::fma( 2.0f, 0.25f, 0.1f ) );
    CHECK( hotFields.linearVelocityX[1] == 0.0f );
    CHECK( hotFields.positionX[1] == doctest::Approx( 1.1f ) );
    CHECK( hotFields.positionX[2] == doctest::Approx( 2.1f ) );
    CHECK( hotFields.angularVelocityX[0] == 0.0f );

    const uint32_t tailMask =
        SkullbonezCore::Physics::Kernels::IntegratePositionAvx2( hotFields, sleepState, timeRemaining, 8, 10 );
    CHECK( tailMask == 0x03u );
    CHECK( hotFields.positionX[8] == std::fma( 2.0f, 0.25f, 8.1f ) );
    CHECK( hotFields.positionX[9] == std::fma( 2.0f, 0.25f, 9.1f ) );
}


TEST_CASE( "Force AVX2 kernels: gravity masks and mutual-pair tails stay independent" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    for ( int index = 0; index < 10; ++index )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = static_cast<float>( index + 2 );
        body.hot.position = Vector3( static_cast<float>( index ) * 2.0f, 0.0f, 0.0f );
        body.hot.linearVelocity = Vector3( 0.0f, 5.0f, 0.0f );
        body.hot.inverseMass = 1.0f / body.cold.mass;
        (void)bodyStore.CreateBodyRecord( body );
    }

    auto hotFields = bodyStore.MutableHotFields();
    std::array<uint8_t, 10> sleepState = {};
    sleepState[2] = 1u;
    hotFields.fixed[3] = 1u;
    hotFields.inverseMass[4] = 0.0f;
    const uint32_t gravityMask =
        SkullbonezCore::Physics::Kernels::ApplyGravityAvx2( hotFields, sleepState, 0, 10, -9.8f, 0.25f );
    CHECK( gravityMask == 0xf3u );
    CHECK( hotFields.linearVelocityY[0] == std::fma( -9.8f, 0.25f, 5.0f ) );
    CHECK( hotFields.linearVelocityY[2] == 5.0f );
    CHECK( hotFields.linearVelocityY[3] == 5.0f );
    CHECK( hotFields.linearVelocityY[4] == 5.0f );
    CHECK( SkullbonezCore::Physics::Kernels::ApplyGravityAvx2( hotFields, sleepState, 8, 10, -9.8f, 0.25f ) == 0x03u );

    std::array<Vector3, 8> pairForces = {};
    const uint32_t pairMask = SkullbonezCore::Physics::Kernels::BuildMutualGravityPairsAvx2( bodyStore.Records(),
                                                                                             bodyStore.HotFields(),
                                                                                             sleepState,
                                                                                             0,
                                                                                             1,
                                                                                             3,
                                                                                             1.0f,
                                                                                             1.0f,
                                                                                             pairForces.data() );
    CHECK( pairMask == 0x03u );
    const float expectedX = 2.0f * 3.0f * 2.0f / std::pow( 5.0f, 1.5f );
    CHECK( pairForces[0].x == doctest::Approx( expectedX ) );
    CHECK( pairForces[0].y == 0.0f );
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
    execution.simdKernels = false;
    WorkerPool inlinePool;
    PhysicsForceStage stage;
    stage.ReserveBodyScratchCapacity( 4u );

    const Vector3* forces = stage.PrepareMutualGravityForces(
        bodyStore.Records(), bodyStore.HotFields(), sleepState, 4, worldForces, execution, inlinePool );

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
    execution.simdKernels = false;
    WorkerPool inlinePool;
    const std::array<uint8_t, 2> sleepState = { 0u, 0u };

    // Invariant: the pair kernel computes one force and applies its exact
    // negation to the other body, independent of mass, distance, or softening.
    for ( int sample = 0; sample < 64; ++sample )
    {
        bodyStore.Clear();
        PhysicsBodyCreateRecord left;
        left.cold.mass = random.Float( 0.25f, 20.0f );
        left.hot.inverseMass = 1.0f / left.cold.mass;
        left.hot.position = Vector3( random.Float( -20.0f, 20.0f ),
                                     random.Float( -20.0f, 20.0f ),
                                     random.Float( -20.0f, 20.0f ) );
        PhysicsBodyCreateRecord right;
        right.cold.mass = random.Float( 0.25f, 20.0f );
        right.hot.inverseMass = 1.0f / right.cold.mass;
        right.hot.position = left.hot.position + Vector3( random.Float( 0.5f, 12.0f ),
                                                           random.Float( 0.5f, 12.0f ),
                                                           random.Float( 0.5f, 12.0f ) );
        (void)bodyStore.CreateBodyRecord( left );
        (void)bodyStore.CreateBodyRecord( right );
        PhysicsWorldForces worldForces;
        worldForces.mutualGravity.enabled = true;
        worldForces.mutualGravity.gravitationalConstant = random.Float( 0.01f, 50.0f );
        worldForces.mutualGravity.softeningLength = random.Float( 0.0f, 2.0f );

        const Vector3* forces = stage.PrepareMutualGravityForces(
            bodyStore.Records(), bodyStore.HotFields(), sleepState, 2, worldForces, execution, inlinePool );

        REQUIRE( forces != nullptr );
        CHECK( forces[0].x == -forces[1].x );
        CHECK( forces[0].y == -forces[1].y );
        CHECK( forces[0].z == -forces[1].z );
    }
}


TEST_CASE( "Broadphase AVX2 kernel: swept and static AABB lanes keep masked tails" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    for ( int index = 0; index < 10; ++index )
    {
        AddCandidateBody( bodyStore,
                          colliderRecords,
                          Vector3( static_cast<float>( index ), 2.0f, -3.0f ),
                          Vector3( index == 0 || index == 2 ? 10.0f : 0.0f, 0.0f, 0.0f ),
                          1.0f );
    }
    bodyStore.MutableHotFields().fixed[2] = 1u;

    SkullbonezCore::Physics::Kernels::BroadphaseBoundsBlock bounds;
    SkullbonezCore::Physics::Kernels::BuildBroadphaseBoundsAvx2( bodyStore.HotFields(),
                                                                 { colliderRecords.data(), colliderRecords.size() },
                                                                 0,
                                                                 10,
                                                                 0.5f,
                                                                 0.0f,
                                                                 bounds );
    CHECK( bounds.validBits == 0xffu );
    CHECK( bounds.sweptBits == 0x01u );
    CHECK( bounds.minX[0] == -1.0f );
    CHECK( bounds.maxX[0] == 6.0f );
    CHECK( bounds.minX[1] == 0.0f );
    CHECK( bounds.maxX[1] == 2.0f );

    SkullbonezCore::Physics::Kernels::BuildBroadphaseBoundsAvx2( bodyStore.HotFields(),
                                                                 { colliderRecords.data(), colliderRecords.size() },
                                                                 8,
                                                                 10,
                                                                 0.5f,
                                                                 0.0f,
                                                                 bounds );
    CHECK( bounds.validBits == 0x03u );
}


TEST_CASE( "Narrowphase prune AVX2 kernel: every partial block matches the scalar oracle" )
{
    PhysicsBodyStore& bodyStore = TestBodyStore();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    for ( int index = 0; index < 9; ++index )
    {
        AddCandidateBody( bodyStore,
                          colliderRecords,
                          Vector3( static_cast<float>( index ) * 3.0f, 0.0f, 0.0f ),
                          Vector3( index == 0 ? 8.0f : 0.0f, 0.0f, 0.0f ),
                          1.0f );
    }
    std::array<std::pair<int, int>, 8> pairs = {};
    for ( int lane = 0; lane < 8; ++lane )
    {
        pairs[static_cast<size_t>( lane )] = { 0, lane + 1 };
    }
    BroadphaseCandidateFilterContext scalarContext{ bodyStore.Records(),
                                                     bodyStore.HotFields(),
                                                     { colliderRecords.data(), colliderRecords.size() },
                                                     9,
                                                     0.5f,
                                                     0.0f };
    for ( int laneCount = 1; laneCount <= 8; ++laneCount )
    {
        uint32_t expected = 0u;
        for ( int lane = 0; lane < laneCount; ++lane )
        {
            if ( BroadphaseCandidateCanTouch( &scalarContext, pairs[static_cast<size_t>( lane )].first,
                                              pairs[static_cast<size_t>( lane )].second ) )
            {
                expected |= 1u << lane;
            }
        }
        const uint32_t actual = SkullbonezCore::Physics::Kernels::PruneNarrowphasePairsAvx2(
            bodyStore.HotFields(),
            { colliderRecords.data(), colliderRecords.size() },
            { pairs.data(), static_cast<size_t>( laneCount ) },
            9,
            0.5f,
            0.0f );
        CHECK( actual == expected );
    }
}


TEST_CASE( "Solver row AVX2 kernel: masses anchor speed and bias preserve a two-row tail" )
{
    std::array<SkullbonezCore::Physics::SolverBodyState, 11> bodies = {};
    for ( auto& body : bodies )
    {
        body.invMass = 1.0f;
        body.invInertia = Vector3( 1.0f, 1.0f, 1.0f );
    }
    bodies[0].linearVelocity = Vector3( 1.0f, 0.0f, 0.0f );
    std::array<SkullbonezCore::Physics::PersistentContact, 10> contacts = {};
    for ( int row = 0; row < 10; ++row )
    {
        auto& contact = contacts[static_cast<size_t>( row )];
        contact.bodyA = 0;
        contact.bodyB = row + 1;
        contact.normal = Vector3( 1.0f, 0.0f, 0.0f );
        contact.penetration = 0.2f;
    }

    SkullbonezCore::Physics::Kernels::SolverRowPrepBlock block;
    SkullbonezCore::Physics::Kernels::PrepareSolverRowsAvx2(
        contacts, bodies, 0, 120.0f, 0.01f, 0.2f, 10.0f, block );
    CHECK( block.validBits == 0xffu );
    CHECK( contacts[0].normalMass == doctest::Approx( 0.5f ).epsilon( 1.0e-6 ) );
    CHECK( contacts[0].tangentMass1 == doctest::Approx( 0.5f ).epsilon( 1.0e-6 ) );
    CHECK( block.normalSpeed[0] == doctest::Approx( -1.0f ).epsilon( 1.0e-6 ) );
    CHECK( block.penetrationBias[0] == doctest::Approx( 4.56f ).epsilon( 1.0e-5 ) );

    SkullbonezCore::Physics::Kernels::PrepareSolverRowsAvx2(
        contacts, bodies, 8, 120.0f, 0.01f, 0.2f, 10.0f, block );
    CHECK( block.validBits == 0x03u );
    CHECK( contacts[9].normalMass == doctest::Approx( 0.5f ).epsilon( 1.0e-6 ) );
}
