/*
File: SkullbonezTests/TestSceneEntityStore.cpp
Purpose:
  Verifies scene identity storage and the paired render creation row.

Summary:
  Creation preflights metadata without mutation, then commits the body-linked
  row once downstream owner rows exist.

Glossary:

  Asset affiliation: Durable library/asset/instance/part provenance.

  Editor metadata: Transient visibility and mutation-lock state keyed by stable identity.

Invariants:
  - Duplicate ids and capacity exhaustion are recoverable preflight failures.
  - Successful commits retain exact identity, material, affiliation, and group values.
  - Clear and commit reuse the pre-scene reservation without growing storage.
  - Render creation publishes presentation, instance, and handle rows together.
  - Owner-prepared shadow stream identity survives render-row publication.
  - A refresh topology mismatch clears presentation, instance, and handle rows
    before any body/collider source is indexed.
  - Swap-last deletion republishes handle-to-row identity before the next
    SceneWorld render snapshot is exposed.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - SkullbonezSource/Rendering/RenderInstanceStore.h
*/
#include "../ThirdPtySource/doctest/doctest.h"
#include "TestColliderStoreFixtures.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Rendering/RenderInstanceStore.h"

#include <string>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsSceneObjectId;

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

TEST_CASE( "SceneEntityStore: preflight and commit preserve durable owner metadata" )
{
    static SceneEntityStore store( diagnostics );
    store.Clear();
    store.ConfigureCapacity( 2 );

    SceneEntityCreateDesc entity;
    entity.sceneObjectId = PhysicsSceneObjectId { 42u };
    entity.SetName( "tower_part" );
    entity.SetRenderTint( 0.2f, 0.4f, 0.6f, 1.0f );
    entity.SetAssetAffiliation( PhysicsSceneObjectId { 900u }, "structures", "tower", "tower_a", "wall", 3u );
    entity.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, PhysicsSceneObjectId { 42u }, 0 );
    entity.editorVisible = false;
    entity.editorLocked = true;
    REQUIRE( store.PreflightAppend( entity ).Ok() );

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
    CHECK( record.behaviorGroup.kind == SceneBehaviorGroupKind::ReleasableTree );
    CHECK( record.behaviorGroup.rootObjectId.value == 42u );
    CHECK( record.behaviorGroup.partIndex == 0 );
    CHECK_FALSE( record.editorVisible );
    CHECK( record.editorLocked );
    CHECK( store.FindByDisplayName( "tower_part" ) == 0 );
    CHECK( store.FindBySceneObjectId( PhysicsSceneObjectId { 42u } ) == 0 );

    PhysicsBodyHandle refreshedBody;
    refreshedBody.index = 9u;
    refreshedBody.generation = 4u;
    store.UpdateBodyHandleAt( 0, refreshedBody, PhysicsSceneObjectId { 42u } );
    CHECK( store.At( 0 ).body.index == 9u );
    CHECK( store.At( 0 ).body.generation == 4u );

    CHECK_FALSE( store.PreflightAppend( entity ).Ok() );

    SceneEntityCreateDesc orphan;
    orphan.sceneObjectId = PhysicsSceneObjectId { 44u };
    orphan.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, PhysicsSceneObjectId { 999u }, 1 );
    CHECK_FALSE( store.PreflightAppend( orphan ).Ok() );

    SceneEntityCreateDesc child;
    child.sceneObjectId = PhysicsSceneObjectId { 43u };
    child.SetName( "tower_child" );
    child.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, PhysicsSceneObjectId { 42u }, 1 );
    REQUIRE( store.PreflightAppend( child ).Ok() );
    store.CommitAppend( child, PhysicsBodyHandle { 10u, 1u } );
    CHECK( store.At( 1 ).behaviorGroup.rootObjectId.value == 42u );
}

TEST_CASE( "SceneEntityStore: active capacity is enforced without growth" )
{
    static SceneEntityStore store( diagnostics );
    store.Clear();
    store.ConfigureCapacity( 1 );
    const uint64_t reservedBytes = store.CapacityBytes();

    SceneEntityCreateDesc first;
    first.sceneObjectId = PhysicsSceneObjectId { 1u };
    first.SetName( "first" );
    PhysicsBodyHandle body;
    body.index = 0u;
    body.generation = 1u;
    store.CommitAppend( first, body );
    CHECK( store.CapacityBytes() == reservedBytes );

    SceneEntityCreateDesc second;
    second.sceneObjectId = PhysicsSceneObjectId { 2u };
    CHECK_FALSE( store.PreflightAppend( second ).Ok() );
    CHECK( store.Count() == 1 );
    CHECK( store.Capacity() == 1 );
    CHECK( store.TrimToCount( 0 ) );
    CHECK( store.Count() == 0 );
    CHECK( store.CapacityBytes() == reservedBytes );
    CHECK( store.PreflightAppend( second ).Ok() );
}

TEST_CASE( "RenderInstanceStore: preflighted creation publishes every render row" )
{
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::Rendering;

    RenderInstanceStore renderStore;
    RenderInstancePresentationRecord presentation;
    presentation.material.baseColor[0] = 0.25f;
    presentation.shadowCasterStream = ShadowCasterStream::Pine;
    presentation.editorVisible = false;
    strcpy_s( presentation.displayName, "transaction_entity" );

    PhysicsBodyRecord body;
    body.handle = PhysicsBodyHandle { 7u, 1u };
    body.sceneObjectId = PhysicsSceneObjectId { 77u };
    PhysicsBodyHotState hotState;

    ColliderRecord collider;
    const SkullbonezCore::Math::CollisionDetection::BoundingSphere shape( 1.5f, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    collider.handle = PhysicsColliderHandle { 9u, 1u };
    collider.body = body.handle;
    collider.sceneObjectId = body.sceneObjectId;
    collider.shape = SkullbonezCore::Math::CollisionDetection::CollisionShapeReference( shape, 0u );
    collider.shapeKind = ColliderShapeKind::Sphere;
    collider.boundingRadius = 1.5f;

    REQUIRE( renderStore.CanAppendCreationRow( 0 ) );
    renderStore.CommitCreationRow( presentation, body, hotState, collider, 0 );

    CHECK( renderStore.PresentationCount() == 1 );
    CHECK( renderStore.Count() == 1 );
    CHECK( renderStore.HandleForModelIndex( 0 ).IsValid() );
    CHECK( renderStore.Records()[0].sceneObjectId == PhysicsSceneObjectId { 77u } );
    CHECK( renderStore.Records()[0].material.baseColor[0] == doctest::Approx( 0.25f ) );
    CHECK( renderStore.Records()[0].shadowCasterStream == ShadowCasterStream::Pine );
    CHECK_FALSE( renderStore.PresentationRecords()[0].editorVisible );
    CHECK_FALSE( renderStore.Records()[0].editorVisible );
    REQUIRE( renderStore.SetEditorVisible( 0, true ) );
    CHECK( renderStore.PresentationRecords()[0].editorVisible );
    CHECK( renderStore.Records()[0].editorVisible );
    CHECK_FALSE( renderStore.SetEditorVisible( 1, false ) );
    CHECK_FALSE( renderStore.CanAppendCreationRow( 0 ) );
}


TEST_CASE( "RenderInstanceStore: topology mismatch clears every draw row in all builds" )
{
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::Rendering;

    RenderInstanceStore renderStore;
    RenderInstancePresentationRecord presentation;
    PhysicsBodyRecord body;
    body.handle = PhysicsBodyHandle { 0u, 1u };
    body.sceneObjectId = PhysicsSceneObjectId { 501u };
    PhysicsBodyHotState hotState;
    ColliderRecord collider;
    const SkullbonezCore::Math::CollisionDetection::BoundingSphere shape( 1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    collider.handle = PhysicsColliderHandle { 0u, 1u };
    collider.body = body.handle;
    collider.sceneObjectId = body.sceneObjectId;
    collider.shape = SkullbonezCore::Math::CollisionDetection::CollisionShapeReference( shape, 0u );
    collider.shapeKind = ColliderShapeKind::Sphere;
    renderStore.CommitCreationRow( presentation, body, hotState, collider, 0 );

    PhysicsBodyStore emptyBodies;
    ColliderStore emptyColliders;
    renderStore.Refresh( emptyBodies, emptyColliders );

    CHECK( renderStore.PresentationCount() == 0 );
    CHECK( renderStore.Count() == 0 );
    CHECK( renderStore.Records().empty() );
    CHECK( renderStore.HasConsistentHandleMap() );
}


TEST_CASE( "SceneWorld render contract: swap-last reorder republishes the complete handle map" )
{
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::Rendering;

    RenderInstanceStore renderStore;
    const SkullbonezCore::Math::CollisionDetection::BoundingSphere shape(
        1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR );

    for ( int modelIndex = 0; modelIndex < 3; ++modelIndex )
    {
        RenderInstancePresentationRecord presentation;
        PhysicsBodyRecord body;
        body.handle = PhysicsBodyHandle { static_cast<uint32_t>( modelIndex ), 1u };
        body.sceneObjectId = PhysicsSceneObjectId { static_cast<uint32_t>( 700 + modelIndex ) };
        ColliderRecord collider;
        collider.handle = PhysicsColliderHandle { static_cast<uint32_t>( modelIndex ), 1u };
        collider.body = body.handle;
        collider.sceneObjectId = body.sceneObjectId;
        collider.shape = SkullbonezCore::Math::CollisionDetection::CollisionShapeReference(
            shape, static_cast<uint32_t>( modelIndex ) );
        collider.shapeKind = ColliderShapeKind::Sphere;
        renderStore.CommitCreationRow( presentation, body, PhysicsBodyHotState {}, collider, modelIndex );
    }

    REQUIRE( renderStore.HasConsistentHandleMap() );
    const RenderInstanceHandle retiredMiddle = renderStore.HandleForModelIndex( 1 );
    const RenderInstanceHandle retiredLast = renderStore.HandleForModelIndex( 2 );
    REQUIRE( renderStore.DestroyCreationRowAtSwapLast( 1 ) );
    REQUIRE( renderStore.Count() == 2 );
    CHECK( renderStore.HasConsistentHandleMap() );
    CHECK( renderStore.Contains( retiredMiddle ) );
    CHECK_FALSE( renderStore.Contains( retiredLast ) );
    CHECK( renderStore.Records()[1].sceneObjectId == PhysicsSceneObjectId { 702u } );
    CHECK( renderStore.ModelIndexForHandle( renderStore.HandleForModelIndex( 1 ) ) == 1 );
}


TEST_CASE( "RenderInstanceStore: fixed-tick poses interpolate and discontinuities collapse" )
{
    using namespace SkullbonezCore::Math::Orientation;
    using namespace SkullbonezCore::Math::Vector;
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::Rendering;

    static PhysicsBodyStore bodyStore;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodyStore.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    bodyStore.Clear();
    PhysicsBodyCreateRecord createRecord;
    createRecord.cold.sceneObjectId = PhysicsSceneObjectId { 901u };
    createRecord.hot.position = Vector3( 0.0f, 0.0f, 0.0f );
    const PhysicsBodyHandle bodyHandle = bodyStore.CreateBodyRecord( createRecord );
    REQUIRE( bodyHandle.IsValid() );

    static ColliderStore colliderStore;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        colliderStore.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        colliderStore.ReserveShapeCapacity( 1u, 0u, 0u );
    }
    colliderStore.Clear();
    ColliderRecord collider;
    const SkullbonezCore::Math::CollisionDetection::CollisionShape
        shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere( 1.0f, ZERO_VECTOR );
    collider.body = bodyHandle;
    collider.sceneObjectId = createRecord.cold.sceneObjectId;
    collider.shapeKind = ColliderShapeKind::Sphere;
    collider.boundingRadius = 1.0f;
    REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore,  collider, shape  ).IsValid() );

    RenderInstanceStore renderStore;
    RenderInstancePresentationRecord presentation;
    renderStore.CommitCreationRow( presentation, bodyStore.Records()[0],
                                   LoadPhysicsBodyHotState( bodyStore.HotFields(), 0u ), colliderStore.Records()[0], 0 );

    renderStore.BeginPhysicsStepPoseCapture( bodyStore );
    auto hotFields = bodyStore.MutableHotFields();
    hotFields.positionX[0] = 8.0f;
    renderStore.CompletePhysicsStepPoseCapture( bodyStore );

    Vector3 presentedPosition;
    Quaternion presentedOrientation;
    REQUIRE( renderStore.TryGetPresentationPose( 0, 0.25f, presentedPosition, presentedOrientation ) );
    CHECK( presentedPosition.x == doctest::Approx( 2.0f ) );

    // Concept: a teleport between ticks is a discontinuity. Refresh must publish it
    // exactly instead of blending from the previous physics endpoint.
    hotFields.positionX[0] = 100.0f;
    renderStore.Refresh( bodyStore, colliderStore, 0.25f );
    CHECK( renderStore.HasConsistentHandleMap() );
    REQUIRE( renderStore.TryGetPresentationPose( 0, 0.25f, presentedPosition, presentedOrientation ) );
    CHECK( presentedPosition.x == doctest::Approx( 100.0f ) );

    // Invariant: input can teleport a body immediately before a solver tick. The begin
    // capture detects that endpoint break, so the next legitimate tick blends
    // from the teleported pose instead of resurrecting the old path.
    hotFields.positionX[0] = 200.0f;
    renderStore.BeginPhysicsStepPoseCapture( bodyStore );
    hotFields.positionX[0] = 208.0f;
    renderStore.CompletePhysicsStepPoseCapture( bodyStore );
    REQUIRE( renderStore.TryGetPresentationPose( 0, 0.25f, presentedPosition, presentedOrientation ) );
    CHECK( presentedPosition.x == doctest::Approx( 202.0f ) );
}

TEST_CASE( "Quaternion shortest nlerp treats antipodal endpoints as one orientation" )
{
    using namespace SkullbonezCore::Math::Orientation;
    const Quaternion identity( 0.0f, 0.0f, 0.0f, 1.0f );
    const Quaternion antipodalIdentity( 0.0f, 0.0f, 0.0f, -1.0f );
    const Quaternion blended = NlerpShortest( identity, antipodalIdentity, 0.5f );
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    blended.GetComponents( x, y, z, w );
    CHECK( x == doctest::Approx( 0.0f ) );
    CHECK( y == doctest::Approx( 0.0f ) );
    CHECK( z == doctest::Approx( 0.0f ) );
    CHECK( w == doctest::Approx( 1.0f ) );
}

TEST_CASE( "RenderInstanceStore: fixed-contact feedback follows presentation rows and decays" )
{
    using namespace SkullbonezCore::Rendering;

    RenderInstanceStore renderStore;
    REQUIRE( renderStore.ResizePresentationRecords( 2 ) );

    renderStore.NotifyFixedContact( 0, 0.5f );
    renderStore.NotifyFixedContact( 1, 0.1f );
    renderStore.TickContactFeedback( 2, 0.05f );

    REQUIRE( renderStore.PresentationRecords().size() == 2u );
    CHECK( renderStore.PresentationRecords()[0].fixedContactAlpha == doctest::Approx( 0.9f ) );
    CHECK( renderStore.PresentationRecords()[1].fixedContactAlpha == doctest::Approx( 0.1f ) );

    renderStore.TickContactFeedback( 2, 1.0f );
    CHECK( renderStore.PresentationRecords()[0].fixedContactAlpha == doctest::Approx( 0.0f ) );
    CHECK( renderStore.PresentationRecords()[1].fixedContactAlpha == doctest::Approx( 0.0f ) );
}

TEST_CASE( "RenderInstanceStore: contact feedback survives swap-last deletion and refresh" )
{
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::Rendering;

    RenderInstanceStore renderStore;
    const auto commitRenderRow = [&]( int index, uint32_t sceneId )
    {
        RenderInstancePresentationRecord presentation;

        PhysicsBodyRecord body;
        body.handle = PhysicsBodyHandle { static_cast<uint32_t>( index ), 1u };
        body.sceneObjectId = PhysicsSceneObjectId { sceneId };
        PhysicsBodyHotState hotState;
        ColliderRecord collider;
        const SkullbonezCore::Math::CollisionDetection::BoundingSphere shape( 1.0f,
                                                                              SkullbonezCore::Math::Vector::ZERO_VECTOR );
        collider.handle = PhysicsColliderHandle { static_cast<uint32_t>( index ), 1u };
        collider.body = body.handle;
        collider.sceneObjectId = body.sceneObjectId;
        collider.shape = SkullbonezCore::Math::CollisionDetection::CollisionShapeReference( shape, 0u );
        collider.shapeKind = ColliderShapeKind::Sphere;
        collider.boundingRadius = 1.0f;
        renderStore.CommitCreationRow( presentation, body, hotState, collider, index );
    };

    commitRenderRow( 0, 101u );
    commitRenderRow( 1, 303u );
    commitRenderRow( 2, 202u );
    renderStore.NotifyFixedContact( 0, 0.5f );
    renderStore.NotifyFixedContact( 2, 0.1f );

    REQUIRE( renderStore.DestroyCreationRowAtSwapLast( 0 ) );
    REQUIRE( renderStore.Count() == 2 );
    REQUIRE( renderStore.PresentationCount() == 2 );
    CHECK( renderStore.Records()[0].sceneObjectId == PhysicsSceneObjectId { 202u } );
    CHECK( renderStore.PresentationRecords()[0].fixedContactAlpha == doctest::Approx( 0.2f ) );

    static PhysicsBodyStore bodyStore;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodyStore.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    bodyStore.Clear();
    PhysicsBodyCreateRecord createRecord;
    createRecord.cold.sceneObjectId = PhysicsSceneObjectId { 202u };
    const PhysicsBodyHandle bodyHandle = bodyStore.CreateBodyRecord( createRecord );
    static ColliderStore colliderStore;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        colliderStore.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        colliderStore.ReserveShapeCapacity( 1u, 0u, 0u );
    }
    colliderStore.Clear();
    ColliderRecord collider;
    const SkullbonezCore::Math::CollisionDetection::CollisionShape
        shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere( 1.0f, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    collider.body = bodyHandle;
    collider.sceneObjectId = createRecord.cold.sceneObjectId;
    collider.shapeKind = ColliderShapeKind::Sphere;
    collider.boundingRadius = 1.0f;
    REQUIRE( SkullbonezTests::ColliderStoreFixtures::CreateColliderRecord( colliderStore,  collider, shape  ).IsValid() );

    REQUIRE( renderStore.ResizePresentationRecords( 1 ) );
    REQUIRE( renderStore.PresentationCount() == 1 );
    renderStore.TickContactFeedback( 1, 0.05f );
    renderStore.Refresh( bodyStore, colliderStore );
    CHECK( renderStore.HasConsistentHandleMap() );
    REQUIRE( renderStore.Count() == 1 );
    CHECK( renderStore.Records()[0].sceneObjectId == PhysicsSceneObjectId { 202u } );
    CHECK( renderStore.Records()[0].fixedContactAlpha == doctest::Approx( 0.1f ) );
}
