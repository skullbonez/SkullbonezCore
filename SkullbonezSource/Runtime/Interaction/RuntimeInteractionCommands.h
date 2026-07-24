/*
File: SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h
Purpose:
  Defines the small command/event vocabulary used by runtime input routing.

Summary:
  Runtime input code converts mouse/editor decisions into narrow selection or
  gesture commands. The owning controller validates and commits the mutation;
  only then does it publish the corresponding event.

Glossary:
  Command: A synchronous runtime mutation request emitted by routed input.
  Event: A lightweight observation record published after a command succeeds.
  Selection scope: Which workspace, editor or inspect, owns a selected model.
  Selection body: Store-owned body/collider handles captured when the command
    is enqueued; dense rows are derived only during synchronous commit.
  Gesture command: Typed begin/end request carrying capture owner and the
    owner-specific body, axis, or gizmo-mode payload needed for the drag.

Invariants:
  - Non-clear selection commands capture body/collider handles before enqueue.
  - Command payloads never retain a dense row as object identity.
  - Events must not mutate world state; they describe completed mutations.
  - Rejected gesture commands leave the event empty and cannot claim capture.

Related:
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Physics/PhysicsHandles.h"
#include "RuntimeInteractionController.h"

namespace SkullbonezCore
{
namespace Runtime
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
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle collider;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
    bool claimSelectionOwner = true;
};

struct RuntimeInteractionSelectionPlan
{
    Physics::ModelRowHint previousModelRow;
    Physics::ModelRowHint modelRow;
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
    Physics::ModelRowHint previousModelRow;
    Physics::ModelRowHint modelRow;
    Physics::PhysicsBodyHandle previousBody;
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle previousCollider;
    Physics::PhysicsColliderHandle collider;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
};

enum class RuntimeGestureCommandAction
{
    Begin,
    End
};

struct RuntimeGestureCommand
{
    RuntimeGestureCommandAction action = RuntimeGestureCommandAction::Begin;
    RuntimeInteractionGesture gesture;
    RuntimePointerCaptureOwner captureOwner = RuntimePointerCaptureOwner::ToolGesture;
    InteractionExitReason reason = InteractionExitReason::BeginGesture;
};

enum class RuntimeGestureEventType
{
    None,
    Began,
    Ended
};

struct RuntimeGestureEvent
{
    RuntimeGestureEventType type = RuntimeGestureEventType::None;
    RuntimeInteractionGesture previousGesture;
    RuntimeInteractionGesture gesture;
    RuntimePointerCaptureOwner previousPointerCapture = RuntimePointerCaptureOwner::None;
    RuntimePointerCaptureOwner pointerCapture = RuntimePointerCaptureOwner::None;
};

} // namespace Runtime
} // namespace SkullbonezCore
