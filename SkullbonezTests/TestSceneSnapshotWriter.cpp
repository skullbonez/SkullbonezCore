/*
File: SkullbonezTests/TestSceneSnapshotWriter.cpp
Purpose:
  Verifies version-2 scene snapshots preserve asset-instance live part state.

Mental model:
  The writer borrows scene/entity, body, collider, group, and joint owner data.
  Asset-backed rows are grouped by stable asset-root id and serialized as
  authoritative part states; the parser then rebuilds shape-specific rows.

Glossary:
  Live part state: Current body/collider values, independent of the original
    asset instance transform.
  Stable root id: Scene object id shared by every part affiliation in one asset
    instance.

Invariants:
  - Asset parts are emitted once in contiguous authored part order.
  - Direct entities remain in objects[] and retain explicit schema-v2 ids.
  - Reparse uses live state rather than recomposing the asset recipe transform.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - SkullbonezSource/Scene/TestSceneParser.cpp
  - Agentic/Plans/TODO/physics-authority-and-identity.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/GameObjects/GameModelCollection.h"
#include "../SkullbonezSource/Physics/BoundingBox.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/ConvexHullShape.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Runtime/Scene/SceneEntityStore.h"
#include "../SkullbonezSource/Scene/SceneSnapshotWriter.h"
#include "../SkullbonezSource/Scene/TestScene.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace SkullbonezCore;
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;

namespace
{
constexpr const char* kLibraryPath = "TestOutput/scene_snapshot_writer.assets.json";
constexpr const char* kSnapshotPath = "TestOutput/scene_snapshot_writer.scene.json";

struct TemporarySnapshotFiles
{
    ~TemporarySnapshotFiles()
    {
        std::error_code ignored;
        std::filesystem::remove( kSnapshotPath, ignored );
        std::filesystem::remove( kLibraryPath, ignored );
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

void AppendEntity( SceneEntityStore& entities,
                   PhysicsBodyStore& bodies,
                   ColliderStore& colliders,
                   uint32_t id,
                   const char* displayName,
                   const CollisionShape& shape,
                   const Vector3& position,
                   const Vector3& velocity,
                   const Vector3& angularVelocity,
                   const Vector3& inertia,
                   float mass,
                   float restitution,
                   const char* contactMaterial,
                   bool fixed,
                   bool sleeping,
                   const char* assetPart,
                   uint32_t assetPartIndex )
{
    // Invariant: handles are assigned by the stores, while the stable scene id
    // is copied into all three owner rows before the entity becomes visible.
    PhysicsBodyRecord body;
    body.sceneObjectId = PhysicsSceneObjectId{ id };
    body.replayBodyId = id;
    body.position = position;
    body.orientation = Quaternion( 0.11f, -0.22f, 0.33f, 0.91f );
    body.orientation.Normalise();
    body.linearVelocity = velocity;
    body.angularVelocity = angularVelocity;
    body.rotationalInertia = inertia;
    body.mass = mass;
    body.invMass = fixed ? 0.0f : 1.0f / mass;
    body.isFixed = fixed;
    body.isSleeping = sleeping;
    body.releasesFromFixedOnContact = assetPartIndex == 2;
    body.contactReleaseImpulseThreshold = 4.25f;
    const PhysicsBodyHandle bodyHandle = bodies.CreateBodyRecord( body );

    ColliderRecord collider;
    collider.body = bodyHandle;
    collider.sceneObjectId = body.sceneObjectId;
    collider.replayBodyId = id;
    collider.shape = shape;
    collider.restitution = restitution;
    strncpy_s( collider.contactMaterialName, contactMaterial, _TRUNCATE );
    (void)colliders.CreateColliderRecord( collider );

    SceneEntityCreateDesc entity;
    entity.sceneObjectId = body.sceneObjectId;
    entity.SetName( displayName );
    entity.SetRenderTint( 0.1f * static_cast<float>( assetPartIndex + 1u ), 0.4f, 0.7f, 1.0f );
    if ( assetPart )
    {
        entity.SetAssetAffiliation(
            PhysicsSceneObjectId{ 300u }, kLibraryPath, "mixed.live", "saved_asset", assetPart, assetPartIndex );
    }
    entities.CommitAppend( entity, bodyHandle );
}
} // namespace

TEST_CASE( "SceneSnapshotWriter: schema-v2 asset parts reparse from authoritative live state" )
{
    const TemporarySnapshotFiles cleanup;
    WriteAssetLibrary();

    static SceneEntityStore entities;
    static PhysicsBodyStore bodies;
    static ColliderStore colliders;
    entities.Clear();
    entities.ConfigureCapacity( 4 );
    bodies.Clear();
    colliders.Clear();

    AppendEntity( entities,
                  bodies,
                  colliders,
                  300u,
                  "saved_box",
                  BoundingBox( Vector3( 2.0f, 3.0f, 4.0f ), ZERO_VECTOR ),
                  Vector3( 10.0f, 11.0f, 12.0f ),
                  Vector3( 1.0f, 2.0f, 3.0f ),
                  Vector3( 4.0f, 5.0f, 6.0f ),
                  Vector3( 7.0f, 8.0f, 9.0f ),
                  12.0f,
                  0.25f,
                  "wood",
                  false,
                  true,
                  "box",
                  0u );
    AppendEntity( entities,
                  bodies,
                  colliders,
                  42u,
                  "saved_sphere",
                  BoundingSphere( 2.5f, ZERO_VECTOR ),
                  Vector3( 20.0f, 21.0f, 22.0f ),
                  Vector3( 2.0f, 3.0f, 4.0f ),
                  Vector3( 5.0f, 6.0f, 7.0f ),
                  Vector3( 8.0f, 9.0f, 10.0f ),
                  13.0f,
                  0.35f,
                  "stone",
                  true,
                  false,
                  "sphere",
                  1u );
    ConvexHullShape hull;
    REQUIRE( ConvexHullShape::TryLoadFromFile( "SkullbonezData/hulls/pyramid.hull", hull ).ok );
    AppendEntity( entities,
                  bodies,
                  colliders,
                  777u,
                  "saved_hull",
                  hull,
                  Vector3( 30.0f, 31.0f, 32.0f ),
                  Vector3( 3.0f, 4.0f, 5.0f ),
                  Vector3( 6.0f, 7.0f, 8.0f ),
                  Vector3( 9.0f, 10.0f, 11.0f ),
                  14.0f,
                  0.45f,
                  "metal",
                  true,
                  false,
                  "hull",
                  2u );
    AppendEntity( entities,
                  bodies,
                  colliders,
                  99u,
                  "direct_sphere",
                  BoundingSphere( 0.75f, ZERO_VECTOR ),
                  Vector3( 40.0f, 41.0f, 42.0f ),
                  ZERO_VECTOR,
                  ZERO_VECTOR,
                  Vector3( 1.0f, 1.0f, 1.0f ),
                  2.0f,
                  0.15f,
                  "default",
                  false,
                  false,
                  nullptr,
                  0u );

    const std::vector<SceneObjectGroupRecord> groups( 4 );
    const SceneSaveView view{ entities,
                              bodies,
                              colliders,
                              groups.data(),
                              static_cast<int>( groups.size() ),
                              nullptr,
                              0,
                              -9.81f,
                              5.0f,
                              1000.0f,
                              {} };
    SceneSaveRequest request;
    request.path = kSnapshotPath;
    request.cameraEye = Vector3( 1.0f, 2.0f, 3.0f );
    request.cameraView = ZERO_VECTOR;
    request.cameraUp = Vector3( 0.0f, 1.0f, 0.0f );
    request.physicsOn = true;
    REQUIRE( SceneSnapshotWriter::Save( view, request ).ok );

    const TestScene saved = TestScene::LoadFromFile( kSnapshotPath );
    CHECK( saved.GetSchemaVersion() == 2u );
    CHECK( saved.GetAssetLibraryCount() == 1 );
    CHECK( saved.GetAssetInstanceCount() == 1 );
    CHECK( saved.GetAssetPartCount() == 3 );
    CHECK( saved.GetBoxStateCount() == 1 );
    CHECK( saved.GetBallStateCount() == 2 );
    CHECK( saved.GetConvexHullStateCount() == 1 );
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
    CHECK( saved.GetConvexHullState( 0 ).sceneObjectId.value == 777u );
    CHECK( saved.GetConvexHullState( 0 ).contactReleaseOnImpact );
    CHECK( saved.GetConvexHullState( 0 ).contactReleaseImpulseThreshold == doctest::Approx( 4.25f ) );
    CHECK( saved.GetBallState( 0 ).sceneObjectId.value == 99u );
    CHECK( saved.GetAssetPart( 0 ).source == SceneAssetPartSource::BoxState );
    CHECK( saved.GetAssetPart( 1 ).source == SceneAssetPartSource::BallState );
    CHECK( saved.GetAssetPart( 2 ).source == SceneAssetPartSource::ConvexHullState );

    std::ifstream snapshot( kSnapshotPath, std::ios::binary );
    std::ostringstream contents;
    contents << snapshot.rdbuf();
    CHECK( contents.str().find( "\"assetInstances\"" ) != std::string::npos );
    CHECK( contents.str().find( "\"objectName\": \"saved_hull\"" ) != std::string::npos );
}
