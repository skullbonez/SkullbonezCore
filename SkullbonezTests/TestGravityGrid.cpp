#include "../ThirdPtySource/doctest/doctest.h"
#include "../SkullbonezSource/Runtime/Render/GravityGridVisualizer.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsWorldForces.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include <cmath>
#include <algorithm>
#include "../SkullbonezSource/Rendering/RenderInstanceStore.h"
#include <memory>
#include <vector>
#include "../SkullbonezSource/Physics/ColliderStore.h"

TEST_CASE( "Gravity grid: live wells are finite, optional, and do not change physics" )
{
    using namespace SkullbonezCore;
    Core::Allocation::RuntimeAllocationScope sceneLoad( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    auto bodies = std::make_unique<Physics::PhysicsBodyStore>();
    bodies->ReserveCapacity( 2 );
    Physics::PhysicsBodyCreateDesc desc;
    desc.sceneObjectId = Physics::MakePhysicsSceneObjectId( 1 );
    desc.mass = 100.0f;
    desc.position = Math::Vector::Vector3( 0.0f, 0.0f, 0.0f );
    bodies->CreateBodyRecord( desc, false );
    auto grid = std::make_unique<Runtime::GravityGridVisualizer>();
    Physics::MutualGravitySettings gravity;
    grid->Update( *bodies, gravity, true );
    CHECK( grid->Lines().empty() );
    gravity.enabled = true;
    gravity.gravitationalConstant = 8.0f;
    gravity.softeningLength = 0.0f;
    grid->Update( *bodies, gravity, true );
    REQUIRE_FALSE( grid->Lines().empty() );
    CHECK( grid->SourceCount() == 1 );
    CHECK( grid->MinimumHeight() < 0.0f );
    for ( float value : grid->Lines() )
    {
        CHECK( std::isfinite( value ) );
    }
    const std::vector<float> original( grid->Lines().begin(), grid->Lines().end() );
    const float depth = grid->MinimumHeight();
    CHECK( Physics::PhysicsBodyPosition( bodies->HotFields(), 0 ).y == 0.0f );
    grid->Update( *bodies, gravity, false );
    CHECK( grid->Lines().empty() );
    CHECK( grid->SourceCount() == 0 );
    grid->Update( *bodies, gravity, true );
    CHECK( std::equal( original.begin(), original.end(), grid->Lines().begin() ) );
    bodies->MutableHotFields().positionX[0] = 10.0f;
    grid->Update( *bodies, gravity, true );
    CHECK_FALSE( std::equal( original.begin(), original.end(), grid->Lines().begin() ) );
    CHECK( bodies->HotFields().positionX[0] == 10.0f );
    CHECK( std::isfinite( grid->MinimumHeight() ) );
    grid->Reset();
    CHECK( grid->Lines().empty() );
    bodies->MutableHotFields().positionX[0] = 0.0f;
    grid->Update( *bodies, gravity, true );
    CHECK( grid->MinimumHeight() == depth );
    bodies->Clear();
    grid->Update( *bodies, gravity, true );
    CHECK( grid->Lines().empty() );
}

TEST_CASE( "Gravity grid: displayed identities and level style determine the surface" )
{
    using namespace SkullbonezCore;
    Core::Allocation::RuntimeAllocationScope sceneLoad( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    auto bodies = std::make_unique<Physics::PhysicsBodyStore>();
    bodies->ReserveCapacity( 2 );
    Physics::PhysicsBodyCreateDesc desc;
    desc.sceneObjectId = Physics::MakePhysicsSceneObjectId( 42 );
    desc.mass = 100;
    bodies->CreateBodyRecord( desc, false );
    std::array<Rendering::RenderInstanceRecord, 1> instances;
    instances[0].sceneObjectId = desc.sceneObjectId;
    Physics::MutualGravitySettings gravity;
    gravity.enabled = true;
    gravity.gravitationalConstant = 8.0f;
    Scene::GravityFieldSettings style;
    auto grid = std::make_unique<Runtime::GravityGridVisualizer>();
    grid->UpdatePresented( *bodies, instances, gravity, style );
    REQUIRE( grid->FirstSourceId() == 42 );
    const std::vector<float> initial( grid->Lines().begin(), grid->Lines().end() );
    const float initialHeight = grid->MinimumHeight();
    instances[0].modelMatrix.m[12] = 12;
    grid->UpdatePresented( *bodies, instances, gravity, style );
    CHECK( grid->FirstSourcePosition().x == 12 );
    CHECK( bodies->HotFields().positionX[0] == 0 );
    CHECK_FALSE( std::equal( initial.begin(), initial.end(), grid->Lines().begin() ) );
    instances[0].modelMatrix.m[12] = 0;
    grid->UpdatePresented( *bodies, instances, gravity, style );
    CHECK( std::equal( initial.begin(), initial.end(), grid->Lines().begin() ) );
    style.height = 50;
    style.color = 1;
    style.opacity = 0.4f;
    grid->UpdatePresented( *bodies, instances, gravity, style );
    CHECK( grid->MinimumHeight() == doctest::Approx( initialHeight + 50 ) );
    CHECK( grid->Lines()[3] > grid->Lines()[4] );
    instances[0].sceneObjectId = Physics::MakePhysicsSceneObjectId( 99 );
    grid->UpdatePresented( *bodies, instances, gravity, style );
    CHECK( grid->Lines().empty() );
    CHECK( grid->FirstSourceId() == 0 );
}

TEST_CASE( "Gravity grid: snap follows mesh height and preserves physical endpoints" )
{
    using namespace SkullbonezCore;
    using Math::Vector::Vector3;
    Core::Allocation::RuntimeAllocationScope sceneLoad( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
    auto bodies = std::make_unique<Physics::PhysicsBodyStore>();
    bodies->ReserveCapacity( 3 );
    Rendering::RenderInstanceStore instances;
    for ( int i = 0; i < 3; ++i )
    {
        Physics::PhysicsBodyCreateDesc desc;
        desc.sceneObjectId = Physics::MakePhysicsSceneObjectId( 40 + i );
        desc.mass = 100;
        desc.position = Vector3( i * 4.0f, 20.0f, 0.0f );
        bodies->CreateBodyRecord( desc, false );
        Physics::PhysicsBodyHotState hot;
        hot.position = desc.position;
        const Math::CollisionDetection::BoundingSphere sphere( 2.0f, Math::Vector::ZERO_VECTOR );
        Physics::ColliderRecord collider;
        collider.handle = Physics::PhysicsColliderHandle { static_cast<uint32_t>( i ), 1 };
        collider.body = bodies->Records()[i].handle;
        collider.sceneObjectId = desc.sceneObjectId;
        collider.shape = Math::CollisionDetection::CollisionShapeReference( sphere, 0u );
        collider.shapeKind = i == 2 ? Physics::ColliderShapeKind::Box : Physics::ColliderShapeKind::Sphere;
        collider.boundingRadius = 2.0f;
        Rendering::RenderInstancePresentationRecord presentation;
        presentation.material.baseColor[3] = i == 2 ? 0.0f : 1.0f;
        instances.CommitCreationRow( presentation, bodies->Records()[i], hot, collider, i );
    }
    auto grid = std::make_unique<Runtime::GravityGridVisualizer>();
    Physics::MutualGravitySettings gravity;
    gravity.enabled = true;
    gravity.gravitationalConstant = 8.0f;
    Scene::GravityFieldSettings style;
    CHECK_FALSE( style.snapBalls );
    const auto update = [&]
    {
        for ( int i = 0; i < 3; ++i )
        {
            REQUIRE( instances.OverridePosition( i, Physics::MakePhysicsSceneObjectId( 40 + i ), Vector3( i * 4.0f, 20, 0 ) ) );
        }
        grid->UpdatePresented( *bodies, instances.Records(), gravity, style );
        grid->SnapSpheres( instances );
    };
    update();
    CHECK( grid->SnappedCount() == 0 );
    style.snapBalls = true;
    update();
    CHECK( grid->SnappedCount() == 2 );
    CHECK( grid->SourceCount() == 3 );
    CHECK_FALSE( instances.Records()[2].editorVisible );
    REQUIRE( instances.SetEditorVisible( 2, true ) );
    CHECK_FALSE( instances.Records()[2].editorVisible );
    CHECK( grid->FirstSnappedId() == 40 );
    for ( int i = 0; i < 2; ++i )
    {
        const auto& matrix = instances.Records()[i].modelMatrix;
        CHECK( matrix.m[13] - 2 == doctest::Approx( grid->HeightAt( matrix.m[12], matrix.m[14] ) ) );
        CHECK( matrix.m[12] == i * 4.0f );
        CHECK( matrix.m[14] == 0 );
        CHECK( bodies->HotFields().positionY[i] == 20 );
        CHECK( instances.Records()[i].currentPosition.y == 20 );
    }
    CHECK( instances.Records()[2].modelMatrix.m[13] == 20 );
    const std::vector<float> surface( grid->Lines().begin(), grid->Lines().end() );
    // Independently compare the sampler to actual line vertices, including the
    // upper edge of the final mesh cell.
    for ( std::size_t i = 0; i < surface.size(); i += 6 )
    {
        CHECK( grid->HeightAt( surface[i], surface[i + 2] ) == doctest::Approx( surface[i + 1] ).epsilon( 0.00001 ) );
    }
    const float firstY = instances.Records()[0].modelMatrix.m[13];
    style.height = 50;
    update();
    CHECK( instances.Records()[0].modelMatrix.m[13] == doctest::Approx( firstY + 50 ) );
    style.height = 0;
    update();
    CHECK( std::equal( surface.begin(), surface.end(), grid->Lines().begin() ) );
    CHECK_FALSE( instances.OverridePosition( 0, Physics::MakePhysicsSceneObjectId( 99 ), Vector3( 0, 900, 0 ) ) );
    style.snapBalls = false;
    update();
    CHECK( grid->SnappedCount() == 0 );
    CHECK( instances.Records()[0].modelMatrix.m[13] == 20 );
    style.snapBalls = true;
    REQUIRE( instances.OverridePosition( 0, Physics::MakePhysicsSceneObjectId( 40 ), Vector3( 900, 20, 0 ) ) );
    grid->UpdatePresented( *bodies, instances.Records(), gravity, style );
    grid->SnapSpheres( instances );
    CHECK( grid->Lines()[grid->Lines().size() - 6] > 900 );
    CHECK( instances.Records()[0].modelMatrix.m[13] - 2 == doctest::Approx( grid->HeightAt( 900, 0 ) ) );
    update();
    CHECK( std::equal( surface.begin(), surface.end(), grid->Lines().begin() ) );
    REQUIRE( instances.SetEditorVisible( 1, false ) );
    update();
    CHECK( grid->SnappedCount() == 1 );
    CHECK( grid->SourceCount() == 3 );
    gravity.enabled = false;
    update();
    CHECK( grid->SnappedCount() == 0 );
}
