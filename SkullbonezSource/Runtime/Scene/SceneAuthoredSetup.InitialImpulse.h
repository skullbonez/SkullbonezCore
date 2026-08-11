/*
File: SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.InitialImpulse.h
Purpose:
  Applies one parsed ball's optional initial impulse to its registered Physics body.

Summary:
  This synchronous scene-load seam owns the policy and value conversion between
  a cold SceneBall record and PhysicsEngine's world-space pending-impulse command.

Invariants:
  - Fixed balls and zero impulses never queue pending solver input.
  - The application lever arm is passed unchanged as a world-axis offset from
    the body's center of mass, never as an absolute position.

Related:
  - SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp
  - SkullbonezSource/Scene/AuthoredScene.h
  - SkullbonezSource/Physics/PhysicsEngine.h
*/
#pragma once

#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Scene/AuthoredScene.h"

namespace SkullbonezCore::Runtime
{
inline void ApplyAuthoredBallInitialImpulse( Physics::PhysicsEngine& physics, Physics::PhysicsBodyHandle body,
                                             const SceneBall& ball )
{
    if ( ball.isFixed || ( ball.forceX == 0.0f && ball.forceY == 0.0f && ball.forceZ == 0.0f ) )
    {
        return;
    }

    physics.SetPendingBodyImpulse( body, Math::Vector::Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                                   Math::Vector::Vector3( ball.impulseWorldOffsetFromCenterX,
                                                          ball.impulseWorldOffsetFromCenterY,
                                                          ball.impulseWorldOffsetFromCenterZ ) );
}
} // namespace SkullbonezCore::Runtime
