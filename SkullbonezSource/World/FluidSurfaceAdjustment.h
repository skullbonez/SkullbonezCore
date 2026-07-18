/*
File: SkullbonezSource/World/FluidSurfaceAdjustment.h
Purpose:
  Defines the typed interaction command for moving the world's fluid surface.

Summary:
  Input translates physical controls into a world-space velocity. The world
  owner applies that value over a simulation interval without learning which
  keyboard keys or automation source produced it.

Glossary:
  Fluid surface: World-space Y plane separating the configured fluid and gas.
  Adjustment velocity: Signed vertical speed in world meters per second.

Invariants:
  - Zero velocity is a no-op.
  - The command contains domain units, not device or key vocabulary.
  - The value is consumed synchronously and retains no owner reference.

Related:
  - SkullbonezSource/Runtime/InputRouter.cpp
  - SkullbonezSource/World/WorldEnvironment.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Environment
{
struct FluidSurfaceAdjustment
{
    float velocityMetersPerSecond = 0.0f;

    float DeltaMeters( float deltaSeconds ) const
    {
        return velocityMetersPerSecond * deltaSeconds;
    }
};
} // namespace Environment
} // namespace SkullbonezCore
