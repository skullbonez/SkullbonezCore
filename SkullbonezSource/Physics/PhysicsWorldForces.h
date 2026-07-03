/*
File: SkullbonezSource/Physics/PhysicsWorldForces.h
Purpose:
  Names the world-level force scalars consumed by physics integration.

Mental model:
  WorldEnvironment owns live gravity and fluid settings. Physics receives this
  value snapshot for one deterministic tick so body, collider, and solver code
  can move off GameModel-owned world pointers without reaching back into runtime
  scene services.

Glossary:
  Fluid surface: World-space Y plane where the fluid medium begins.
  Fluid density: Density of the liquid medium used by buoyancy and drag.
  Gas density: Density of the air-like medium above the fluid surface.
  Angular drag multiplier: Fluid damping scale applied to submerged spin.

Invariants:
  - Values are copied at the runtime/physics boundary and treated as immutable
    during one physics tick.
  - Gravity is signed; negative Y means downward acceleration.

Related:
  - SkullbonezSource/World/WorldEnvironment.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsWorldForces
{
    float fluidSurfaceHeight = 0.0f;    // World-space Y plane where water begins.
    float fluidDensity = 0.0f;          // kg/m^3 liquid density used by buoyancy.
    float gasDensity = 0.0f;            // kg/m^3 air density used by drag blending.
    float gravity = 0.0f;               // m/s^2; negative values accelerate downward.
    float angularDragMultiplier = 2.0f; // Damping scale for submerged angular velocity.
};
} // namespace Physics
} // namespace SkullbonezCore
