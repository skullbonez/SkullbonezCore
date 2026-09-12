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
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsForceStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"

#include <array>
#include <cmath>
#include <limits>
#include <bit>
#include <vector>
#include <memory>

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
void AddCandidateBody( PhysicsBodyStore& bodyStore, ColliderStore& colliderStore, const Vector3& position, const Vector3& velocity, float radius )
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
    CHECK_FALSE( BroadphaseSweepContactEnvelope::IsValid( 1.0f / 120.0f, ( std::numeric_limits<float>::infinity )(), 0.01f ) );
    CHECK_FALSE( BroadphaseSweepContactEnvelope::IsValid( 1.0f / 120.0f, 0.05f, -0.01f ) );
}

PhysicsBodyStore& TestBodyStore()
{
    // Why: physics fixed lists own SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS slots. Static storage matches
    // runtime ownership and avoids consuming the doctest thread stack.
    static PhysicsBodyStore store;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
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
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
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
    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, activity, BroadphaseSweepContactEnvelope( 1.0f, 0.0f, 0.0f ) );

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
    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ), BroadphaseSweepContactEnvelope( 1.0f, 0.0f, 0.0f ) );

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
    const BroadphasePairFilter pairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ), BroadphaseSweepContactEnvelope( 1.0f, 0.1f, 0.0f ) );

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

    CHECK_FALSE( BroadphasePairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ), envelope ).CanTouch( 0, 1 ) );
    sleepState[0] = 0u;
    CHECK( BroadphasePairFilter( bodyStore, colliderStore, BroadphaseBodyActivityView( 2, sleepState, {}, {}, {} ), envelope ).CanTouch( 0, 1 ) );
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
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage.ReserveBodyScratchCapacity( 4u );
    }

    const Vector3* forces = stage.PrepareMutualGravityForces( bodyStore.Records(), bodyStore.HotFields(), sleepState, 4, worldForces, execution, inlinePool );

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
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
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
        left.hot.position = Vector3( random.Float( -20.0f, 20.0f ), random.Float( -20.0f, 20.0f ), random.Float( -20.0f, 20.0f ) );
        PhysicsBodyCreateRecord right;
        right.cold.mass = random.Float( 0.25f, 20.0f );
        right.hot.inverseMass = 1.0f / right.cold.mass;
        right.hot.position = left.hot.position + Vector3( random.Float( 0.5f, 12.0f ), random.Float( 0.5f, 12.0f ), random.Float( 0.5f, 12.0f ) );
        (void)bodyStore.CreateBodyRecord( left );
        (void)bodyStore.CreateBodyRecord( right );
        PhysicsWorldForces worldForces;
        worldForces.mutualGravity.enabled = true;
        worldForces.mutualGravity.gravitationalConstant = random.Float( 0.01f, 50.0f );
        worldForces.mutualGravity.softeningLength = random.Float( 0.0f, 2.0f );

        const Vector3* forces = stage.PrepareMutualGravityForces( bodyStore.Records(), bodyStore.HotFields(), sleepState, 2, worldForces, execution, inlinePool );

        REQUIRE( forces != nullptr );
        CHECK( forces[0].x == -forces[1].x );
        CHECK( forces[0].y == -forces[1].y );
        CHECK( forces[0].z == -forces[1].z );
    }
}

namespace
{
// Test-only reference keeps the original all-target sweep and linear duplicate
// lookup. The production grid is independently populated from current shapes;
// this comparison detects an omitted conservative pair or changed pair order.
std::vector<std::pair<int, int>> ReferenceDiscreteGridAndSweep( const PhysicsBodyStore& bodies,
                                                                const ColliderStore& colliders,
                                                                BroadphaseBodyActivityView activity,
                                                                BroadphaseSweepContactEnvelope envelope,
                                                                std::span<const SkullbonezCore::Physics::PointJointConstraint> joints )
{
    namespace Collision = SkullbonezCore::Math::CollisionDetection;
    namespace Vector = SkullbonezCore::Math::Vector;
    namespace Physics = SkullbonezCore::Physics;
    const auto hot = bodies.HotFields();
    const auto records = colliders.Records();
    float largest = 0.0f;
    for ( const auto& collider : records )
    {
        largest = (std::max)( largest, collider.boundingRadius );
    }
    auto grid = std::make_unique<Collision::SpatialGrid>( (std::min)( 24.0f, (std::max)( 0.5f, ( largest + envelope.ContactSkin() ) * 2.0f ) ) );
    Physics::PhysicsCandidatePairList pairs( "TestBroadphase.referencePairs", Physics::PhysicsCapacityReason::ExplicitTestCapacity );
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope scope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        grid->ReserveSceneCapacity( bodies.Count() );
        pairs.Reserve( Physics::PhysicsCandidatePairCapacity( bodies.Count() ) );
    }
    grid->BeginFrame( bodies.Count() );
    const auto center = [&]( int index )
    { return Collision::GetWorldShapeCenter( records[index].shape, Physics::PhysicsBodyPosition( hot, index ), Physics::PhysicsBodyOrientation( hot, index ).GetOrientationMatrix() ); };
    for ( int index = 0; index < bodies.Count(); ++index )
    {
        grid->Insert( index, center( index ), Collision::GetShapeBoundingRadius( records[index].shape ) + envelope.ContactSkin() );
    }
    const BroadphasePairFilter filter( bodies, colliders, activity, envelope );
    for ( int body : activity.AwakeBodyIndices() )
    {
        grid->MarkPairSourceCells( body );
    }
#if defined( _DEBUG )
    grid->GetFilteredCandidatePairs( pairs, filter, false );
#else
    grid->GetFilteredCandidatePairs( pairs, filter, true );
#endif
    for ( int moving : activity.AwakeBodyIndices() )
    {
        const float radius = records[moving].boundingRadius;
        const auto travel = Physics::PhysicsBodyLinearVelocity( hot, moving ) * envelope.DeltaTime();
        const float minimum = (std::max)( radius * 2.0f, 1.0f );
        if ( hot.fixed[moving] || radius > 1.0f || Vector::VectorMagSquared( travel ) <= minimum * minimum )
        {
            continue;
        }
        for ( int target = 0; target < bodies.Count(); ++target )
        {
            if ( target == moving )
            {
                continue;
            }
            const auto start = center( moving ) - center( target );
            const auto displacement = ( Physics::PhysicsBodyLinearVelocity( hot, moving ) - Physics::PhysicsBodyLinearVelocity( hot, target ) ) * envelope.DeltaTime();
            const float lengthSq = Vector::VectorMagSquared( displacement );
            if ( lengthSq <= TOLERANCE * TOLERANCE )
            {
                continue;
            }
            float t = -Dot( start, displacement ) / lengthSq;
            t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
            const auto closest = start + displacement * t;
            const float expanded = Collision::GetShapeBoundingRadius( records[moving].shape ) + Collision::GetShapeBoundingRadius( records[target].shape ) + envelope.ContactEpsilon() + 1.0f;
            const std::pair<int, int> pair( (std::min)( moving, target ), (std::max)( moving, target ) );
            if ( Vector::VectorMagSquared( closest ) <= expanded * expanded && filter.CanTouch( pair.first, pair.second ) )
            {
                if ( std::find( pairs.begin(), pairs.end(), pair ) == pairs.end() )
                {
                    pairs.push_back( pair );
                }
            }
        }
    }
    std::vector<std::pair<int, int>> result;
    for ( const auto& pair : pairs )
    {
        if ( hot.fixed[pair.first] && hot.fixed[pair.second] )
        {
            continue;
        }
        bool excluded = false;
        for ( const auto& joint : joints )
        {
            int a = joint.BodyAIndex( bodies );
            int b = joint.BodyBIndex( bodies );
            if ( a > b )
            {
                std::swap( a, b );
            }
            excluded |= a == pair.first && b == pair.second;
        }
        if ( !excluded )
        {
            result.push_back( pair );
        }
    }
    std::sort( result.begin(), result.end() );
    return result;
}
} // namespace

static void CheckBroadphaseReference( int count, bool mixedShapes = false )
{
    namespace Physics = SkullbonezCore::Physics;
    namespace Collision = SkullbonezCore::Math::CollisionDetection;
    PhysicsBodyStore& bodies = TestBodyStore();
    ColliderStore& colliders = TestColliderStore();
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope scope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        colliders.ReserveShapeCapacity( count, mixedShapes ? count : 0u, mixedShapes ? count : 0u );
    }
    Collision::ConvexHullShape hull;
    if ( mixedShapes )
    {
        SkullbonezCore::Core::SbDiagnosticStore diagnostics;
        REQUIRE( Collision::ConvexHullShape::TryLoadFromFile( diagnostics, "SkullbonezData/hulls/pyramid.hull", hull ).Ok() );
        for ( int axis = 0; axis < 3; ++axis )
        {
            hull.ScaleAxis( axis, 0.25f );
        }
    }
    std::vector<int> awake;
    std::vector<uint8_t> sleep( count, 0u );
    SkullbonezTests::FixedSeed random( 0x21c873a9u );
    for ( int index = 0; index < count; ++index )
    {
        PhysicsBodyCreateRecord body;
        body.cold.mass = 1.0f;
        body.hot.inverseMass = 1.0f;
        body.hot.position = Vector3( random.Float( -30.0f, 30.0f ), random.Float( -6.0f, 6.0f ), random.Float( -6.0f, 6.0f ) );
        body.hot.linearVelocity = Vector3( random.Float( -120.0f, 120.0f ), random.Float( -15.0f, 15.0f ), random.Float( -15.0f, 15.0f ) );
        body.hot.fixed = index % 11 == 0;
        if ( mixedShapes )
        {
            body.hot.orientation = SkullbonezCore::Math::Orientation::Quaternion( 0.0f, 0.38268343f, 0.0f, 0.92387953f );
        }
        const auto handle = bodies.CreateBodyRecord( body );
        sleep[index] = index % 5 == 0;
        if ( !sleep[index] && !body.hot.fixed )
        {
            awake.push_back( index );
        }
        const float radius = index % 7 == 0 ? 1.5f : 0.25f;
        const Vector3 offset( index % 3 == 0 ? 0.125f : 0.0f, 0.0f, 0.0f );
        ColliderRecord collider;
        collider.body = handle;
        CollisionShape shape( BoundingSphere( radius, offset, 0.0f ) );
        if ( mixedShapes && index % 17 == 0 )
        {
            shape = CollisionShape( hull );
        }
        else if ( mixedShapes && index % 13 == 0 )
        {
            shape = CollisionShape( Collision::BoundingBox( Vector3( radius, radius * 0.5f, radius * 0.75f ), offset ) );
        }
        collider.boundingRadius = Collision::GetShapeBoundingRadius( shape ) + offset.x;
        REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliders, collider, shape ).IsValid() );
    }
    auto stage = std::make_unique<Physics::PhysicsBroadphaseStage>();
    auto diagnostics = std::make_unique<Physics::PhysicsStepDiagnostics>();
    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope scope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        stage->ReserveSceneCapacity( count, 4u );
        diagnostics->ReserveSceneCapacity( count );
    }
    std::array<Physics::PointJointConstraint, 4> joints;
    joints[0].bodyA = bodies.HandleForModelIndex( 1 );
    joints[0].bodyB = bodies.HandleForModelIndex( 2 );
    joints[1] = joints[0];
    std::swap( joints[1].bodyA, joints[1].bodyB );
    joints[2] = joints[0];
    // The fourth joint retains invalid handles and must exclude nothing.
    const std::vector<uint8_t> discrete( count, 0u );
    for ( int turn = 0; turn < 12; ++turn )
    {
        CAPTURE( turn );
        const BroadphaseBodyActivityView activity( count, sleep, awake, discrete, {} );
        const BroadphaseSweepContactEnvelope envelope( turn == 0 ? 0.0f : 0.125f, 0.05f, turn % 2 ? 0.01f : 0.02f );
        diagnostics->BeginStep( count );
        const auto expected = ReferenceDiscreteGridAndSweep( bodies, colliders, activity, envelope, joints );
        const auto actual = stage->Run( bodies, colliders, joints, activity, envelope, diagnostics->MutablePipelineTraceRecorder() );
        REQUIRE( actual.size() == expected.size() );
        CHECK( std::equal( actual.begin(), actual.end(), expected.begin() ) );
        if ( expected.size() > 1u )
        {
            auto reordered = expected;
            std::swap( reordered[0], reordered[1] );
            CHECK_FALSE( std::equal( actual.begin(), actual.end(), reordered.begin() ) );
            auto omitted = expected;
            omitted.erase( omitted.begin() );
            CHECK_FALSE( std::equal( actual.begin(), actual.end(), omitted.begin(), omitted.end() ) );
        }
        auto hot = bodies.MutableHotFields();
        for ( int index : awake )
        {
            hot.positionX[index] += hot.linearVelocityX[index] * 0.01f;
        }
        if ( turn % 3 == 0 )
        {
            stage->ResetTransientAfterReplayRestore();
            CHECK( stage->Stats().sweepMovers == 0u );
            CHECK( stage->Stats().jointEndpointResolutions == 0u );
        }
        if ( turn % 3 == 1 )
        {
            stage->InvalidateBodyTopology();
            CHECK( stage->Stats().sweepTargets == 0u );
            CHECK( stage->Stats().jointExclusionKeys == 0u );
        }
    }

    if ( count >= 512 )
    {
        CHECK( stage->Stats().sweepQueryNodes > 0u );
        CHECK( stage->Stats().sweepTargets < stage->Stats().sweepMovers * static_cast<uint64_t>( count - 1 ) );
        auto extreme = bodies.MutableHotFields();
        extreme.linearVelocityX[1] = 1.0e20f;
        stage->InvalidateBodyTopology();
        const BroadphaseBodyActivityView activity( count, sleep, awake, discrete, {} );
        const BroadphaseSweepContactEnvelope envelope( 0.125f, 0.05f, 0.01f );
        const auto expected = ReferenceDiscreteGridAndSweep( bodies, colliders, activity, envelope, joints );
        diagnostics->BeginStep( count );
        const auto actual = stage->Run( bodies, colliders, joints, activity, envelope, diagnostics->MutablePipelineTraceRecorder() );
        REQUIRE( actual.size() == expected.size() );
        CHECK( std::equal( actual.begin(), actual.end(), expected.begin() ) );
        CHECK( stage->Stats().sweepQueryNodes == 0u );
        CHECK( stage->Stats().sweepFullScanMovers == stage->Stats().sweepMovers );
        extreme.linearVelocityX[1] = 100.0f;
        stage->Clear();
        CHECK( stage->Stats().sweepScratchBytes == 0u );
        diagnostics->BeginStep( count );
        const auto recoveredExpected = ReferenceDiscreteGridAndSweep( bodies, colliders, activity, envelope, joints );
        const auto recovered = stage->Run( bodies, colliders, joints, activity, envelope, diagnostics->MutablePipelineTraceRecorder() );
        REQUIRE( recovered.size() == recoveredExpected.size() );
        CHECK( std::equal( recovered.begin(), recovered.end(), recoveredExpected.begin() ) );
        CHECK( stage->Stats().sweepQueryNodes > 0u );
        return;
    }

    // Every grid pair fills the exact committed output capacity. Repeated
    // sweep discoveries must be rejected before attempting another append.
    auto hot = bodies.MutableHotFields();
    awake.clear();
    std::fill( sleep.begin(), sleep.end(), uint8_t { 0u } );
    for ( int index = 0; index < count; ++index )
    {
        hot.positionX[index] = hot.positionY[index] = hot.positionZ[index] = 0.0f;
        hot.fixed[index] = 0u;
        hot.linearVelocityX[index] = 100.0f + static_cast<float>( index * 5 );
        awake.push_back( index );
    }
    stage->InvalidateBodyTopology();
    diagnostics->BeginStep( count );
    const BroadphaseBodyActivityView denseActivity( count, sleep, awake, discrete, {} );
    const auto pairs = stage->Run( bodies, colliders, {}, denseActivity, BroadphaseSweepContactEnvelope( 0.125f, 0.05f, 0.02f ), diagnostics->MutablePipelineTraceRecorder() );
    REQUIRE( pairs.size() == Physics::PhysicsCandidatePairCapacity( count ) );
    CHECK( std::adjacent_find( pairs.begin(), pairs.end() ) == pairs.end() );
    diagnostics->BeginStep( count );
    const auto filtered = stage->Run( bodies, colliders, joints, denseActivity, BroadphaseSweepContactEnvelope( 0.125f, 0.05f, 0.02f ), diagnostics->MutablePipelineTraceRecorder() );
    CHECK( filtered.size() + 1u == Physics::PhysicsCandidatePairCapacity( count ) );
    CHECK_FALSE( std::binary_search( filtered.begin(), filtered.end(), std::make_pair( 1, 2 ) ) );
    CHECK( stage->Stats().jointEndpointResolutions == joints.size() * 2u );
    CHECK( stage->Stats().jointExclusionKeys == 1u );

    // Retired handles must not exclude whichever body compaction puts at row 2.
    REQUIRE( colliders.DestroyColliderRecord( colliders.HandleForModelIndex( 2 ) ) );
    REQUIRE( bodies.DestroyBodyRecord( joints[0].bodyB ) );
    stage->InvalidateBodyTopology();
    awake.pop_back();
    sleep.pop_back();
    const std::vector<uint8_t> currentDiscrete( count - 1, 0u );
    const BroadphaseBodyActivityView compactActivity( count - 1, sleep, awake, currentDiscrete, {} );
    const BroadphaseSweepContactEnvelope currentEnvelope( 0.125f, 0.05f, 0.02f );
    const auto expected = ReferenceDiscreteGridAndSweep( bodies, colliders, compactActivity, currentEnvelope, joints );
    diagnostics->BeginStep( count - 1 );
    const auto compact = stage->Run( bodies, colliders, joints, compactActivity, currentEnvelope, diagnostics->MutablePipelineTraceRecorder() );
    REQUIRE( compact.size() == expected.size() );
    CHECK( std::equal( compact.begin(), compact.end(), expected.begin() ) );
    CHECK( stage->Stats().jointExclusionKeys == 0u );
    const auto refreshed = stage->RefreshCurrentContacts( bodies, colliders, joints, compactActivity, 0.02f );
    CHECK( refreshed.size() == Physics::PhysicsCandidatePairCapacity( count - 1 ) );
}

TEST_CASE( "Physics broadphase: exact reference covers discrete fast movers sleepers offsets and joint exclusions" )
{
    for ( int count : { 96, 520, 1025 } )
    {
        CAPTURE( count );
        CheckBroadphaseReference( count );
    }
    CheckBroadphaseReference( 520, true );
}

namespace
{
std::vector<Vector3> ReferenceGravityForces( const PhysicsBodyStore& bodies, std::span<const uint8_t> sleep, const PhysicsWorldForces& world )
{
    const auto hot = bodies.HotFields();
    const auto records = bodies.Records();
    std::vector<Vector3> result( records.size(), SkullbonezCore::Math::Vector::ZERO_VECTOR );
    const float softening = (std::max)( world.mutualGravity.softeningLength, TOLERANCE );
    const float softenedSq = softening * softening;
    // This retains the original serial (i,j) operation and addition order.
    // Compare component bits, never rounded text or object padding.
    for ( size_t i = 0; i < records.size(); ++i )
    {
        if ( records[i].mass <= TOLERANCE )
        {
            continue;
        }
        const bool receivesA = !hot.fixed[i] && hot.inverseMass[i] > 0.0f && ( i >= sleep.size() || !sleep[i] );
        for ( size_t j = i + 1; j < records.size(); ++j )
        {
            if ( records[j].mass <= TOLERANCE )
            {
                continue;
            }
            const bool receivesB = !hot.fixed[j] && hot.inverseMass[j] > 0.0f && ( j >= sleep.size() || !sleep[j] );
            if ( !receivesA && !receivesB )
            {
                continue;
            }
            const Vector3 a( hot.positionX[i], hot.positionY[i], hot.positionZ[i] );
            const Vector3 b( hot.positionX[j], hot.positionY[j], hot.positionZ[j] );
            const auto displacement = b - a;
            const float distanceSq = SkullbonezCore::Math::Vector::VectorMagSquared( displacement ) + softenedSq;
            const float inverseDistance = 1.0f / sqrtf( distanceSq );
            const float inverseCubed = inverseDistance * inverseDistance * inverseDistance;
            const auto force = displacement * ( world.mutualGravity.gravitationalConstant * records[i].mass * records[j].mass * inverseCubed );
            if ( receivesA )
            {
                result[i] += force;
            }
            if ( receivesB )
            {
                result[j] -= force;
            }
        }
    }
    return result;
}
} // namespace

static void CheckForceComponentBits( std::span<const Vector3> actual, std::span<const Vector3> expected )
{
    REQUIRE( actual.size() == expected.size() );
    for ( size_t index = 0; index < actual.size(); ++index )
    {
        CHECK( std::bit_cast<uint32_t>( actual[index].x ) == std::bit_cast<uint32_t>( expected[index].x ) );
        CHECK( std::bit_cast<uint32_t>( actual[index].y ) == std::bit_cast<uint32_t>( expected[index].y ) );
        CHECK( std::bit_cast<uint32_t>( actual[index].z ) == std::bit_cast<uint32_t>( expected[index].z ) );
    }
}

TEST_CASE( "Physics gravity: raw force bits match serial reference around the batching boundary" )
{
    PhysicsWorldForces world;
    world.mutualGravity = { true, 3.25f, 0.35f, true };
    PhysicsBodyStore& bodies = TestBodyStore();
    for ( int count : { 31, 511, 512, 513, 1025 } )
    {
        CAPTURE( count );
        bodies.Clear();
        std::vector<uint8_t> sleep( count );
        for ( int i = 0; i < count; ++i )
        {
            PhysicsBodyCreateRecord body;
            body.cold.mass = i % 13 == 0 ? 0.0f : 0.25f + ( i % 19 ) * 0.125f;
            body.hot.inverseMass = body.cold.mass > 0.0f ? 1.0f / body.cold.mass : 0.0f;
            body.hot.fixed = i % 11 == 0;
            body.hot.position = Vector3( static_cast<float>( i % 17 ) * 1.125f, static_cast<float>( i / 17 ) * 1.25f, static_cast<float>( i % 7 ) * -0.625f );
            (void)bodies.CreateBodyRecord( body );
            sleep[i] = i % 5 == 0;
        }
        PhysicsForceStage stage;
        {
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope scope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
            stage.ReserveBodyScratchCapacity( count );
        }
        const auto expected = ReferenceGravityForces( bodies, sleep, world );
        for ( int workers : { 0, 1, 4 } )
        {
            CAPTURE( workers );
            LockOrderValidator locks;
            WorkerPool pool( locks );
            pool.Initialise( workers );
            const SkullbonezCore::Physics::PhysicsExecutionSettings execution;
            const auto* actual = stage.PrepareMutualGravityForces( bodies.Records(), bodies.HotFields(), sleep, count, world, execution, pool );
            REQUIRE( actual != nullptr );
            CHECK( stage.Stats().pairContributions > 0u );
            CHECK( stage.Stats().scratchBytes == stage.CollectDynamicMemoryBytes() );
            CheckForceComponentBits( std::span<const Vector3>( actual, count ), expected );
            // A one-bit mutation must fail the same equality used above.
            CHECK( std::bit_cast<uint32_t>( actual[1].x ) != ( std::bit_cast<uint32_t>( expected[1].x ) ^ 1u ) );
            pool.Shutdown();
        }
        world.mutualGravity.enabled = false;
        LockOrderValidator locks;
        WorkerPool pool( locks );
        const SkullbonezCore::Physics::PhysicsExecutionSettings execution;
        CHECK( stage.PrepareMutualGravityForces( bodies.Records(), bodies.HotFields(), sleep, count, world, execution, pool ) == nullptr );
        CHECK( stage.Stats().pairContributions == 0u );
        CHECK( stage.Stats().pairBatches == 0u );
        stage.Clear();
        CHECK( stage.Stats().scratchBytes == 0u );
        world.mutualGravity.enabled = true;
    }
}
