/*
File: SkullbonezSource/Runtime/RuntimeInteractionCommands.h
Purpose:
  Defines the small command/event vocabulary used by runtime input routing.

Mental model:
  Runtime input code converts mouse/editor decisions into narrow command
  records before mutating selection state. RuntimeTools validates commands into
  exact plans, composition applies any requested owner transition, and the tool
  owner commits the plan without callbacks into Run.

Glossary:
  Command: A synchronous runtime mutation request emitted by routed input.
  Event: A lightweight observation record published after a command succeeds.
  Selection scope: Which workspace, editor or inspect, owns a selected model.
  Selection body: Store-owned body/collider handles for the selected object;
    the model index is only the UI/order hint paired with those handles.

Invariants:
  - Selection commands carry physics handles when the caller already has them.
  - Model indices remain frame-local UI hints, not physics authority.
  - Events must not mutate world state; they describe completed mutations.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Physics/PhysicsHandles.h"

namespace SkullbonezCore
{
namespace Basics
{
// Concept: These records are deliberately tiny so editor/replay/input modules
// can request a mutation without depending on Run's private state layout.
enum class RuntimeInteractionCommandType
{
    None,
    SetEditorSelection
};

enum class RuntimeInteractionSelectionScope
{
    Editor,
    Inspect
};

struct RuntimeInteractionCommand
{
    RuntimeInteractionCommandType type = RuntimeInteractionCommandType::None;
    int modelIndex = -1;
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle collider;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
    bool claimSelectionOwner = true;
};

struct RuntimeInteractionSelectionPlan
{
    int previousModelIndex = -1;
    int modelIndex = -1;
    Physics::PhysicsBodyHandle previousBody;
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle previousCollider;
    Physics::PhysicsColliderHandle collider;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
    bool claimSelectionOwner = false;
};

enum class RuntimeInteractionEventType
{
    None,
    SelectionChanged
};

struct RuntimeInteractionEvent
{
    RuntimeInteractionEventType type = RuntimeInteractionEventType::None;
    int previousModelIndex = -1;
    int modelIndex = -1;
    Physics::PhysicsBodyHandle previousBody;
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle previousCollider;
    Physics::PhysicsColliderHandle collider;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
};

} // namespace Basics
} // namespace SkullbonezCore
