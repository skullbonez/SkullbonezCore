//
// File: SkullbonezTests/TestPhysicsHandles.cpp
// Purpose:
//   Lock the first focused tests for physics body and collider handle semantics.
//
// Mental model:
//   Physics handles are allocator identities, not dense row indices. The stores
//   keep simulation rows compact by moving the final row into a deleted slot,
//   while handle maps preserve live identity and reject stale generations.
//
// Glossary:
//   Handle generation: Version counter incremented when a handle slot is
//     retired, making old handles fail lookup after slot reuse.
//   Dense row: Compact store array index used by hot simulation scans.
//   Model row hint: Cached dense-row guess that a resolver can repair after
//     deletion compacts the store.
//   Replay body id: Stable id used by replay/diagnostics to find a body even
//     when a model-index hint is stale.
//
// Invariants:
//   - HandleForModelIndex() and ModelIndexForHandle() are inverse for live rows.
//   - Destroying a middle row moves the final row down and updates its handle map.
//   - Reused handle slots must increment generation before accepting new records.
//
// Related:
//   - SkullbonezSource/Physics/PhysicsBodyStore.h
//   - SkullbonezSource/Physics/ColliderStore.h
//   - Agentic/Plans/TODO/behavioral-test-depth.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::MakePhysicsSceneObjectIdFromReplayBodyId;
using SkullbonezCore::Physics::ModelRowHint;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsColliderHandle;

namespace
{
PhysicsBodyRecord MakeBodyRecord( uint32_t replayBodyId, const Vector3& position )
{
    PhysicsBodyRecord record;
    record.replayBodyId = replayBodyId;
    record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( replayBodyId );
    record.position = position;
    record.mass = 1.0f;
    record.invMass = 1.0f;
    return record;
}

ColliderRecord MakeColliderRecord( PhysicsBodyHandle body, uint32_t replayBodyId, float radius )
{
    ColliderRecord record;
    record.body = body;
    record.replayBodyId = replayBodyId;
    record.sceneObjectId = MakePhysicsSceneObjectIdFromReplayBodyId( replayBodyId );
    record.boundingRadius = radius;
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


TEST_CASE( "Physics handles: body store resolves fresh handles and replay ids" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first =
        store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle second =
        store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );

    CHECK( first.IsValid() );
    CHECK( second.IsValid() );
    CHECK( store.Count() == 2 );
    CHECK( store.HandleForModelIndex( 0 ) == first );
    CHECK( store.HandleForModelIndex( 1 ) == second );
    CHECK( store.ModelIndexForHandle( first ) == 0 );
    CHECK( store.ModelIndexForHandle( second ) == 1 );
    REQUIRE( store.RecordForHandle( second ) != nullptr );
    CHECK( store.RecordForHandle( second )->replayBodyId == 202u );
    CHECK( store.HandleForReplayBodyId( 202u, 1 ) == second );
    CHECK( store.HandleForReplayBodyId( 202u, 0 ) == second );
}


TEST_CASE( "Physics handles: body destroy moves dense rows and rejects stale generations" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first =
        store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle =
        store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last =
        store.CreateBodyRecord( MakeBodyRecord( 303u, Vector3( 3.0f, 0.0f, 0.0f ) ) );

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
    CHECK( store.RecordForHandle( last )->replayBodyId == 303u );

    const PhysicsBodyHandle replacement =
        store.CreateBodyRecord( MakeBodyRecord( 404u, Vector3( 4.0f, 0.0f, 0.0f ) ) );
    CHECK( replacement.index == middle.index );
    CHECK( replacement.generation != middle.generation );
    CHECK( store.Contains( replacement ) );
    CHECK_FALSE( store.Contains( middle ) );
}


TEST_CASE( "Physics handles: body row hints self-heal and invalidate stale handles" )
{
    PhysicsBodyStore& store = TestBodyStore();
    const PhysicsBodyHandle first =
        store.CreateBodyRecord( MakeBodyRecord( 101u, Vector3( 1.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle middle =
        store.CreateBodyRecord( MakeBodyRecord( 202u, Vector3( 2.0f, 0.0f, 0.0f ) ) );
    const PhysicsBodyHandle last =
        store.CreateBodyRecord( MakeBodyRecord( 303u, Vector3( 3.0f, 0.0f, 0.0f ) ) );

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
    CHECK( store.HandleForSceneObjectId( MakePhysicsSceneObjectIdFromReplayBodyId( 707u ) ) == collider );
    REQUIRE( store.RecordForHandle( collider ) != nullptr );
    CHECK( store.RecordForHandle( collider )->boundingRadius == 3.0f );
}


TEST_CASE( "Physics handles: collider destroy moves rows and rejects stale handles" )
{
    ColliderStore& store = TestColliderStore();
    PhysicsBodyHandle bodyA{ 11u, 1u };
    PhysicsBodyHandle bodyB{ 12u, 1u };
    PhysicsBodyHandle bodyC{ 13u, 1u };
    const PhysicsColliderHandle first = store.CreateColliderRecord( MakeColliderRecord( bodyA, 111u, 1.0f ) );
    const PhysicsColliderHandle middle = store.CreateColliderRecord( MakeColliderRecord( bodyB, 222u, 2.0f ) );
    const PhysicsColliderHandle last = store.CreateColliderRecord( MakeColliderRecord( bodyC, 333u, 3.0f ) );

    CHECK( store.DestroyColliderRecord( middle ) );

    CHECK( store.Count() == 2 );
    CHECK( store.Contains( first ) );
    CHECK( store.Contains( last ) );
    CHECK_FALSE( store.Contains( middle ) );
    CHECK( store.HandleForModelIndex( 1 ) == last );
    CHECK( store.ModelIndexForHandle( last ) == 1 );
    CHECK( store.HandleForBodyHandle( bodyC ) == last );

    const PhysicsColliderHandle replacement =
        store.CreateColliderRecord( MakeColliderRecord( PhysicsBodyHandle{ 14u, 1u }, 444u, 4.0f ) );
    CHECK( replacement.index == middle.index );
    CHECK( replacement.generation != middle.generation );
    CHECK( store.Contains( replacement ) );
    CHECK_FALSE( store.Contains( middle ) );
}
