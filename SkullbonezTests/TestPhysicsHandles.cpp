//
// File: SkullbonezTests/TestPhysicsHandles.cpp
// Purpose:
//   Lock body/collider handle semantics and aligned buoyancy-row lifecycle.
//
// Summary:
//   Physics handles are allocator identities, not dense row indices. The stores
//   keep simulation rows compact by moving the final row into a deleted slot,
//   while handle maps preserve live identity and reject stale generations.
//   Buoyancy tests prove its feature-owned facts mirror the same row operations.
//
// Glossary:
//   Handle generation: Version counter incremented when a handle slot is
//     retired, making old handles fail lookup after slot reuse.
//   Dense row: Compact store array index used by hot simulation scans.
//   Model row hint: Cached dense-row guess that a resolver can repair after
//     deletion compacts the store.
//   Collider authoring row: Cold scene round-trip text paired with one dense
//     hot collider row.
//   Scene object id: Stable id used by replay/diagnostics to find a body even
//     when a model-index hint is stale.
//   Hot SoA fields: 32-byte-aligned component arrays that keep adjacent body
//     values contiguous for cache-friendly stage scans.
//
// Invariants:
//   - HandleForModelIndex() and ModelIndexForHandle() are inverse for live rows.
//   - Destroying a middle row moves the final row down and updates its handle map.
//   - Collider hot and authoring rows compact together under the same handle.
//   - Buoyancy facts remain a compact five-float row and compact by swap-last.
//   - Reused handle slots must increment generation before accepting new records.
//   - Hot state has one authority: aligned SoA arrays; cold records do not
//     duplicate pose, velocity, inertia, motion-kind, or sleep fields.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsBodyStore.h
//   - SkullbonezSource/Physics/ColliderStore.h
//   - SkullbonezSource/Physics/BuoyancySystem.h
//   - Agentic/Reports/2026-07-15/math-fatal-call-site-survey.md
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestFixedSeed.h"

#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/BuoyancySystem.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRestoreService.h"

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cmath>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderAuthoringRecord;
using SkullbonezCore::Physics::BuoyancyBodyFacts;
using SkullbonezCore::Physics::BuoyancySystem;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakePhysicsSceneObjectId;
using SkullbonezCore::Physics::ModelRowHint;
using SkullbonezCore::Physics::PhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsBodyCreateRecord;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyPosition;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;
using SkullbonezCore::Physics::PhysicsWorldForces;
using SkullbonezCore::Runtime::ReplayRestoreService;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;

namespace
{
PhysicsBodyCreateRecord MakeBodyRecord( uint32_t sceneObjectIdValue, const Vector3& position )
{
    PhysicsBodyCreateRecord record;
    record.cold.sceneObjectId = MakePhysicsSceneObjectId( sceneObjectIdValue );
    record.hot.position = position;
    record.cold.mass = 1.0f;
    record.hot.inverseMass = 1.0f;
    return record;
}

ColliderRecord MakeColliderRecord( PhysicsBodyHandle body, uint32_t sceneObjectIdValue, float radius )
{
    ColliderRecord record;
    record.body = body;
    record.sceneObjectId = MakePhysicsSceneObjectId( sceneObjectIdValue );
    record.boundingRadius = radius;
    return record;
}

ColliderAuthoringRecord MakeColliderAuthoringRecord( const char* contactMaterialName )
{
    ColliderAuthoringRecord record;
    strncpy_s( record.contactMaterialName, contactMaterialName, _TRUNCATE );
    return record;
}

PhysicsBodyStore& TestBodyStore()
{
    // Why: PhysicsBodyStore owns fixed-capacity runtime arrays; static storage
    // keeps the focused unit fixture off the doctest thread stack.
    static PhysicsBodyStore store;
    store.Clear();
    return store;
}

ColliderStore& TestColliderStore()
{
    // Why: ColliderStore mirrors runtime fixed storage, so tests reuse one
    // static fixture and Clear() it between cases instead of stack-allocating it.
    static ColliderStore store;
    store.Clear();
    return store;
}
} // namespace


TEST_CASE( "Physics body cold record: vector metadata retains its 16-byte boundary" )
{
    CHECK_EQ( offsetof( PhysicsBodyRecord, rotationalInertia ), 16u );
    CHECK_EQ( sizeof( BuoyancyBodyFacts ), sizeof( float ) * 5u );
}


TEST_CASE( "Buoyancy facts: refresh, swap-last erase, trim, and clear preserve dense row semantics" )
{
    // Why: the fixed-step owner relies on row equality with body/collider
    // stores. This focused lifecycle test catches field drift and compaction
    // mistakes without reaching through PhysicsEngine internals.
    static BuoyancySystem buoyancy;
    buoyancy.Clear();

    PhysicsBodyCreateDesc first;
    first.volume = 1.0f;
    first.projectedSurfaceArea = 2.0f;
    first.dragCoefficient = 3.0f;
    first.contactEpsilon = 0.01f;
    PhysicsBodyCreateDesc middle = first;
    middle.volume = 11.0f;
    middle.projectedSurfaceArea = 12.0f;
    middle.dragCoefficient = 13.0f;
    middle.contactEpsilon = 0.02f;
    PhysicsBodyCreateDesc last = first;
    last.volume = 21.0f;
    last.projectedSurfaceArea = 22.0f;
    last.dragCoefficient = 23.0f;
    last.contactEpsilon = 0.03f;

    REQUIRE( buoyancy.AppendBodyFacts( first ) );
    REQUIRE( buoyancy.AppendBodyFacts( middle ) );
    REQUIRE( buoyancy.AppendBodyFacts( last ) );
    REQUIRE( buoyancy.Count() == 3 );
    buoyancy.MutableFacts()[0].submergedVolumePercent = 0.75f;

    first.volume = 4.0f;
    REQUIRE( buoyancy.RefreshBodyFacts( 0, first ) );
    CHECK( buoyancy.Facts()[0].volume == doctest::Approx( 4.0f ) );
    CHECK( buoyancy.Facts()[0].submergedVolumePercent == doctest::Approx( 0.0f ) );

    REQUIRE( buoyancy.EraseBodyFactsSwapLast( 1 ) );
    REQUIRE( buoyancy.Count() == 2 );
    CHECK( buoyancy.Facts()[1].volume == doctest::Approx( 21.0f ) );
    CHECK( buoyancy.Facts()[1].projectedSurfaceArea == doctest::Approx( 22.0f ) );
    CHECK( buoyancy.Facts()[1].dragCoefficient == doctest::Approx( 23.0f ) );
    CHECK( buoyancy.Facts()[1].contactEpsilon == doctest::Approx( 0.03f ) );

    REQUIRE( buoyancy.TrimToCount( 1 ) );
    CHECK( buoyancy.Count() == 1 );
    buoyancy.Clear();
    CHECK( buoyancy.Facts().empty() );
}


TEST_CASE( "Property invariant: equal-and-opposite impulses conserve pair momentum [seed 0x16A11CE5]" )
{
    SkullbonezTests::FixedSeed random( 0x16A11CE5u );
    PhysicsBodyStore& store = TestBodyStore();

    // Invariant: application-point torque may change angular momentum, but the
    // zero-offset +J/-J pair cannot change total linear momentum.
    for ( int sample = 0; sample < 64; ++sample )
    {
        store.Clear();
        const float leftMass = random.Float( 0.25f, 20.0f );
        const float rightMass = random.Float( 0.25f, 20.0f );
        PhysicsBodyCreateRecord left = MakeBodyRecord( 1u, Vector3( 0.0f, 0.0f, 0.0f ) );
        left.cold.mass = leftMass;
        left.hot.inverseMass = 1.0f / leftMass;
        left.hot.linearVelocity =
            Vector3( random.Float( -5.0f, 5.0f ), random.Float( -5.0f, 5.0f ), random.Float( -5.0f, 5.0f ) );
        PhysicsBodyCreateRecord right = MakeBodyRecord( 2u, Vector3( 0.0f, 0.0f, 0.0f ) );
        right.cold.mass = rightMass;
        right.hot.inverseMass = 1.0f / rightMass;
        right.hot.linearVelocity =
            Vector3( random.Float( -5.0f, 5.0f ), random.Float( -5.0f, 5.0f ), random.Float( -5.0f, 5.0f ) );
        const PhysicsBodyHandle leftHandle = store.CreateBodyRecord( left );
        const PhysicsBodyHandle rightHandle = store.CreateBodyRecord( right );
        const Vector3 momentumBefore = left.hot.linearVelocity * leftMass + right.hot.linearVelocity * rightMass;
        const Vector3 impulse( random.Float( -12.0f, 12.0f ),
                               random.Float( -12.0f, 12.0f ),
                               random.Float( -12.0f, 12.0f ) );

        REQUIRE( store.ApplyBodyImpulse( leftHandle, impulse, Vector3( 0.0f, 0.0f, 0.0f ) ) );
        REQUIRE( store.ApplyBodyImpulse( rightHandle, impulse * -1.0f, Vector3( 0.0f, 0.0f, 0.0f ) ) );
        REQUIRE( store.ConsumePendingBodyImpulse( 0 ) );
        REQUIRE( store.ConsumePendingBodyImpulse( 1 ) );

        const auto hot = store.HotFields();
        const Vector3 leftVelocity( hot.linearVelocityX[0], hot.linearVelocityY[0], hot.linearVelocityZ[0] );
        const Vector3 rightVelocity( hot.linearVelocityX[1], hot.linearVelocityY[1], hot.linearVelocityZ[1] );
        const Vector3 momentumAfter = leftVelocity * leftMass + rightVelocity * rightMass;
        const Vector3 drift = momentumAfter - momentumBefore;
        CHECK( fabsf( drift.x ) <= 0.0001f );
        CHECK( fabsf( drift.y ) <= 0.0001f );
        CHECK( fabsf( drift.z ) <= 0.0001f );
    }
}


TEST_CASE( "Physics handles: body store resolves fresh handles and scene object ids" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );

    CHECK( first.IsValid() );
    CHECK( second.IsValid() );
    CHECK( store.Count() == 2 );
    CHECK( store.HandleForModelIndex( 0 ) == first );
    CHECK( store.HandleForModelIndex( 1 ) == second );
    CHECK( store.ModelIndexForHandle( first ) == 0 );
    CHECK( store.ModelIndexForHandle( second ) == 1 );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( store.RecordForHandle( second )->sceneObjectId == MakePhysicsSceneObjectId( 202u ) );
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectId( 202u ), 1 ) == second );
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectId( 202u ), 0 ) == second );
}


TEST_CASE( "Physics body SoA: aligned hot fields are the sole hot-state authority" )
{
    PhysicsBodyStore& store = TestBodyStore();
    PhysicsBodyCreateRecord initial = MakeBodyRecord( 303u, Vector3( 1.25f, -2.5f, 3.75f ) );
    initial.hot.orientation = SkullbonezCore::Math::Orientation::Quaternion( 0.1f, 0.2f, 0.3f, 0.9f );
    initial.hot.linearVelocity = Vector3( 4.0f, 5.0f, 6.0f );
    initial.hot.angularVelocity = Vector3( 7.0f, 8.0f, 9.0f );
    initial.hot.inverseRotationalInertia = Vector3( 0.25f, 0.5f, 0.75f );
    initial.hot.boundingRadius = 2.25f;
    const PhysicsBodyHandle body = store.CreateBodyRecord( initial );

    const auto hot = static_cast<const PhysicsBodyStore&>( store ).HotFields();
    REQUIRE( hot.positionX.size() == 1u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.positionX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.positionY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.positionZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.orientationW.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.linearVelocityX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.linearVelocityY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.linearVelocityZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.angularVelocityX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.angularVelocityY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.angularVelocityZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseMass.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseInertiaX.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseInertiaY.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.inverseInertiaZ.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.boundingRadius.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.fixed.data() ) % 32u == 0u );
    CHECK( reinterpret_cast<std::uintptr_t>( hot.awake.data() ) % 32u == 0u );
    CHECK( hot.positionX[0] == initial.hot.position.x );
    CHECK( hot.positionY[0] == initial.hot.position.y );
    CHECK( hot.positionZ[0] == initial.hot.position.z );
    CHECK( hot.linearVelocityZ[0] == initial.hot.linearVelocity.z );
    CHECK( hot.inverseInertiaY[0] == initial.hot.inverseRotationalInertia.y );

    auto mutableHot = store.MutableHotFields();
    mutableHot.positionX[0] = -11.5f;
    mutableHot.linearVelocityY[0] = 12.25f;
    mutableHot.awake[0] = 0u;
    REQUIRE( store.RecordForHandle( body ) != nullptr );
    CHECK( mutableHot.positionX[0] == -11.5f );
    CHECK( mutableHot.linearVelocityY[0] == 12.25f );
    CHECK( mutableHot.awake[0] == 0u );

    mutableHot.angularVelocityZ[0] = -6.5f;
    mutableHot.inverseMass[0] = 0.125f;
    const auto refreshedHot = static_cast<const PhysicsBodyStore&>( store ).HotFields();
    CHECK( refreshedHot.angularVelocityZ[0] == -6.5f );
    CHECK( refreshedHot.inverseMass[0] == 0.125f );
}


TEST_CASE( "Physics handles: descriptor reorder preserves handle-owned pending impulse" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const Vector3 impulse( 7.0f, 8.0f, 9.0f );
    const Vector3 applicationPoint( 1.0f, 2.0f, 3.0f );
    REQUIRE( store.SetPendingBodyImpulse( second, impulse, applicationPoint ) );

    PhysicsBodyCreateDesc reordered[2];
    reordered[0].sceneObjectId = MakePhysicsSceneObjectId( 202u );
    reordered[0].position = Vector3( 20.0f, 0.0f, 0.0f );
    reordered[1].sceneObjectId = MakePhysicsSceneObjectId( 101u );
    reordered[1].position = Vector3( 10.0f, 0.0f, 0.0f );
    const uint8_t awakeRows[2] = {};

    store.LoadFromDescriptors( reordered, awakeRows );

    CHECK( store.HandleForModelIndex( 0 ) == second );
    CHECK( store.HandleForModelIndex( 1 ) == first );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( store.RecordForHandle( second )->hasPendingImpulse );
    CHECK( store.RecordForHandle( second )->pendingImpulse == impulse );
    CHECK( store.RecordForHandle( second )->pendingImpulseApplicationPoint == applicationPoint );
    REQUIRE( store.RecordForHandle( first ) != nullptr );
    CHECK_FALSE( store.RecordForHandle( first )->hasPendingImpulse );
}


TEST_CASE( "Replay restore: stable body ids override stale row hints" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );

    ReplaySolverFrameSample sample;
    ReplaySolverBodySample firstSample;
    firstSample.id.value = 101u;
    firstSample.modelRow = SkullbonezCore::Physics::MakeModelRowHint( 1 );
    sample.bodies.push_back( firstSample );
    ReplaySolverBodySample secondSample;
    secondSample.id.value = 202u;
    secondSample.modelRow = SkullbonezCore::Physics::MakeModelRowHint( 0 );
    sample.bodies.push_back( secondSample );

    ReplayRestoreService::ResolvedBodyTable resolved{};
    char reason[128] = {};
    REQUIRE( ReplayRestoreService::ResolveBodiesForRestore( store, sample, resolved, reason, sizeof( reason ) ) );
    CHECK( resolved[0] == first );
    CHECK( resolved[1] == second );

    sample.bodies[0].id.value = 999u;
    CHECK_FALSE( ReplayRestoreService::ResolveBodiesForRestore( store, sample, resolved, reason, sizeof( reason ) ) );
    REQUIRE( store.RecordForHandle( first ) != nullptr );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( PhysicsBodyPosition( store.HotFields(), static_cast<std::size_t>( store.ModelIndexForHandle( first ) ) ).x ==
           1.0f );
    CHECK(
        PhysicsBodyPosition( store.HotFields(), static_cast<std::size_t>( store.ModelIndexForHandle( second ) ) ).x ==
        2.0f );

    sample.bodies[0].id.value = 101u;
    sample.bodies[1].id.value = 101u;
    CHECK_FALSE( ReplayRestoreService::ResolveBodiesForRestore( store, sample, resolved, reason, sizeof( reason ) ) );
    CHECK( std::strstr( reason, "duplicate body ids" ) != nullptr );
}


TEST_CASE( "Physics handles: body destroy moves dense rows and rejects stale generations" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last = store.CreateBodyRecord( MakeBodyRecord( 303u, Vector3( 3.0f, 0.0f, 0.0f ) ) );

    CHECK( store.DestroyBodyRecord( middle ) );

    CHECK( store.Count() == 2 );
    CHECK( store.Contains( first ) );
    CHECK( store.Contains( last ) );
    CHECK_FALSE( store.Contains( middle ) );
    CHECK( store.ModelIndexForHandle( middle ) == -1 );
    CHECK( store.RecordForHandle( middle ) == nullptr );
    CHECK( store.HandleForModelIndex( 1 ) == last );
    CHECK( store.ModelIndexForHandle( last ) == 1 );
    REQUIRE( store.RecordForHandle( last ) != nullptr );
    CHECK( store.RecordForHandle( last )->sceneObjectId == MakePhysicsSceneObjectId( 303u ) );

    const PhysicsBodyHandle replacement = store.CreateBodyRecord( MakeBodyRecord( 404u, Vector3( 4.0f, 0.0f, 0.0f ) ) );
    CHECK( replacement.index == middle.index );
    CHECK( replacement.generation != middle.generation );
    CHECK( store.Contains( replacement ) );
    CHECK_FALSE( store.Contains( middle ) );
}


TEST_CASE( "Physics handles: body row hints self-heal and invalidate stale handles" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first = store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle = store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last = store.CreateBodyRecord( MakeBodyRecord( 303u, Vector3( 3.0f, 0.0f, 0.0f ) ) );

    ModelRowHint wrongHint;
    wrongHint.value = 2;
    CHECK( store.ResolveModelRow( first, wrongHint ) == 0 );
    CHECK( wrongHint.value == 0 );

    ModelRowHint movedHint;
    movedHint.value = 2;
    CHECK( store.DestroyBodyRecord( middle ) );
    CHECK( store.ResolveModelRow( last, movedHint ) == 1 );
    CHECK( movedHint.value == 1 );

    ModelRowHint staleHint;
    staleHint.value = 0;
    CHECK( store.DestroyBodyRecord( first ) );
    CHECK( store.ResolveModelRow( first, staleHint ) == -1 );
    CHECK( staleHint.value == -1 );
}


TEST_CASE( "Physics handles: collider store resolves body, scene, and model handles" )
{
    ColliderStore& store = TestColliderStore();
    PhysicsBodyHandle body;
    body.index = 7u;
    body.generation = 1u;

    const PhysicsColliderHandle collider = store.CreateColliderRecord( MakeColliderRecord( body, 707u, 3.0f ) );

    CHECK( collider.IsValid() );
    CHECK( store.Count() == 1 );
    CHECK( store.HandleForModelIndex( 0 ) == collider );
    CHECK( store.ModelIndexForHandle( collider ) == 0 );
    CHECK( store.HandleForBodyHandle( body ) == collider );
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectId( 707u ) ) == collider );
    REQUIRE( store.RecordForHandle( collider ) != nullptr );
    CHECK( store.RecordForHandle( collider )->boundingRadius == 3.0f );
}


TEST_CASE( "Physics handles: collider destroy moves rows and rejects stale handles" )
{
    ColliderStore& store = TestColliderStore();
    PhysicsBodyHandle bodyA{ 11u, 1u };
    PhysicsBodyHandle bodyB{ 12u, 1u };
    PhysicsBodyHandle bodyC{ 13u, 1u };
    const PhysicsColliderHandle first =
        store.CreateColliderRecord( MakeColliderRecord( bodyA, 111u, 1.0f ), MakeColliderAuthoringRecord( "stone" ) );
    const PhysicsColliderHandle middle =
        store.CreateColliderRecord( MakeColliderRecord( bodyB, 222u, 2.0f ), MakeColliderAuthoringRecord( "metal" ) );
    const PhysicsColliderHandle last =
        store.CreateColliderRecord( MakeColliderRecord( bodyC, 333u, 3.0f ), MakeColliderAuthoringRecord( "wood" ) );

    CHECK( store.DestroyColliderRecord( middle ) );

    CHECK( store.Count() == 2 );
    CHECK( store.Contains( first ) );
    CHECK( store.Contains( last ) );
    CHECK_FALSE( store.Contains( middle ) );
    CHECK( store.HandleForModelIndex( 1 ) == last );
    CHECK( store.ModelIndexForHandle( last ) == 1 );
    CHECK( store.HandleForBodyHandle( bodyC ) == last );
    REQUIRE( store.AuthoringRecordForHandle( last ) != nullptr );
    CHECK( std::strcmp( store.AuthoringRecordForHandle( last )->contactMaterialName, "wood" ) == 0 );

    const PhysicsColliderHandle replacement =
        store.CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle{ 14u, 1u }, 444u, 4.0f ) );
    CHECK( replacement.index == middle.index );
    CHECK( replacement.generation != middle.generation );
    CHECK( store.Contains( replacement ) );
    CHECK_FALSE( store.Contains( middle ) );
}


TEST_CASE( "Physics handles: collider rows realign to compacted body handles" )
{
    PhysicsBodyStore& bodies = TestBodyStore();
    ColliderStore& colliders = TestColliderStore();
    const PhysicsBodyHandle first = bodies.CreateBodyRecord( MakeBodyRecord( 111u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle = bodies.CreateBodyRecord( MakeBodyRecord( 222u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last = bodies.CreateBodyRecord( MakeBodyRecord( 333u, Vector3( 3.0f, 0.0f, 0.0f ) ) );
    const PhysicsColliderHandle firstCollider =
        colliders.CreateColliderRecord( MakeColliderRecord( first, 111u, 1.0f ) );
    const PhysicsColliderHandle middleCollider =
        colliders.CreateColliderRecord( MakeColliderRecord( middle, 222u, 2.0f ) );
    const PhysicsColliderHandle lastCollider = colliders.CreateColliderRecord( MakeColliderRecord( last, 333u, 3.0f ) );

    REQUIRE( bodies.DestroyBodyRecord( middle ) );
    REQUIRE( colliders.DestroyColliderRecord( middleCollider ) );
    REQUIRE( colliders.RefreshBodyBindings( bodies ) );

    CHECK( colliders.HandleForModelIndex( 0 ) == firstCollider );
    CHECK( colliders.HandleForModelIndex( 1 ) == lastCollider );
    REQUIRE( colliders.Count() == 2 );
    CHECK( colliders.Data()[1].body == last );
    CHECK( colliders.Data()[1].sceneObjectId == MakePhysicsSceneObjectId( 333u ) );
}


TEST_CASE( "Physics impulses: zero mass and inertia absorb immediate and pending components" )
{
    PhysicsBodyStore& bodies = TestBodyStore();
    ColliderStore& colliders = TestColliderStore();
    PhysicsBodyCreateRecord body = MakeBodyRecord( 808u, Vector3( 0.0f, 10.0f, 0.0f ) );
    body.cold.mass = 0.0f;
    body.cold.rotationalInertia = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    body.hot.inverseMass = 0.0f;
    body.hot.inverseRotationalInertia = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    body.hot.linearVelocity = Vector3( 1.0f, 2.0f, 3.0f );
    body.hot.angularVelocity = Vector3( 0.4f, 0.5f, 0.6f );
    const PhysicsBodyHandle handle = bodies.CreateBodyRecord( body );
    colliders.CreateColliderRecord( MakeColliderRecord( handle, 808u, 1.0f ) );

    const Vector3 mutualGravityImpulse( 9.0f, 8.0f, 7.0f );
    const BuoyancyBodyFacts buoyancyFacts;
    REQUIRE(
        bodies.ApplyForces( PhysicsWorldForces{}, colliders, {}, buoyancyFacts, 0, 1.0f, &mutualGravityImpulse ) );
    auto hot = bodies.HotFields();
    CHECK( hot.linearVelocityX[0] == doctest::Approx( 1.0f ) );
    CHECK( hot.linearVelocityY[0] == doctest::Approx( 2.0f ) );
    CHECK( hot.linearVelocityZ[0] == doctest::Approx( 3.0f ) );
    CHECK( hot.angularVelocityX[0] == doctest::Approx( 0.4f ) );
    CHECK( hot.angularVelocityY[0] == doctest::Approx( 0.5f ) );
    CHECK( hot.angularVelocityZ[0] == doctest::Approx( 0.6f ) );

    REQUIRE( bodies.SetPendingBodyImpulse( handle, Vector3( 3.0f, 4.0f, 5.0f ), Vector3( 2.0f, 0.0f, 1.0f ) ) );
    REQUIRE( bodies.ConsumePendingBodyImpulse( 0 ) );
    hot = bodies.HotFields();
    CHECK( hot.linearVelocityX[0] == doctest::Approx( 1.0f ) );
    CHECK( hot.linearVelocityY[0] == doctest::Approx( 2.0f ) );
    CHECK( hot.linearVelocityZ[0] == doctest::Approx( 3.0f ) );
    CHECK( hot.angularVelocityX[0] == doctest::Approx( 0.4f ) );
    CHECK( hot.angularVelocityY[0] == doctest::Approx( 0.5f ) );
    CHECK( hot.angularVelocityZ[0] == doctest::Approx( 0.6f ) );
    REQUIRE( bodies.RecordForHandle( handle ) != nullptr );
    CHECK_FALSE( bodies.RecordForHandle( handle )->hasPendingImpulse );
    CHECK( bodies.RecordForHandle( handle )->pendingImpulse == SkullbonezCore::Math::Vector::ZERO_VECTOR );
    CHECK( bodies.RecordForHandle( handle )->pendingImpulseApplicationPoint ==
           SkullbonezCore::Math::Vector::ZERO_VECTOR );

    CHECK( std::isfinite( hot.linearVelocityX[0] ) );
    CHECK( std::isfinite( hot.angularVelocityX[0] ) );
}
