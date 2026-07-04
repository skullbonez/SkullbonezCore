/*
File: SkullbonezSource/Runtime/RuntimePickService.h
Purpose:
  Defines explicit runtime picking requests for editor, tool, and replay input.

Mental model:
  Input routing should choose a pick purpose, borrow the current physics body
  and collider stores, then ask one service for the selection result.

Glossary:
  Pick purpose: The tool-specific policy for interpreting a mouse ray.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - RuntimePickRequest borrows physics stores for one call; the service does
    not retain them.
  - RuntimePickResult.modelIndex is the dense compatibility row/model index for
    the same store snapshot and frame that produced the result.

Related:
  - Agentic/Plans/runtime-interaction-state-machine-hardening-plan.md
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
*/
#pragma once

#include <cfloat>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics

namespace Basics
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
    int modelIndex = -1;
    float rayT = FLT_MAX;
};

class RuntimePickService
{
  public:
    static bool TryPickModel( const RuntimePickRequest& request, RuntimePickResult& outResult );
};
} // namespace Basics
} // namespace SkullbonezCore
