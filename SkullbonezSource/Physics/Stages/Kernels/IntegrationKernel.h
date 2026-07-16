/*
File: SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.h
Purpose:
  Declares the pinned eight-lane position/velocity integration pilot.

Summary:
  PhysicsForceStage supplies borrowed SoA rows and one consecutive block of up
  to eight bodies. The implementation simplifies velocity components, advances
  position with explicit fused multiply-add operations, and returns the active
  lane mask so the stage can complete orientation and terrain work through the
  body-store owner.

Glossary:
  Lane: One body slot inside an eight-wide vector operation.
  Lane mask: Bit set selecting a valid, awake, dynamic, non-sleeping body.
  FMA (Fused Multiply-Add): One explicitly rounded `position + velocity * dt`
    operation used only by the enabled SIMD path.
  Masked tail: Final partial block whose absent lanes cannot read or write rows.

Invariants:
  - This API has no runtime CPU dispatch; its translation unit is compiled for
    the campaign's pinned AVX2/FMA hardware envelope.
  - Every active lane is independent; no horizontal reduction or cross-lane
    accumulation can change body-order semantics.
  - A returned bit is set exactly when position and velocity were written.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/IntegrationKernel.cpp
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#pragma once

#include <cstdint>
#include <span>

#include "../../PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Physics::Kernels
{
inline constexpr int INTEGRATION_LANE_COUNT = 8;

// Advances one block beginning at an eight-row boundary. The returned low
// eight bits identify rows that require scalar orientation/terrain completion.
uint32_t IntegratePositionAvx2( const PhysicsBodyHotFieldsView& hotFields,
                                std::span<const uint8_t> sleepState,
                                std::span<const float> timeRemaining,
                                int bodyBegin,
                                int bodyCount );
} // namespace Physics::Kernels
} // namespace SkullbonezCore
