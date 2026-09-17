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
