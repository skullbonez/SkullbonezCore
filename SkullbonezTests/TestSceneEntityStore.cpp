/*
File: SkullbonezTests/TestSceneEntityStore.cpp
Purpose:
  Verifies fixed-capacity scene identity and asset-affiliation storage.

Mental model:
  Creation preflights metadata without mutation, then commits the body-linked
  row once downstream owner rows exist.

Glossary:
  Stable identity: Nonzero PhysicsSceneObjectId independent of dense row order.
  Asset affiliation: Durable library/asset/instance/part provenance.

Invariants:
  - Duplicate ids and capacity exhaustion are recoverable preflight failures.
  - Successful commits retain exact identity, material, and affiliation values.
  - Clear and commit reuse the pre-scene reservation without growing storage.

Related:
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - Agentic/Plans/TODO/physics-authority-and-identity.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"

#include <string>

using namespace SkullbonezCore::Basics;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsSceneObjectId;

TEST_CASE( "SceneEntityStore: preflight and commit preserve durable owner metadata" )
{
    static SceneEntityStore store;
    store.Clear();
    store.ConfigureCapacity( 2 );

    SceneEntityCreateDesc entity;
    entity.sceneObjectId = PhysicsSceneObjectId{ 42u };
    entity.SetName( "tower_part" );
    entity.SetRenderTint( 0.2f, 0.4f, 0.6f, 1.0f );
    entity.SetAssetAffiliation( PhysicsSceneObjectId{ 900u }, "structures", "tower", "tower_a", "wall", 3u );
    REQUIRE( store.PreflightAppend( entity ).ok );

    PhysicsBodyHandle body;
    body.index = 7u;
    body.generation = 2u;
    store.CommitAppend( entity, body );

    REQUIRE( store.Count() == 1 );
    const SceneEntityRecord& record = store.At( 0 );
    CHECK( record.sceneObjectId.value == 42u );
    CHECK( record.body.index == 7u );
    CHECK( std::string( record.displayName ) == "tower_part" );
    CHECK( record.renderMaterial.baseColor[0] == doctest::Approx( 0.2f ) );
    CHECK( record.asset.isAssetBacked );
    CHECK( record.asset.rootObjectId.value == 900u );
    CHECK( std::string( record.asset.libraryToken ) == "structures" );
    CHECK( std::string( record.asset.assetName ) == "tower" );
    CHECK( std::string( record.asset.instanceName ) == "tower_a" );
    CHECK( std::string( record.asset.partName ) == "wall" );
    CHECK( record.asset.partIndex == 3u );
    CHECK( store.FindByDisplayName( "tower_part" ) == 0 );
    CHECK( store.FindBySceneObjectId( PhysicsSceneObjectId{ 42u } ) == 0 );

    PhysicsBodyHandle refreshedBody;
    refreshedBody.index = 9u;
    refreshedBody.generation = 4u;
    store.UpdateBodyHandleAt( 0, refreshedBody, PhysicsSceneObjectId{ 42u } );
    CHECK( store.At( 0 ).body.index == 9u );
    CHECK( store.At( 0 ).body.generation == 4u );

    CHECK_FALSE( store.PreflightAppend( entity ).ok );
}

TEST_CASE( "SceneEntityStore: active capacity is enforced without growth" )
{
    static SceneEntityStore store;
    store.Clear();
    store.ConfigureCapacity( 1 );
    const uint64_t reservedBytes = store.CapacityBytes();

    SceneEntityCreateDesc first;
    first.sceneObjectId = PhysicsSceneObjectId{ 1u };
    first.SetName( "first" );
    PhysicsBodyHandle body;
    body.index = 0u;
    body.generation = 1u;
    store.CommitAppend( first, body );
    CHECK( store.CapacityBytes() == reservedBytes );

    SceneEntityCreateDesc second;
    second.sceneObjectId = PhysicsSceneObjectId{ 2u };
    CHECK_FALSE( store.PreflightAppend( second ).ok );
    CHECK( store.Count() == 1 );
    CHECK( store.Capacity() == 1 );
    CHECK( store.TrimToCount( 0 ) );
    CHECK( store.Count() == 0 );
    CHECK( store.CapacityBytes() == reservedBytes );
    CHECK( store.PreflightAppend( second ).ok );
}
