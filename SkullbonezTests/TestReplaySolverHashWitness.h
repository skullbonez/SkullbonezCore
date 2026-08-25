/*
File: TestReplaySolverHashWitness.h
Purpose:
  Declares the test-only bridge from Physics determinism fixtures to the
  Runtime-owned production Replay solver fingerprint.

Summary:
  Renderer-free tests compare Physics state directly. The full MSVC test lane
  additionally calls this bridge so worker-count equality is checked by the
  same production fingerprint used by Replay without exposing Replay types to
  the portable translation unit.

Invariants:
  - The declaration carries only borrowed Physics owners and no Runtime type.
  - The implementation is compiled by SKULLBONEZ_TESTS, never portable CMake.

Related:
  - SkullbonezTests/TestDeterminism.cpp
  - SkullbonezTests/TestReplaySolverHashWitness.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/

#pragma once

namespace SkullbonezCore::Physics
{
class PhysicsEngine;
}

namespace SkullbonezTests
{
void CheckProductionReplaySolverHashEqual( const SkullbonezCore::Physics::PhysicsEngine& left,
                                           const SkullbonezCore::Physics::PhysicsEngine& right );
}
