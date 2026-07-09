//
// File: SkullbonezTests/TestSolverBroadphaseStage.cpp
// Purpose:
//   Lock direct coverage for the pure solver broadphase candidate filter.
//
// Mental model:
//   SpatialGrid emits possible pairs from cell overlap. The solver broadphase
//   filter is a cheaper geometric pass that keeps only pairs whose swept
//   bounding spheres can touch this tick.
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
//   - Agentic/Plans/02-physicsworld-solver-decomposition.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/SolverBroadphaseStage.h"

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::BroadphaseCandidateCanTouch;
using SkullbonezCore::Physics::BroadphaseCandidateFilterContext;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderRecordList;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyRecordList;

namespace
{
void AddCandidateBody( PhysicsBodyRecordList& bodyRecords,
                       ColliderRecordList& colliderRecords,
                       const Vector3& position,
                       const Vector3& velocity,
                       float radius )
{
    PhysicsBodyRecord body;
    body.position = position;
    body.linearVelocity = velocity;
    bodyRecords.push_back( body );

    ColliderRecord collider;
    collider.boundingRadius = radius;
    colliderRecords.push_back( collider );
}

PhysicsBodyRecordList& TestBodyRecords()
{
    // Why: physics fixed lists own MAX_GAME_MODELS slots. Static storage matches
    // runtime ownership and avoids consuming the doctest thread stack.
    static PhysicsBodyRecordList records( "TestSolverBroadphaseStage.bodyRecords" );
    records.clear();
    return records;
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
    PhysicsBodyRecordList& bodyRecords = TestBodyRecords();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    AddCandidateBody( bodyRecords,
                      colliderRecords,
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      1.0f );
    AddCandidateBody( bodyRecords,
                      colliderRecords,
                      Vector3( 2.0f, 0.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      1.0f );
    AddCandidateBody( bodyRecords,
                      colliderRecords,
                      Vector3( 8.0f, 0.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      1.0f );
    AddCandidateBody( bodyRecords,
                      colliderRecords,
                      Vector3( 10.0f, 0.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      1.0f );

    BroadphaseCandidateFilterContext context{ bodyRecords, colliderRecords, 4, 1.0f, 0.0f };

    CHECK( BroadphaseCandidateCanTouch( &context, 0, 1 ) );
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, 0, 2 ) );

    bodyRecords[0].linearVelocity = Vector3( 10.0f, 0.0f, 0.0f );
    CHECK( BroadphaseCandidateCanTouch( &context, 0, 3 ) );
}


TEST_CASE( "Solver broadphase stage: candidate filter keeps boundary policy conservative" )
{
    PhysicsBodyRecordList& bodyRecords = TestBodyRecords();
    ColliderRecordList& colliderRecords = TestColliderRecords();
    AddCandidateBody( bodyRecords,
                      colliderRecords,
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      1.0f );
    AddCandidateBody( bodyRecords,
                      colliderRecords,
                      Vector3( 100.0f, 0.0f, 0.0f ),
                      Vector3( 0.0f, 0.0f, 0.0f ),
                      -1.0f );

    BroadphaseCandidateFilterContext context{ bodyRecords, colliderRecords, 2, 1.0f, 0.0f };

    CHECK( BroadphaseCandidateCanTouch( nullptr, 0, 1 ) );
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, -1, 1 ) );
    CHECK_FALSE( BroadphaseCandidateCanTouch( &context, 0, 2 ) );
    CHECK( BroadphaseCandidateCanTouch( &context, 0, 1 ) );
}
