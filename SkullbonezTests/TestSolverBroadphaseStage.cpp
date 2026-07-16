//
// File: SkullbonezTests/TestSolverBroadphaseStage.cpp
// Purpose:
//   Lock direct coverage for the solver broadphase filter and the AVX2
//   integration pilot's activity-mask boundary.
//
// Summary:
//   SpatialGrid emits possible pairs from cell overlap. The solver broadphase
//   filter is a cheaper geometric pass that keeps only pairs whose swept
//   bounding spheres can touch this tick. The integration fixture separately
//   proves inactive and masked-tail lanes never mutate SoA body storage.
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
//
// Related:
//   - SkullbonezSource/Physics/SolverBroadphaseStage.h
//   - SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.h
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/SolverBroadphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.h"

#include <array>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BroadphaseCandidateCanTouch;
using SkullbonezCore::Physics::BroadphaseCandidateFilterContext;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;

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

    const uint32_t firstMask = SkullbonezCore::Physics::Kernels::IntegratePositionAvx2(
        hotFields,
        sleepState,
        timeRemaining,
        0,
        10 );
    CHECK( firstMask == 0xc3u );
    CHECK( hotFields.positionX[0] == std::fma( 2.0f, 0.25f, 0.1f ) );
    CHECK( hotFields.linearVelocityX[1] == 0.0f );
    CHECK( hotFields.positionX[1] == doctest::Approx( 1.1f ) );
    CHECK( hotFields.positionX[2] == doctest::Approx( 2.1f ) );
    CHECK( hotFields.angularVelocityX[0] == 0.0f );

    const uint32_t tailMask = SkullbonezCore::Physics::Kernels::IntegratePositionAvx2(
        hotFields,
        sleepState,
        timeRemaining,
        8,
        10 );
    CHECK( tailMask == 0x03u );
    CHECK( hotFields.positionX[8] == std::fma( 2.0f, 0.25f, 8.1f ) );
    CHECK( hotFields.positionX[9] == std::fma( 2.0f, 0.25f, 9.1f ) );
}
