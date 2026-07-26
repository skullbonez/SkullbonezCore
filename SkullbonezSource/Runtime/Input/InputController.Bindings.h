/*
File: InputController.Bindings.h
Purpose:
  Publishes the keyboard binding table consumed by InputFrameExecution.

Summary:
  Keyboard shortcuts are data first and side effects second. This module names
  the key/action/context rows so tests and runtime dispatch read the same table,

  while InputFrameExecution dispatches each action to its named domain owner.

Glossary:
  Virtual key: Win32 integer key code sampled in DeviceInputFrame.
  Runtime input action: Engine command name produced from a key, UI, mouse, or
    scripted source.
  Context mask: Bit set describing which dispatch pass or runtime state owns a
    binding row.
  Binding view: Borrowed pointer/count pair over static rows; callers must not
    store it past process lifetime assumptions.

Invariants:
  - A key/context row maps to one action in the shared table.
  - This module does not poll hardware or apply gameplay side effects.

Related:
  - InputController.h defines the action and context vocabulary.
  - InputRouter.Interactions.cpp executes the actions during the frame.
*/
#pragma once

#include "InputController.h"

#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
struct RuntimeInputKeyBindingView
{
    const RuntimeInputKeyBinding* bindings = nullptr;
    std::size_t count = 0;
};

// Returns the static TakeInput keyboard table. The rows are immutable process
// data and may be inspected by tests without constructing Run.
RuntimeInputKeyBindingView TakeInputKeyboardBindings();
} // namespace Runtime
} // namespace SkullbonezCore
