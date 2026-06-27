/*
File: SkullbonezSource/Runtime/RuntimeInteractionCommands.h
Purpose:
  Defines the small command/event vocabulary used by runtime input routing.

Mental model:
  Runtime input code converts mouse/editor decisions into narrow command
  records before mutating selection state. These records describe intent; the
  process-level Run object still decides when to execute them.

Glossary:
  Command: A synchronous runtime mutation request emitted by routed input.
  Event: A lightweight observation record published after a command succeeds.
  Selection scope: Which workspace, editor or inspect, owns a selected model.

Invariants:
  - Commands carry model indices in frame-local GameModelCollection order.
  - Events must not mutate world state; they describe completed mutations.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

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
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
    bool claimSelectionOwner = true;
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
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
};

} // namespace Basics
} // namespace SkullbonezCore
