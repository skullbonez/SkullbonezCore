/*
File: SkullbonezTests/TestSceneSnapshotWriter.cpp
Purpose:
  Verifies version-2 scene snapshots preserve every owner-published save field
  and asset-instance live part state.

Summary:
  The writer consumes SceneWorld, session, and presentation publications.
  Tests cover their persisted policy fields, then reparse asset-backed rows as
  authoritative shape-specific part states. The three runtime save policies
  are executed directly so no caller can regress to a partial publication.

Glossary:
  Live part state: Current body/collider values, independent of the original
    asset instance transform.
  Collider authoring row: Cold material text stored beside, not inside, the hot
    collider record.
  Stable root id: Scene object id shared by every part affiliation in one asset
    instance.
  Entry policy: Production operation that owns editor numbering, load-only
    output validation, or active editable-scene overwrite behavior.

Invariants:
  - Asset parts are emitted once in contiguous authored part order.
  - Direct entities remain in objects[] and retain explicit schema-v2 ids.
  - Reparse uses live state rather than recomposing the asset recipe transform.
  - Contact-material text survives save/reparse through the cold authoring row.
  - Every runtime save entry serializes all three owner publications.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp
  - SkullbonezSource/Scene/AuthoredSceneParser.cpp
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
*/
#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"

#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h"
#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/Runtime/Scene/SceneSessionState.h"
#include "../SkullbonezSource/Runtime/Scene/SceneSaveOperations.h"
#include "../SkullbonezSource/Scene/SceneSnapshotWriter.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using namespace SkullbonezCore;
using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace
{
constexpr const char* kLibraryPath = "TestOutput/scene_snapshot_writer.assets.json";
constexpr const char* kSnapshotPath = "TestOutput/scene_snapshot_writer.scene.json";
constexpr const char* kEditorSnapshotPath = "Scenes/snapshot_9100.scene.json";
constexpr const char* kLoadOnlySnapshotPath = "TestOutput/scene_snapshot_load_only.scene.json";
constexpr const char* kEditableSnapshotPath = "TestOutput/scene_snapshot_editable.scene.json";

struct TemporarySnapshotFiles
{
    ~TemporarySnapshotFiles()
    {
        std::error_code ignored;
        std::filesystem::remove( kSnapshotPath, ignored );
        std::filesystem::remove( kLibraryPath, ignored );
        std::filesystem::remove( kEditorSnapshotPath, ignored );
        std::filesystem::remove( kLoadOnlySnapshotPath, ignored );
        std::filesystem::remove( kEditableSnapshotPath, ignored );
    }
};

void WriteAssetLibrary()
{
    std::ofstream output( kLibraryPath, std::ios::binary | std::ios::trunc );
    REQUIRE( output.good() );
    output << R"({
  "format":"skullbonez.asset_library.json","version":1,
  "assets":[{"name":"mixed.live","type":"compound","parts":[
    {"name":"box","type":"box","halfExtents":[1,1,1],"mass":1,"restitution":0.1,"material":{"mode":"metal"}},
    {"name":"sphere","type":"sphere","radius":1,"mass":1,"restitution":0.1,"sleeping":true,"material":{"mode":"glass"}},
    {"name":"hull","type":"convexHull","hull":"pyramid","mass":1,"restitution":0.1,"material":{"mode":"matte"}}
  ]}]
})";
    REQUIRE( output.good() );
}

TEST_CASE( "Scene save owners publish every session and presentation field" )
{
    SceneSessionState session;
    session.isScenePhysics = false;
    session.isSceneText = false;
    session.isEditableScene = true;
    session.isFixedStep = true;
    session.hasFlatSlope = true;
    session.flatBaseY = 3.5f;
    session.flatSlopeX = -0.125f;
    session.flatSlopeZ = 0.375f;

    const SceneSessionSaveState sessionSave = session.GetSaveState();
    CHECK_FALSE( sessionSave.physicsOn );
    CHECK_FALSE( sessionSave.textOn );
    CHECK( sessionSave.editableScene );
    CHECK( sessionSave.fixedStep );
    CHECK( sessionSave.hasFlatSlope );
    CHECK( sessionSave.flatBaseY == doctest::Approx( 3.5f ) );
    CHECK( sessionSave.flatSlopeX == doctest::Approx( -0.125f ) );
    CHECK( sessionSave.flatSlopeZ == doctest::Approx( 0.375f ) );

    OverlayDebugState presentation;
    presentation.isWaterHidden = true;
    presentation.isTerrainHidden = true;
    const PresentationSaveState presentationSave = presentation.GetSaveState();
    CHECK( presentationSave.waterHidden );
    CHECK( presentationSave.terrainHidden );
}


void CheckCompleteOwnerPublication( const char* path, const SceneWorldSaveState& world, const SceneSessionSaveState& session,
                                    const PresentationSaveState& presentation )
{
    const AuthoredScene saved = AuthoredScene::LoadFromFile( diagnostics, path );
    CHECK( saved.IsPhysicsEnabled() == session.physicsOn );
    CHECK( saved.IsTextEnabled() == session.textOn );
    CHECK( saved.IsEditableScene() == session.editableScene );
    CHECK( saved.IsFixedStep() == session.fixedStep );
    CHECK( saved.IsWaterHidden() == presentation.waterHidden );
    CHECK( saved.IsTerrainHidden() == presentation.terrainHidden );
    CHECK( saved.HasFlatSlope() == session.hasFlatSlope );
    CHECK( saved.GetFlatBaseY() == doctest::Approx( session.flatBaseY ) );
    CHECK( saved.GetFlatSlopeX() == doctest::Approx( session.flatSlopeX ) );
    CHECK( saved.GetFlatSlopeZ() == doctest::Approx( session.flatSlopeZ ) );
    CHECK( saved.GetWorldGravity() == doctest::Approx( world.gravity ) );
    CHECK( saved.GetWorldFluidHeight() == doctest::Approx( world.fluidSurfaceHeight ) );
    CHECK( saved.GetWorldFluidDensity() == doctest::Approx( world.fluidDensity ) );

    const MutualGravitySettings& savedMutualGravity = saved.GetWorldMutualGravitySettings();
    CHECK( savedMutualGravity.enabled == world.mutualGravity.enabled );
    CHECK( savedMutualGravity.gravitationalConstant == doctest::Approx( world.mutualGravity.gravitationalConstant ) );
    CHECK( savedMutualGravity.softeningLength == doctest::Approx( world.mutualGravity.softeningLength ) );
    CHECK( savedMutualGravity.elasticCollisions == world.mutualGravity.elasticCollisions );

    REQUIRE( saved.GetCameraCount() == 1 );
    CHECK( saved.GetCamera( 0 ).m_position == world.cameraEye );
    CHECK( saved.GetCamera( 0 ).view == world.cameraView );
    CHECK( saved.GetCamera( 0 ).up == world.cameraUp );
}


TEST_CASE( "Scene save entry policies serialize complete owner publications" )
{
    TemporarySnapshotFiles cleanup;

    // These fixed-capacity stores are intentionally static: placing all three
    // owner arrays in one doctest frame exceeds the Windows test-thread stack.
    static SceneEntityStore entities( diagnostics );
    static PhysicsBodyStore bodies;
    static ColliderStore colliders;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        colliders.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        colliders.ReserveShapeCapacity( 16u, 16u, 16u );
    }

    entities.Clear();
    bodies.Clear();
    colliders.Clear();

    MutualGravitySettings mutualGravity;
    mutualGravity.enabled = true;
    mutualGravity.gravitationalConstant = 4.75f;
    mutualGravity.softeningLength = 0.625f;
    mutualGravity.elasticCollisions = false;
    const SceneWorldSaveState world { entities,
                                      bodies,
                                      colliders,
                                      nullptr,
                                      0,
                                      -7.25f,
                                      11.5f,
                                      875.0f,
                                      mutualGravity,
                                      Vector3( 2.0f, 3.0f, 4.0f ),
                                      Vector3( 5.0f, 6.0f, 7.0f ),
                                      Vector3( 0.0f, 0.0f, 1.0f ) };

    SceneSessionState sessionOwner;
    sessionOwner.isScenePhysics = true;
    sessionOwner.isSceneText = false;
    sessionOwner.isEditableScene = true;
    sessionOwner.isFixedStep = true;
    sessionOwner.hasFlatSlope = true;
    sessionOwner.flatBaseY = 8.5f;
    sessionOwner.flatSlopeX = 0.125f;
    sessionOwner.flatSlopeZ = -0.25f;
    const SceneSessionSaveState session = sessionOwner.GetSaveState();

    OverlayDebugState presentationOwner;
    presentationOwner.isWaterHidden = true;
    presentationOwner.isTerrainHidden = true;
    const PresentationSaveState presentation = presentationOwner.GetSaveState();

    SUBCASE( "editor hotkey policy selects a numbered path and writes every owner value" )
    {
        std::error_code ignored;

        std::filesystem::remove( kEditorSnapshotPath, ignored );
        int sequence = 9100;
        Core::SbResult saveResult = Core::SbResult::Success();
        REQUIRE( TrySaveNextEditorSceneSnapshot( diagnostics, sequence, world, session, presentation, saveResult ) );
        REQUIRE( saveResult.Ok() );
        CHECK( sequence == 9101 );
        CheckCompleteOwnerPublication( kEditorSnapshotPath, world, session, presentation );
    }

    SUBCASE( "scene-load-only policy writes every owner value to its explicit output" )
    {
        REQUIRE( SaveSceneLoadOnlySnapshot( diagnostics, kLoadOnlySnapshotPath, world, session, presentation ).Ok() );

        CheckCompleteOwnerPublication( kLoadOnlySnapshotPath, world, session, presentation );
    }

    SUBCASE( "editable replacement policy overwrites the active scene with every owner value" )
    {
        REQUIRE( SaveEditableSceneBeforeReplacement( diagnostics, kEditableSnapshotPath, world, session, presentation ).Ok() );

        CheckCompleteOwnerPublication( kEditableSnapshotPath, world, session, presentation );
    }
}

void AppendEntity( SceneEntityStore& entities, PhysicsBodyStore& bodies, ColliderStore& colliders, uint32_t id,
                   const char* displayName, const CollisionShape& shape, const Vector3& position, const Vector3& velocity,
                   const Vector3& angularVelocity, const Vector3& inertia, float mass, float restitution,
                   const char* contactMaterial, bool fixed, bool sleeping, const char* assetPart, uint32_t assetPartIndex,
                   SceneBehaviorGroupKind behaviorKind = SceneBehaviorGroupKind::None,
                   PhysicsSceneObjectId behaviorRoot = {}, int behaviorPartIndex = -1 )
{

    // Invariant: handles are assigned by the stores, while the stable scene id
    // is copied into all three owner rows before the entity becomes visible.
    PhysicsBodyCreateRecord body;
    body.cold.sceneObjectId = PhysicsSceneObjectId { id };
    body.hot.position = position;
    body.hot.orientation = Quaternion( 0.11f, -0.22f, 0.33f, 0.91f );
    body.hot.orientation.Normalise();
    body.hot.linearVelocity = velocity;
    body.hot.angularVelocity = angularVelocity;
    body.cold.rotationalInertia = inertia;
    body.cold.mass = mass;
    body.hot.inverseMass = fixed ? 0.0f : 1.0f / mass;
    body.hot.fixed = fixed;
    body.hot.awake = !sleeping;
    body.cold.releasesFromFixedOnContact = assetPartIndex == 2;
    body.cold.contactReleaseImpulseThreshold = 4.25f;
    const PhysicsBodyHandle bodyHandle = bodies.CreateBodyRecord( body );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.sceneObjectId = body.cold.sceneObjectId;
    collider.restitution = restitution;
    ColliderAuthoringRecord colliderAuthoring;
    strncpy_s( colliderAuthoring.contactMaterialName, contactMaterial, _TRUNCATE );
    (void)colliders.CreateColliderRecord( collider, shape, colliderAuthoring );

    SceneEntityCreateDesc entity;
    entity.sceneObjectId = body.cold.sceneObjectId;
    entity.SetName( displayName );
    entity.SetRenderTint( 0.1f * static_cast<float>( assetPartIndex + 1u ), 0.4f, 0.7f, 1.0f );
    Rendering::RenderMaterial completeMaterial = entity.GetRenderMaterial();
    completeMaterial.baseColor[3] = 0.55f + 0.05f * static_cast<float>( assetPartIndex );
    completeMaterial.emissiveColor[0] = 0.05f;
    completeMaterial.emissiveColor[1] = 0.10f;
    completeMaterial.emissiveColor[2] = 0.15f;
    completeMaterial.emissiveStrength = 0.20f;
    completeMaterial.roughness = 0.25f;
    completeMaterial.metallic = 0.30f;
    completeMaterial.specular = 0.35f;
    completeMaterial.transmission = 0.40f;
    completeMaterial.stylization = 0.45f;
    completeMaterial.flags = 10u + assetPartIndex;
    entity.SetRenderMaterial( completeMaterial );

    if ( assetPart )
    {
        entity.SetAssetAffiliation( PhysicsSceneObjectId { 300u }, kLibraryPath, "mixed.live", "saved_asset", assetPart,
                                    assetPartIndex );
    }

    if ( behaviorKind != SceneBehaviorGroupKind::None )
    {
        entity.SetBehaviorGroup( behaviorKind, behaviorRoot, behaviorPartIndex );
    }

    entities.CommitAppend( entity, bodyHandle );
}

void CheckVector( const Vector3& actual, const Vector3& expected )
{
    CHECK( actual.x == doctest::Approx( expected.x ) );
    CHECK( actual.y == doctest::Approx( expected.y ) );
    CHECK( actual.z == doctest::Approx( expected.z ) );
}

void CheckMaterial( const Rendering::RenderMaterial& actual, const Rendering::RenderMaterial& expected )
{

    // Contact flash is frame feedback, not durable render intent. Every other
    // material field serialized by SceneSnapshotWriter is compared here.
    CHECK( std::string( actual.name ) == expected.name );
    CHECK( actual.kind == expected.kind );

    for ( int i = 0; i < 4; ++i )
    {
        CHECK( actual.baseColor[i] == doctest::Approx( expected.baseColor[i] ) );
    }

    for ( int i = 0; i < 3; ++i )
    {
        CHECK( actual.emissiveColor[i] == doctest::Approx( expected.emissiveColor[i] ) );
    }

    CHECK( actual.emissiveStrength == doctest::Approx( expected.emissiveStrength ) );
    CHECK( actual.roughness == doctest::Approx( expected.roughness ) );
    CHECK( actual.metallic == doctest::Approx( expected.metallic ) );
    CHECK( actual.specular == doctest::Approx( expected.specular ) );
    CHECK( actual.transmission == doctest::Approx( expected.transmission ) );
    CHECK( actual.stylization == doctest::Approx( expected.stylization ) );
    CHECK( actual.textureMode == doctest::Approx( expected.textureMode ) );
    CHECK( actual.flags == expected.flags );
}

void CheckShape( const CollisionShapeReference& actual, const CollisionShapeReference& expected )
{
    const BoundingSphere* actualSphere = GetShapeIf<BoundingSphere>( &actual );
    const BoundingSphere* expectedSphere = GetShapeIf<BoundingSphere>( &expected );
    REQUIRE( ( actualSphere != nullptr ) == ( expectedSphere != nullptr ) );

    if ( actualSphere )
    {
        CHECK( actualSphere->GetRadius() == doctest::Approx( expectedSphere->GetRadius() ) );
        return;
    }

    const BoundingBox* actualBox = GetShapeIf<BoundingBox>( &actual );
    const BoundingBox* expectedBox = GetShapeIf<BoundingBox>( &expected );
    REQUIRE( ( actualBox != nullptr ) == ( expectedBox != nullptr ) );

    if ( actualBox )
    {
        CheckVector( actualBox->GetHalfExtents(), expectedBox->GetHalfExtents() );
        return;
    }

    const ConvexHullShape* actualHull = GetShapeIf<ConvexHullShape>( &actual );
    const ConvexHullShape* expectedHull = GetShapeIf<ConvexHullShape>( &expected );
    REQUIRE( actualHull );
    REQUIRE( expectedHull );
    CHECK( std::string( actualHull->GetName() ) == expectedHull->GetName() );
    REQUIRE( actualHull->GetVertexCount() == expectedHull->GetVertexCount() );
    CHECK( actualHull->GetFaceCount() == expectedHull->GetFaceCount() );
    CHECK( actualHull->GetEdgeCount() == expectedHull->GetEdgeCount() );

    for ( uint16_t index = 0; index < actualHull->GetVertexCount(); ++index )
    {
        CheckVector( actualHull->GetVertex( index ), expectedHull->GetVertex( index ) );
    }
}

void CheckOrientation( const Quaternion& actual, const Quaternion& expected )
{
    float actualX = 0.0f, actualY = 0.0f, actualZ = 0.0f, actualW = 1.0f;
    float expectedX = 0.0f, expectedY = 0.0f, expectedZ = 0.0f, expectedW = 1.0f;
    actual.GetComponents( actualX, actualY, actualZ, actualW );
    expected.GetComponents( expectedX, expectedY, expectedZ, expectedW );
    const float sign = actualX * expectedX + actualY * expectedY + actualZ * expectedZ + actualW * expectedW < 0.0f ? -1.0f
                                                                                                                    : 1.0f;

    CHECK( actualX == doctest::Approx( sign * expectedX ) );
    CHECK( actualY == doctest::Approx( sign * expectedY ) );
    CHECK( actualZ == doctest::Approx( sign * expectedZ ) );
    CHECK( actualW == doctest::Approx( sign * expectedW ) );
}

void CheckRecreatedOwners( const SceneEntityStore& sourceEntities, const PhysicsBodyStore& sourceBodies,
                           const ColliderStore& sourceColliders, const SceneEntityStore& recreatedEntities,
                           const PhysicsBodyStore& recreatedBodies, const ColliderStore& recreatedColliders )
{
    REQUIRE( recreatedEntities.Count() == sourceEntities.Count() );

    for ( int sourceIndex = 0; sourceIndex < sourceEntities.Count(); ++sourceIndex )
    {
        const SceneEntityRecord& sourceEntity = sourceEntities.At( sourceIndex );
        const int recreatedIndex = recreatedEntities.FindBySceneObjectId( sourceEntity.sceneObjectId );
        REQUIRE( recreatedIndex >= 0 );
        const SceneEntityRecord& recreatedEntity = recreatedEntities.At( recreatedIndex );
        CHECK( std::string( recreatedEntity.displayName ) == sourceEntity.displayName );
        CHECK( recreatedEntity.asset.isAssetBacked == sourceEntity.asset.isAssetBacked );
        CHECK( recreatedEntity.asset.rootObjectId.value == sourceEntity.asset.rootObjectId.value );
        CHECK( std::string( recreatedEntity.asset.libraryToken ) == sourceEntity.asset.libraryToken );
        CHECK( std::string( recreatedEntity.asset.assetName ) == sourceEntity.asset.assetName );
        CHECK( std::string( recreatedEntity.asset.instanceName ) == sourceEntity.asset.instanceName );
        CHECK( std::string( recreatedEntity.asset.partName ) == sourceEntity.asset.partName );
        CHECK( recreatedEntity.asset.partIndex == sourceEntity.asset.partIndex );
        CHECK( recreatedEntity.behaviorGroup.kind == sourceEntity.behaviorGroup.kind );
        CHECK( recreatedEntity.behaviorGroup.rootObjectId.value == sourceEntity.behaviorGroup.rootObjectId.value );
        CHECK( recreatedEntity.behaviorGroup.partIndex == sourceEntity.behaviorGroup.partIndex );
        CheckMaterial( recreatedEntity.renderMaterial, sourceEntity.renderMaterial );

        const PhysicsBodyRecord* sourceBody = sourceBodies.RecordForHandle( sourceEntity.body );
        const PhysicsBodyRecord* recreatedBody = recreatedBodies.RecordForHandle( recreatedEntity.body );
        REQUIRE( sourceBody );
        REQUIRE( recreatedBody );
        const int sourceBodyIndex = sourceBodies.ModelIndexForHandle( sourceEntity.body );
        const int recreatedBodyIndex = recreatedBodies.ModelIndexForHandle( recreatedEntity.body );
        REQUIRE( sourceBodyIndex >= 0 );
        REQUIRE( recreatedBodyIndex >= 0 );
        const PhysicsBodyHotState sourceHot = LoadPhysicsBodyHotState( sourceBodies.HotFields(),
                                                                       static_cast<std::size_t>( sourceBodyIndex ) );

        const PhysicsBodyHotState recreatedHot = LoadPhysicsBodyHotState( recreatedBodies.HotFields(),
                                                                          static_cast<std::size_t>( recreatedBodyIndex ) );

        CheckVector( recreatedHot.position, sourceHot.position );
        CheckOrientation( recreatedHot.orientation, sourceHot.orientation );
        CheckVector( recreatedHot.linearVelocity, sourceHot.linearVelocity );
        CheckVector( recreatedHot.angularVelocity, sourceHot.angularVelocity );
        CheckVector( recreatedBody->rotationalInertia, sourceBody->rotationalInertia );
        CHECK( recreatedBody->mass == doctest::Approx( sourceBody->mass ) );
        CHECK( recreatedHot.fixed == sourceHot.fixed );
        CHECK( recreatedHot.awake == sourceHot.awake );
        CHECK( recreatedBody->releasesFromFixedOnContact == sourceBody->releasesFromFixedOnContact );

        if ( sourceBody->releasesFromFixedOnContact )
        {
            CHECK( recreatedBody->contactReleaseImpulseThreshold ==
                   doctest::Approx( sourceBody->contactReleaseImpulseThreshold ) );
        }

        const PhysicsColliderHandle sourceColliderHandle = sourceColliders.HandleForSceneObjectId( sourceEntity.sceneObjectId );
        const PhysicsColliderHandle recreatedColliderHandle = recreatedColliders.HandleForSceneObjectId( recreatedEntity.sceneObjectId );
        const ColliderRecord* sourceCollider = sourceColliders.RecordForHandle( sourceColliderHandle );
        const ColliderRecord* recreatedCollider = recreatedColliders.RecordForHandle( recreatedColliderHandle );
        const ColliderAuthoringRecord* sourceColliderAuthoring = sourceColliders.AuthoringRecordForHandle( sourceColliderHandle );
        const ColliderAuthoringRecord* recreatedColliderAuthoring = recreatedColliders.AuthoringRecordForHandle( recreatedColliderHandle );
        REQUIRE( sourceCollider );
        REQUIRE( recreatedCollider );
        REQUIRE( sourceColliderAuthoring );
        REQUIRE( recreatedColliderAuthoring );
        CheckShape( recreatedCollider->shape, sourceCollider->shape );
        CHECK( recreatedCollider->restitution == doctest::Approx( sourceCollider->restitution ) );
        CHECK( std::string( recreatedColliderAuthoring->contactMaterialName ) ==
               sourceColliderAuthoring->contactMaterialName );
    }
}

void ApplyParsedAffiliation( SceneEntityCreateDesc& entity, const AuthoredScene& scene, SceneAssetPartSource source,
                             uint32_t sourceIndex )
{

    for ( int partIndex = 0; partIndex < scene.GetAssetPartCount(); ++partIndex )
    {
        const SceneAssetPartRef& part = scene.GetAssetPart( partIndex );

        if ( part.source != source || part.sourceIndex != sourceIndex )
        {
            continue;
        }

        for ( int instanceIndex = 0; instanceIndex < scene.GetAssetInstanceCount(); ++instanceIndex )
        {
            const SceneAssetInstanceRecord& instance = scene.GetAssetInstance( instanceIndex );

            if ( static_cast<uint32_t>( partIndex ) < instance.firstPart ||
                 static_cast<uint32_t>( partIndex ) >= instance.firstPart + instance.partCount )
            {
                continue;
            }

            entity.SetAssetAffiliation( instance.rootSceneObjectId,
                                        scene.GetAssetLibrary( static_cast<int>( instance.libraryRefIndex ) ).token,
                                        instance.assetName, instance.instanceName, part.partName, part.partIndex );

            return;
        }
    }
}

void ApplyParsedMaterial( SceneEntityCreateDesc& entity, const AuthoredScene& scene, const char* displayName )
{

    for ( int index = 0; index < scene.GetObjectMaterialOverrideCount(); ++index )
    {
        const SceneObjectMaterialOverride& material = scene.GetObjectMaterialOverride( index );

        if ( std::strcmp( material.target, displayName ) == 0 )
        {
            entity.SetRenderMaterial( material.material );
        }
    }
}

void AppendParsedEntity( SceneEntityStore& entities, PhysicsBodyStore& bodies, ColliderStore& colliders,
                         const AuthoredScene& scene, SceneAssetPartSource source, uint32_t sourceIndex,
                         PhysicsSceneObjectId id, const char* displayName, const CollisionShape& shape,
                         const Vector3& position, const Quaternion& orientation, const Vector3& velocity,
                         const Vector3& angularVelocity, const Vector3& inertia, float mass, float restitution,
                         const char* contactMaterial, bool fixed, bool sleeping, bool releasesOnContact,
                         float releaseThreshold, const SceneObjectGroupMetadata* group )
{
    PhysicsBodyCreateRecord body;
    body.cold.sceneObjectId = id;
    body.hot.position = position;
    body.hot.orientation = orientation;
    body.hot.linearVelocity = velocity;
    body.hot.angularVelocity = angularVelocity;
    body.cold.rotationalInertia = inertia;
    body.cold.mass = mass;
    body.hot.inverseMass = fixed ? 0.0f : 1.0f / mass;
    body.hot.fixed = fixed;
    body.hot.awake = !sleeping;
    body.cold.releasesFromFixedOnContact = releasesOnContact;
    body.cold.contactReleaseImpulseThreshold = releaseThreshold;
    const PhysicsBodyHandle bodyHandle = bodies.CreateBodyRecord( body );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.sceneObjectId = id;
    collider.restitution = restitution;
    ColliderAuthoringRecord colliderAuthoring;
    strncpy_s( colliderAuthoring.contactMaterialName, contactMaterial, _TRUNCATE );
    (void)colliders.CreateColliderRecord( collider, shape, colliderAuthoring );

    SceneEntityCreateDesc entity;
    entity.sceneObjectId = id;
    entity.SetName( displayName );
    ApplyParsedAffiliation( entity, scene, source, sourceIndex );
    ApplyParsedMaterial( entity, scene, displayName );

    if ( group && group->kind == SceneObjectGroupKind::ReleasableTree )
    {
        entity.SetBehaviorGroup( SceneBehaviorGroupKind::ReleasableTree, group->rootObjectId, group->partIndex );
    }

    REQUIRE( entities.PreflightAppend( entity ).Ok() );
    entities.CommitAppend( entity, bodyHandle );
}

void RecreateParsedOwners( const AuthoredScene& scene, SceneEntityStore& entities, PhysicsBodyStore& bodies,
                           ColliderStore& colliders )
{
    entities.Clear();
    entities.ConfigureCapacity( scene.GetBallStateCount() + scene.GetBoxStateCount() + scene.GetConvexHullStateCount() );
    bodies.Clear();
    colliders.Clear();

    for ( int index = 0; index < scene.GetBallStateCount(); ++index )
    {
        const SceneBallState& state = scene.GetBallState( index );
        AppendParsedEntity( entities, bodies, colliders, scene, SceneAssetPartSource::BallState,
                            static_cast<uint32_t>( index ), state.sceneObjectId, state.name,
                            BoundingSphere( state.radius, ZERO_VECTOR ), Vector3( state.posX, state.posY, state.posZ ),
                            Quaternion( state.orientX, state.orientY, state.orientZ, state.orientW ),
                            Vector3( state.velX, state.velY, state.velZ ),
                            Vector3( state.angVelX, state.angVelY, state.angVelZ ),
                            Vector3( state.inertiaX, state.inertiaY, state.inertiaZ ), state.mass, state.restitution,
                            state.contactMaterial, state.isFixed, state.isSleeping, false, 1.0f, nullptr );
    }

    for ( int index = 0; index < scene.GetBoxStateCount(); ++index )
    {
        const SceneBoxState& state = scene.GetBoxState( index );
        AppendParsedEntity( entities, bodies, colliders, scene, SceneAssetPartSource::BoxState,
                            static_cast<uint32_t>( index ), state.sceneObjectId, state.name,
                            BoundingBox( Vector3( state.halfX, state.halfY, state.halfZ ), ZERO_VECTOR ),
                            Vector3( state.posX, state.posY, state.posZ ),
                            Quaternion( state.orientX, state.orientY, state.orientZ, state.orientW ),
                            Vector3( state.velX, state.velY, state.velZ ),
                            Vector3( state.angVelX, state.angVelY, state.angVelZ ),
                            Vector3( state.inertiaX, state.inertiaY, state.inertiaZ ), state.mass, state.restitution,
                            state.contactMaterial, state.isFixed, state.isSleeping, false, 1.0f, nullptr );
    }

    for ( int index = 0; index < scene.GetConvexHullStateCount(); ++index )
    {
        const SceneConvexHullState& state = scene.GetConvexHullState( index );
        ConvexHullShape hull;
        const std::string hullPath = std::string( "SkullbonezData/hulls/" ) + state.hullPath + ".hull";
        REQUIRE( ConvexHullShape::TryLoadFromFile( diagnostics, hullPath.c_str(), hull ).Ok() );
        AppendParsedEntity( entities, bodies, colliders, scene, SceneAssetPartSource::ConvexHullState,
                            static_cast<uint32_t>( index ), state.sceneObjectId, state.name, hull,
                            Vector3( state.posX, state.posY, state.posZ ),
                            Quaternion( state.orientX, state.orientY, state.orientZ, state.orientW ),
                            Vector3( state.velX, state.velY, state.velZ ),
                            Vector3( state.angVelX, state.angVelY, state.angVelZ ),
                            Vector3( state.inertiaX, state.inertiaY, state.inertiaZ ), state.mass, state.restitution,
                            state.contactMaterial, state.isFixed, state.isSleeping, state.contactReleaseOnImpact,
                            state.contactReleaseImpulseThreshold, &state.group );
    }
}
} // namespace

TEST_CASE( "SceneSnapshotWriter: schema-v2 asset parts reparse from authoritative live state" )
{
    const TemporarySnapshotFiles cleanup;
    WriteAssetLibrary();

    static SceneEntityStore entities( diagnostics );
    static PhysicsBodyStore bodies;
    static ColliderStore colliders;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        bodies.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        colliders.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        colliders.ReserveShapeCapacity( 16u, 16u, 16u );
    }

    entities.Clear();
    entities.ConfigureCapacity( 6 );
    bodies.Clear();
    colliders.Clear();

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        AppendEntity( entities, bodies, colliders, 300u, "saved_box",
                      BoundingBox( Vector3( 2.0f, 3.0f, 4.0f ), ZERO_VECTOR ), Vector3( 10.0f, 11.0f, 12.0f ),
                      Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 4.0f, 5.0f, 6.0f ), Vector3( 7.0f, 8.0f, 9.0f ), 12.0f, 0.25f,
                      "wood", false, true, "box", 0u );

        AppendEntity( entities, bodies, colliders, 42u, "saved_sphere", BoundingSphere( 2.5f, ZERO_VECTOR ),
                      Vector3( 20.0f, 21.0f, 22.0f ), Vector3( 2.0f, 3.0f, 4.0f ), Vector3( 5.0f, 6.0f, 7.0f ),
                      Vector3( 8.0f, 9.0f, 10.0f ), 13.0f, 0.35f, "stone", true, false, "sphere", 1u );

        ConvexHullShape hull;
        REQUIRE( ConvexHullShape::TryLoadFromFile( diagnostics, "SkullbonezData/hulls/pyramid.hull", hull ).Ok() );
        AppendEntity( entities, bodies, colliders, 777u, "saved_hull", hull, Vector3( 30.0f, 31.0f, 32.0f ),
                      Vector3( 3.0f, 4.0f, 5.0f ), Vector3( 6.0f, 7.0f, 8.0f ), Vector3( 9.0f, 10.0f, 11.0f ), 14.0f, 0.45f,
                      "metal", true, false, "hull", 2u, SceneBehaviorGroupKind::ReleasableTree,
                      PhysicsSceneObjectId { 777u }, 0 );

        AppendEntity( entities, bodies, colliders, 99u, "direct_sphere", BoundingSphere( 0.75f, ZERO_VECTOR ),
                      Vector3( 40.0f, 41.0f, 42.0f ), ZERO_VECTOR, ZERO_VECTOR, Vector3( 1.0f, 1.0f, 1.0f ), 2.0f, 0.15f,
                      "default", false, false, nullptr, 0u );

        AppendEntity( entities, bodies, colliders, 1001u, "tree_root", hull, Vector3( 50.0f, 51.0f, 52.0f ), ZERO_VECTOR,
                      ZERO_VECTOR, Vector3( 3.0f, 4.0f, 5.0f ), 5.0f, 0.2f, "wood", true, false, nullptr, 0u,
                      SceneBehaviorGroupKind::ReleasableTree, PhysicsSceneObjectId { 1001u }, 0 );

        AppendEntity( entities, bodies, colliders, 555u, "tree_child", hull, Vector3( 53.0f, 54.0f, 55.0f ), ZERO_VECTOR,
                      ZERO_VECTOR, Vector3( 4.0f, 5.0f, 6.0f ), 6.0f, 0.3f, "wood", true, false, nullptr, 0u,
                      SceneBehaviorGroupKind::ReleasableTree, PhysicsSceneObjectId { 1001u }, 1 );
    }

    MutualGravitySettings mutualGravity;
    mutualGravity.enabled = true;
    mutualGravity.gravitationalConstant = 6.25f;
    mutualGravity.softeningLength = 0.75f;
    mutualGravity.elasticCollisions = false;
    const SceneWorldSaveState world { entities,
                                      bodies,
                                      colliders,
                                      nullptr,
                                      0,
                                      -9.81f,
                                      5.0f,
                                      1000.0f,
                                      mutualGravity,
                                      Vector3( 1.0f, 2.0f, 3.0f ),
                                      Vector3( 4.0f, 5.0f, 6.0f ),
                                      Vector3( 0.0f, 1.0f, 0.0f ) };

    const SceneSessionSaveState session { true, false, true, true, true, 7.5f, 0.25f, -0.5f };
    const PresentationSaveState presentation { true, true };
    const SceneSaveRequest request { kSnapshotPath, world, session, presentation };
    REQUIRE( SceneSnapshotWriter::Save( diagnostics, request ).Ok() );

    const AuthoredScene saved = AuthoredScene::LoadFromFile( diagnostics, kSnapshotPath );
    CHECK( saved.GetSchemaVersion() == 2u );
    CHECK( saved.IsPhysicsEnabled() );
    CHECK_FALSE( saved.IsTextEnabled() );
    CHECK( saved.IsEditableScene() );
    CHECK( saved.IsFixedStep() );
    CHECK( saved.IsWaterHidden() );
    CHECK( saved.IsTerrainHidden() );
    CHECK( saved.HasFlatSlope() );
    CHECK( saved.GetFlatBaseY() == doctest::Approx( 7.5f ) );
    CHECK( saved.GetFlatSlopeX() == doctest::Approx( 0.25f ) );
    CHECK( saved.GetFlatSlopeZ() == doctest::Approx( -0.5f ) );
    CHECK( saved.GetWorldGravity() == doctest::Approx( -9.81f ) );
    CHECK( saved.GetWorldFluidHeight() == doctest::Approx( 5.0f ) );
    CHECK( saved.GetWorldFluidDensity() == doctest::Approx( 1000.0f ) );
    REQUIRE( saved.GetCameraCount() == 1 );
    CHECK( saved.GetCamera( 0 ).m_position == Vector3( 1.0f, 2.0f, 3.0f ) );
    CHECK( saved.GetCamera( 0 ).view == Vector3( 4.0f, 5.0f, 6.0f ) );
    CHECK( saved.GetCamera( 0 ).up == Vector3( 0.0f, 1.0f, 0.0f ) );
    const MutualGravitySettings& savedMutualGravity = saved.GetWorldMutualGravitySettings();
    CHECK( savedMutualGravity.enabled );
    CHECK( savedMutualGravity.gravitationalConstant == doctest::Approx( 6.25f ) );
    CHECK( savedMutualGravity.softeningLength == doctest::Approx( 0.75f ) );
    CHECK_FALSE( savedMutualGravity.elasticCollisions );
    CHECK( saved.GetAssetLibraryCount() == 1 );
    CHECK( saved.GetAssetInstanceCount() == 1 );
    CHECK( saved.GetAssetPartCount() == 3 );
    CHECK( saved.GetBoxStateCount() == 1 );
    CHECK( saved.GetBallStateCount() == 2 );
    CHECK( saved.GetConvexHullStateCount() == 3 );
    CHECK( saved.GetBoxState( 0 ).sceneObjectId.value == 300u );
    CHECK( saved.GetBoxState( 0 ).posX == doctest::Approx( 10.0f ) );
    CHECK( saved.GetBoxState( 0 ).halfX == doctest::Approx( 2.0f ) );
    CHECK( saved.GetBoxState( 0 ).mass == doctest::Approx( 12.0f ) );
    CHECK( saved.GetBoxState( 0 ).restitution == doctest::Approx( 0.25f ) );
    CHECK( std::string( saved.GetBoxState( 0 ).contactMaterial ) == "wood" );
    CHECK( saved.GetBoxState( 0 ).isSleeping );
    CHECK( saved.GetBallState( 1 ).sceneObjectId.value == 42u );
    CHECK( saved.GetBallState( 1 ).radius == doctest::Approx( 2.5f ) );
    CHECK_FALSE( saved.GetBallState( 1 ).isSleeping );
    CHECK( saved.GetConvexHullState( 2 ).sceneObjectId.value == 777u );
    CHECK( saved.GetConvexHullState( 2 ).contactReleaseOnImpact );
    CHECK( saved.GetConvexHullState( 2 ).contactReleaseImpulseThreshold == doctest::Approx( 4.25f ) );
    CHECK( saved.GetConvexHullState( 2 ).group.rootObjectId.value == 777u );
    CHECK( saved.GetConvexHullState( 0 ).group.rootObjectId.value == 1001u );
    CHECK( saved.GetConvexHullState( 0 ).group.partIndex == 0 );
    CHECK( saved.GetConvexHullState( 1 ).group.rootObjectId.value == 1001u );
    CHECK( saved.GetConvexHullState( 1 ).group.partIndex == 1 );
    CHECK( saved.GetBallState( 0 ).sceneObjectId.value == 99u );
    CHECK( saved.GetAssetPart( 0 ).source == SceneAssetPartSource::BoxState );
    CHECK( saved.GetAssetPart( 1 ).source == SceneAssetPartSource::BallState );
    CHECK( saved.GetAssetPart( 2 ).source == SceneAssetPartSource::ConvexHullState );

    std::ifstream snapshot( kSnapshotPath, std::ios::binary );
    std::ostringstream contents;
    contents << snapshot.rdbuf();
    CHECK( contents.str().find( "\"assetInstances\"" ) != std::string::npos );
    CHECK( contents.str().find( "\"objectName\": \"saved_hull\"" ) != std::string::npos );

    // The second half rebuilds fresh production owner stores without Run.
    // Stable ids deliberately permit the parser's shape-section row reorder.
    static SceneEntityStore recreatedEntities( diagnostics );
    static PhysicsBodyStore recreatedBodies;
    static ColliderStore recreatedColliders;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        recreatedBodies.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        recreatedColliders.ReserveCapacity( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
        recreatedColliders.ReserveShapeCapacity( 16u, 16u, 16u );
        RecreateParsedOwners( saved, recreatedEntities, recreatedBodies, recreatedColliders );
    }
    CheckRecreatedOwners( entities, bodies, colliders, recreatedEntities, recreatedBodies, recreatedColliders );
}
