/*
File: SkullbonezSource/Runtime/Interaction/RuntimePickService.h
Purpose:
  Defines explicit runtime picking requests for editor, tool, and replay input.

Summary:
  RuntimePickService.h defines explicit runtime picking requests for editor,
  tool, and replay input. As a public header, keep edits anchored on local
  owner boundaries and call direction and on the glossary/invariants below.

Glossary:
  Pick purpose: The tool-specific policy for interpreting a mouse ray.
  Physics body handle: Generational id for a live row in `PhysicsBodyStore`.
  Model row hint: Dense model-order cache used by synchronous UI work; it is
    never authority for a physics command.

Invariants:
  - RuntimePickRequest borrows physics stores for one call; the service does
    not retain them.
  - RuntimePickResult.body is the physics-store handle for command paths.
  - RuntimePickResult.collider is the collider-store handle paired with body.
  - RuntimePickResult.modelRow is the dense-row hint for synchronous UI work
    in the same store snapshot and frame that produced the result.

Related:
  - Agentic/Reports/2026-07-11/interaction-state-machine-closure-review.md
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
*/
#pragma once

#include <cfloat>

#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics

namespace Runtime
{
enum class RuntimePickPurpose
{
    EditorSelection,
    AttachCameraTarget,
    ReplayPathTarget,
    ManipulatorPickup
};

struct RuntimePickRequest
{
    RuntimePickPurpose purpose = RuntimePickPurpose::EditorSelection;
    const Physics::PhysicsBodyStore* bodyStore = nullptr;
    const Physics::ColliderStore* colliderStore = nullptr;
    Math::Vector::Vector3 rayOrigin = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rayDirection = Math::Vector::ZERO_VECTOR;
};

struct RuntimePickResult
{
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle collider;
    Physics::ModelRowHint modelRow;
    float rayT = FLT_MAX;
};

class RuntimePickService
{
  public:
    static bool TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult );
};
} // namespace Runtime
} // namespace SkullbonezCore
