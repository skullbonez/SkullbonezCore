/*
File: SkullbonezTests/TestSceneAuthoredImpulseSetup.cpp
Purpose:
  Proves a loaded authored impulse reaches the registered Physics body unchanged.

Summary:
  The fixture loads the committed ragdoll playground, registers its wake ball
  through public Physics descriptors, and invokes the same scene-load handoff
  used by SceneAuthoredSetup before inspecting Physics-owned pending state.

Invariants:
  - The authored impulse remains a world vector.
  - The application lever arm reaches Physics as a world-axis offset from the
    body's center of mass rather than the body's absolute position.

Related:
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.InitialImpulse.h
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
  - SkullbonezData/scenes/ragdoll_playground.scene.json
*/

#include "../ThirdPtySource/doctest/doctest.h"
#include "TestResultLoadFixtures.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"
#include "../SkullbonezSource/Physics/BoundingSphere.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.InitialImpulse.h"
#include "../SkullbonezSource/Scene/AuthoredScene.h"

using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::MakeColliderCreateDesc;
using SkullbonezCore::Physics::MakePhysicsBodyCreateDesc;
using SkullbonezCore::Physics::PhysicsAuthoredBodyRegistration;
using SkullbonezCore::Physics::PhysicsBodyMotionKind;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Runtime::ApplyAuthoredBallInitialImpulse;
using SkullbonezCore::Runtime::AuthoredScene;

TEST_CASE( "SceneAuthoredSetup: wake_ball queues a center-applied world impulse" )
{
    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    AuthoredScene scene;
    REQUIRE(
        SkullbonezTests::ResultLoadFixtures::TryLoadAuthoredScene( diagnostics,
                                                                   "SkullbonezData/scenes/ragdoll_playground.scene.json",
                                                                   scene ) );
    REQUIRE( scene.GetBallCount() == 1 );
    const auto& wakeBall = scene.GetBall( 0 );

    PhysicsEngine physics;
    const BoundingSphere shape( wakeBall.m_radius, Vector3( 0.0f, 0.0f, 0.0f ) );
    const auto bodyDesc = MakePhysicsBodyCreateDesc( wakeBall.sceneObjectId, shape,
                                                     Vector3( wakeBall.posX, wakeBall.posY, wakeBall.posZ ),
                                                     SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                                     Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                                                     Vector3( wakeBall.moment, wakeBall.moment, wakeBall.moment ),
                                                     wakeBall.m_mass, wakeBall.restitution, PhysicsBodyMotionKind::Dynamic,
                                                     wakeBall.name );
    PhysicsAuthoredBodyRegistration registration;

    {
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
        physics.ReserveAuthoredBodyCapacity( 1u, 1u );
        registration = physics.RegisterAuthoredBody( bodyDesc, MakeColliderCreateDesc( shape, wakeBall.restitution, 0u ) );
    }

    REQUIRE( registration.IsValid() );
    ApplyAuthoredBallInitialImpulse( physics, registration.body, wakeBall );

    const PhysicsBodyStore& bodies = PhysicsEngine::ReadBodies( physics );
    const auto* body = bodies.RecordForHandle( registration.body );
    REQUIRE( body != nullptr );
    CHECK( body->pendingImpulse == Vector3( 0.0f, 0.0f, 120.0f ) );
    CHECK( body->pendingImpulseWorldOffset == Vector3( 0.0f, 0.0f, 0.0f ) );
}
