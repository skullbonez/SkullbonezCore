/*
File: SkullbonezTests/TestSceneEntityStore.cpp
Purpose:
  Verifies scene identity storage and the paired render creation row.

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
  - Render creation publishes presentation, instance, and handle rows together.

Related:
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - Agentic/Plans/TODO/physics-authority-and-identity.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Rendering/RenderInstanceStore.h"

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

TEST_CASE( "RenderInstanceStore: preflighted creation publishes every render row" )
{
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::Rendering;

    RenderInstanceStore renderStore;
    RenderInstancePresentationRecord presentation;
    presentation.material.baseColor[0] = 0.25f;
    strcpy_s( presentation.displayName, "transaction_entity" );

    PhysicsBodyRecord body;
    body.handle = PhysicsBodyHandle{ 7u, 1u };
    body.sceneObjectId = PhysicsSceneObjectId{ 77u };
    body.replayBodyId = 77u;

    ColliderRecord collider;
    collider.handle = PhysicsColliderHandle{ 9u, 1u };
    collider.body = body.handle;
    collider.sceneObjectId = body.sceneObjectId;
    collider.replayBodyId = body.replayBodyId;
    collider.shape =
        SkullbonezCore::Math::CollisionDetection::BoundingSphere( 1.5f, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    collider.shapeKind = ColliderShapeKind::Sphere;
    collider.boundingRadius = 1.5f;

    REQUIRE( renderStore.CanAppendCreationRow( 0 ) );
    renderStore.CommitCreationRow( presentation, body, collider, 0 );

    CHECK( renderStore.PresentationCount() == 1 );
    CHECK( renderStore.Count() == 1 );
    CHECK( renderStore.HandleForModelIndex( 0 ).IsValid() );
    CHECK( renderStore.Records()[0].replayBodyId == 77u );
    CHECK( renderStore.Records()[0].material.baseColor[0] == doctest::Approx( 0.25f ) );
    CHECK_FALSE( renderStore.CanAppendCreationRow( 0 ) );
}
