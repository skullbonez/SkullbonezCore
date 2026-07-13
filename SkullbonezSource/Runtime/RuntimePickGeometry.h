/*
File: SkullbonezSource/Runtime/RuntimePickGeometry.h
Purpose:
  Declares exact CPU ray tests for runtime model picking.

Summary:
  Runtime pick policy decides which models are eligible, then this helper tests
  the actual collision geometry for each eligible model. It is deliberately
  CPU-only so editor, replay, and manipulator input can share one deterministic
  closest-hit query without a renderer pass.

Glossary:
  Pick transform: Body position plus orientation used to move a collision shape
    from local shape space into world space.
  RayT: Distance along the supplied pick ray to the first shape hit.
  Half-space clipping: Convex-hull ray test that keeps the interval where the
    ray is inside every face plane.

Invariants:
  - Ray directions are expected to be normalized by the caller.
  - Padding and other tool forgiveness belong in picker policy, not in this
    exact geometry helper.

Related:
  - SkullbonezSource/Runtime/RuntimePickService.cpp
  - Agentic/Reports/2026-07-11/interaction-state-machine-closure-review.md
*/
#pragma once

#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"
#include "../Physics/CollisionShape.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct RuntimePickShapeTransform
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
};

bool TryIntersectRuntimePickShape( const Math::CollisionDetection::CollisionShape& shape,
                                   const RuntimePickShapeTransform& transform,
                                   const Math::Vector::Vector3& rayOrigin,
                                   const Math::Vector::Vector3& rayDirection,
                                   float& outT );
} // namespace Runtime
} // namespace SkullbonezCore
